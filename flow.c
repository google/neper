/*
 * Copyright 2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/tcp.h>
#include <linux/version.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "flow.h"
#include "logging.h"
#include "socket.h"
#include "stats.h"
#include "thread.h"

/*
 * We define the flow struct locally to this file to force outside users to go
 * through the API functions.
 */

struct flow {
        struct thread * f_thread;   /* owner of this flow */
        flow_handler    f_handler;  /* state machine: current callback */
        void *          f_opaque;   /* state machine: opaque state */
        void *          f_mbuf;     /* send/recv message buffer */
        int             f_fd;       /* open file descriptor */
        int             f_id;       /* index of this flow within the thread */

        /* support for paced send: track next event and epoll events */
        uint64_t        f_next_event;  /* absolute time (ns) of next event */
        uint32_t        f_events;   /* pending epoll events */

        struct neper_stat *f_stat;

	/* TCP RX zerocopy state. */
	void *f_rx_zerocopy_buffer;
	size_t f_rx_zerocopy_buffer_sz;

        long rtt_log_count;
};

int flow_fd(const struct flow *f)
{
        return f->f_fd;
}

int flow_id(const struct flow *f)
{
        return f->f_id;
}

void *flow_mbuf(const struct flow *f)
{
        return f->f_mbuf;
}

void *flow_opaque(const struct flow *f)
{
        return f->f_opaque;
}

struct neper_stat *flow_stat(const struct flow *f)
{
        return f->f_stat;
}

struct thread *flow_thread(const struct flow *f)
{
        return f->f_thread;
}

void flow_event(const struct epoll_event *e)
{
        struct flow *f = e->data.ptr;
        f->f_events = e->events; /* support for paced send */
        f->f_handler(f, e->events);
}

static void flow_ctl(struct flow *f, int op, flow_handler fh, uint32_t events,
                     bool or_die)
{
        if (f->f_fd >= 0) {
                f->f_handler = fh;

                struct epoll_event ev;
                ev.events = events | EPOLLRDHUP;
                ev.data.ptr = f;

                int err = epoll_ctl(f->f_thread->epfd, op, f->f_fd, &ev);
                if (err) {
                        if (or_die)
                                PLOG_FATAL(f->f_thread->cb, "epoll_ctl()");
                        close(f->f_fd);
                        f->f_fd = -1;
                        flow_delete(f);
                }
        }
}

static void flow_del_or_die(struct flow *f)
{
        flow_ctl(f, EPOLL_CTL_DEL, NULL, 0, true);
        close(f->f_fd);
        f->f_fd = -1;
}

void flow_mod(struct flow *f, flow_handler fh, uint32_t events, bool or_die)
{
        flow_ctl(f, EPOLL_CTL_MOD, fh, events, or_die);
}

void flow_reconnect(struct flow *f, flow_handler fh, uint32_t events)
{
        flow_del_or_die(f);
        f->f_fd = socket_connect_one(f->f_thread, SOCK_NONBLOCK);
        flow_ctl(f, EPOLL_CTL_ADD, fh, events, true);
}

void flow_init_rx_zerocopy(struct flow *f, int buffer_size, struct callbacks *cb)
{
        // Use RCVLOWAT to reduce syscall overhead.
        int rcvlowat = buffer_size;
        if (setsockopt(f->f_fd, SOL_SOCKET, SO_RCVLOWAT, &rcvlowat,
                       sizeof(rcvlowat)) == -1)
                PLOG_FATAL(cb, "setsockopt(SO_RCVLOWAT)");

        // Zerocopy requires mmap'd pages. Each flow has its own pages.
        f->f_rx_zerocopy_buffer = mmap(NULL, buffer_size, PROT_READ,
                        MAP_SHARED, f->f_fd, 0);
        if (f->f_rx_zerocopy_buffer == (void *)-1)
                PLOG_FATAL(cb, "failed to map RX zerocopy buffer");

        f->f_rx_zerocopy_buffer_sz = buffer_size;
}

long flow_rtt_log_count(const struct flow *f)
{
        return f->rtt_log_count;
}

void flow_increment_rtt_log_count(struct flow *f)
{
        f->rtt_log_count++;
}

struct flow *flow_create(const struct flow_create_args *args)
{
        struct thread *t = args->thread;
        struct flow *f = calloc_or_die(1, sizeof(struct flow), t->cb);
        int events = args->events;      /* must be overriden in some cases */

        f->f_thread = t;
        f->f_opaque = args->opaque;
        f->f_fd     = args->fd;

        if (args->mbuf_alloc) {
                /* The next line is a hack. mbuf_alloc implies traffic and */
                /* traffic implies a flow_id is needed. */
                f->f_id   = t->flow_count++;
                f->f_mbuf = args->mbuf_alloc(t);
        }
        if (args->stat) {
                f->f_stat = args->stat(f);
                if (f->f_stat != NULL) {
                        /* If we keep stats, remember this flow */
                        thread_store_flow_or_die(t, f);
                }
        }
        /* In bidirectional mode, acks are piggybacked behind data and this
         * creates unwanted dependencies between forward and reverse flows.
         *
         * To solve the problem, IN BIDIRECTIONAL STREAM MODE ONLY we use
         * one tcp socket per direction (the user-specified number of flows
         * is doubled after option parsing), used as follows:
         * - client and server always read from all sockets
         * - client sends only on half of the sockets (those witheven f_id).
         *   This is done by disabling EPOLLOUT on alternate sockets, below.
         * - server starts sending on all sockets, but will stop sending and
         *   disable EPOLLOUT on sockets on which data is received.
         *   This is done in stream_handler.
         * The above allows to have half of the sockets in tx, and half in rx,
         * without control plane modifications.
         * For backward compatibility reasons, this is controlled by a
         * command-line option, --split-bidir
         */
        if (t->opts->split_bidir && t->opts->client)
                events &= (f->f_id & 1) ? EPOLLOUT : EPOLLIN;

        flow_ctl(f, EPOLL_CTL_ADD, args->handler, events, true);
	return f;
}

/* Returns true if the deadline for the flow has expired.
 * Takes into account the rounding of the timer.
 */
static int deadline_expired(const struct flow *f)
{
        return f->f_thread->rl.now + f->f_thread->rounding_ns >= f->f_next_event;
}

/* Flows with delayed events are stored unsorted in a per-thread array.
 * Scan the array, remove and serve pending flows updating their next time,
 * and update the deadline in the thread to the earliest pending flow.
 */
static void run_ready_handlers(struct thread *t)
{
        int i;
        uint64_t next_deadline = ~0ULL;
        struct rate_limit *rl = &t->rl;

        for (i = 0; i < rl->pending_count; i++) {
                struct flow *f = rl->pending_flows[i];

                if (!deadline_expired(f)) {
                        rl->reschedule_count++;
                        if (f->f_next_event < next_deadline)
                                next_deadline = f->f_next_event;
                        continue;
                }
                /* Replace current entry with last event. Handlers may safely
                 * cause postponed events to be added to the array.
                 */
                rl->pending_flows[i--] = rl->pending_flows[--rl->pending_count];
                /* Run the handler, but DO NOT UPDATE THE DEADLINE: it will be
                 * done in flow_postpone when the dealayed handler is run.
                 */
                f->f_handler(f, f->f_events);
        }
        rl->next_event = next_deadline;
}

/* Serve pending eligible flows, and return the number of milliseconds to
 * the next scheduled event. To be called before blocking in the main loop.
 *
 * Returns true iff the timeout should be indefinite.
 */
bool flow_serve_pending(struct thread *t, struct timespec *timeout)
{
        /* The default timeout of 1m is an upper bound that will shrink if
         * there are any pending flows.
         */
        int64_t ns = t->opts->busywait ?:
                     t->opts->nonblocking ? 600 * 1000 * 1000 * (int64_t)1000 : -1;
        struct rate_limit *rl = &t->rl;

        while (rl->pending_count) {
                /* Take a timestamp, subtract the start time so all times are
                 * relative to the start of the main loop (easier to read).
                 */
                struct timespec t1;

                common_gettime(&t1);
                rl->now = t1.tv_sec * 1000000000UL + t1.tv_nsec;
                if (rl->start_time == 0)
                        rl->start_time = rl->now;
                rl->now -= rl->start_time;
                if (rl->now + t->rounding_ns < rl->next_event) {
                        /* Too early, compute time to next event and break. */
                        int64_t wait_ns = rl->next_event + t->rounding_ns - rl->now;
                        if (ns == -1 || wait_ns < ns) {
                                ns = wait_ns;
                                rl->sleep_count++;
                        }
                        break;
                }
                run_ready_handlers(t);
        }

        *timeout = ns_to_timespec(ns);
        return ns == -1;
}

/* Check if the flow must be postponed. If yes, record the flow in the array
 * of pending flows for the thread. Otherwise, update the deadline for the
 * next run of the handler.
 */
int flow_postpone(struct flow *f)
{
        struct thread *t = f->f_thread;

        if (deadline_expired(f)) {
                /* Can serve the flow now, update next deadline. Only one of
                 * delay or noburst can be set. */
                if (t->opts->delay) {
                        flow_update_next_event(f, t->opts->delay);
                } else {
                        f->f_next_event = thread_next_slot(t);
                }

                return false;
        }

        struct rate_limit *rl = &t->rl;
        /* flow must be delayed, record in the array, update next
         * event for the thread, and disable epoll on this fd.
         */
        if (f->f_next_event < rl->next_event)
                rl->next_event = f->f_next_event;
        rl->pending_flows[rl->pending_count++] = f;
        rl->delay_count++;
        flow_mod(f, f->f_handler, 0, true); /* disable epoll */
        return true;
}

void flow_delete(struct flow *f)
{
        flow_del_or_die(f);
        if (f->f_stat != NULL) {
                /* If we kept stats, forget this flow */
                thread_clear_flow_or_die(f->f_thread, f);
        }

        /* TODO: need to free the stat struct here for crr tests */
        free(f->f_opaque);
        /* Right now the test is always false, but let's leave it in case
         * we want to implement independent per-flow buffers.
         */
        if (f->f_mbuf != f->f_thread->f_mbuf)
                free(f->f_mbuf);

        /* Cleanup TCP RX zerocopy. */
        if (f->f_rx_zerocopy_buffer)
                munmap(f->f_rx_zerocopy_buffer, f->f_rx_zerocopy_buffer_sz);

        free(f);
}

void flow_update_next_event(struct flow *f, uint64_t duration)
{
        f->f_next_event += duration;
}

ssize_t flow_recv_zerocopy(struct flow *f, void *copybuf, size_t copybuf_len) {
        struct tcp_zerocopy_receive zc = {0};
        socklen_t zc_len = sizeof(zc);
        ssize_t n_read;
        int result;

        /* Setup both the mmap address and extra buffer for bytes that aren't
         * zerocopy-able.
         */
        zc.address = (__u64)f->f_rx_zerocopy_buffer;
        zc.length = copybuf_len; /* Same size used as zerocopy buffer. */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 82)
        /* The kernel will effectively use copybuf_len as a hint as to what the
         * cutoff point between zerocopy and recv is. So passing a large copybuf
         * causes less zerocopy. Thus we pass just under a page to maximize
         * zerocopying.
         */
        zc.copybuf_address = (__u64)copybuf;
        zc.copybuf_len = copybuf_len < 4096 ? copybuf_len : 4095;
#endif

        result = getsockopt(f->f_fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, &zc,
                        &zc_len);
        if (result == -1)
                return result;

        /* Handle overflow data, i.e. bytes that couldn't be zerocopied. */
        if (zc.recv_skip_hint) {
                int read_len = zc.recv_skip_hint < copybuf_len ?
                        zc.recv_skip_hint : copybuf_len;
                result = read(f->f_fd, copybuf, read_len);
                if (result < 0)
                        PLOG_FATAL(f->f_thread->cb, "failed to read extra "
                                        "bytes");
        }

        /* Handle zerocopy data. */
        if (zc.length) {
                flow_thread(f)->io_stats.rx_zc_bytes += zc.length;
                madvise(f->f_rx_zerocopy_buffer, zc.length, MADV_DONTNEED);
        }

        n_read = zc.recv_skip_hint + zc.length;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 82)
        n_read += zc.copybuf_len;
#endif
        return n_read;
}
