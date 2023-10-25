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

#include "common.h"
#include "flow.h"
#include "socket.h"
#include "thread.h"
#include "stats.h"
#ifdef WITH_TCPDEVMEM_CUDA
#include "tcpdevmem_cuda.h"
#endif /* WITH_TCPDEVMEM_CUDA */
#ifdef WITH_TCPDEVMEM_UDMA
#include "tcpdevmem_udma.h"
#endif /* WITH_TCPDEVMEM_UDMA */

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

void flow_create(const struct flow_create_args *args)
{
        struct thread *t = args->thread;
        struct flow *f = calloc_or_die(1, sizeof(struct flow), t->cb);

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
        flow_ctl(f, EPOLL_CTL_ADD, args->handler, args->events, true);
}

/* Returns true if the deadline for the flow has expired.
 * Takes into account the rounding of the timer.
 */
static const int ONE_MS = 1000000;
static int deadline_expired(const struct flow *f)
{
        return (f->f_thread->rl.now + ONE_MS/2 >= f->f_next_event);
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
 */
int flow_serve_pending(struct thread *t)
{
        struct rate_limit *rl = &t->rl;
        int64_t ms = t->opts->nonblocking ? 10 : -1;

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
                /* The granularity of the timer is 1ms so round times. */
                if (rl->now + ONE_MS/2 < rl->next_event) {
                        /* Too early, compute time to next event and break. */
                        int64_t wait_ms = (rl->next_event + ONE_MS/2 - rl->now)/ONE_MS;
                        if (ms == -1 || wait_ms < ms) {
                                ms = wait_ms;
                                rl->sleep_count++;
                        }
                        break;
                }
                run_ready_handlers(t);
        }
        return ms;
}

/* Check if the flow must be postponed. If yes, record the flow in the array
 * of pending flows for the thread. Otherwise, update the deadline for the
 * next run of the handler.
 */
int flow_postpone(struct flow *f)
{
        struct thread *t = f->f_thread;

        if (deadline_expired(f)) {
                /* can serve the flow now, update next deadline */
                f->f_next_event += t->opts->delay;
        } else {
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
        };
        return false;
}

void flow_delete(struct flow *f)
{
        flow_del_or_die(f);
        if (f->f_stat != NULL) {
                /* If we kept stats, forget this flow */
                thread_clear_flow_or_die(f->f_thread, f);
        }

#ifdef WITH_TCPDEVMEM_CUDA
        if (flow_thread(f)->opts->tcpd_gpu_pci_addr) {
                cuda_flow_cleanup(f->f_mbuf);
        } else
#endif /* WITH_TCPDEVMEM_CUDA */
#ifdef WITH_TCPDEVMEM_UDMA
        if (flow_thread(f)->opts->tcpd_nic_pci_addr) {
                struct tcpdevmem_udma_mbuf *t_mbuf = (struct tcpdevmem_udma_mbuf *)f->f_mbuf;

                close(t_mbuf->buf_pages);
                close(t_mbuf->buf);
                close(t_mbuf->memfd);
                close(t_mbuf->devfd);
        }
#endif /* WITH_TCPDEVMEM_UDMA */

/* TODO: need to free the stat struct here for crr tests */
        free(f->f_opaque);
        /* Right now the test is always false, but let's leave it in case
         * we want to implement indipendent per-flow buffers.
         */
        if (f->f_mbuf != f->f_thread->f_mbuf)
                free(f->f_mbuf);
        free(f);
}
