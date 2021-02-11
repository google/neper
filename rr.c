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

/*
 * This file implements six different state machines:
 * TCP_RR client/server, UDP_RR client/server, and TCP_CRR client/server
 *
 * Note that there is a high degree of overlap, for example the TCP_RR server
 * TCP_CRR server state machines are identical. The state machines are broken
 * down into small callback functions which each perform some small task, such
 * as sending a message, opening a socket connection, or gathering statistics.
 */

#include "coef.h"
#include "common.h"
#include "countdown_cond.h"
#include "flow.h"
#include "histo.h"
#include "percentiles.h"
#include "print.h"
#include "rr.h"
#include "snaps.h"
#include "socket.h"
#include "stats.h"
#include "thread.h"

#define NEPER_EPOLL_MASK (EPOLLHUP | EPOLLRDHUP | EPOLLERR)

typedef ssize_t (*rr_send_t)(struct flow *, const char *, size_t, int);
typedef ssize_t (*rr_recv_t)(struct flow *, char *, size_t);

struct rr_state {
        rr_send_t rr_send;
        rr_recv_t rr_recv;

        ssize_t rr_xfer;  /* # of bytes remaining in the current send or recv */

        struct timespec rr_ts_0;  /* timestamp just after previous recv() */
        struct timespec rr_ts_1;  /* timestamp just before current send() */
        struct timespec rr_ts_2;  /* timestamp just after current recv() */

        struct sockaddr_storage rr_peer;  /* for UDP servers */
        socklen_t rr_peerlen;
};

struct rr_snap_opaque {
        double min;
        double max;
        double mean;
        double stddev;
        double percentile[0];
};

static void rr_server_state_0(struct flow *, uint32_t);
static void rr_client_state_0(struct flow *, uint32_t);
static void crr_client_state_0(struct flow *, uint32_t);

/*
 * Some protocol-specific send/recv wrappers for the state machine.
 * UDP server sockets receive traffic from many different clients and
 * therefore need to use recvfrom() and sendto().
 */

static ssize_t rr_fn_send(struct flow *f, const char *buf, size_t len,
                          int flags)
{
        return send(flow_fd(f), buf, len, flags);
}

static ssize_t rr_fn_sendto(struct flow *f, const char *buf, size_t len,
                            int flags)
{
        const struct rr_state *rr = flow_opaque(f);
        return sendto(flow_fd(f), buf, len, flags, (void *)&rr->rr_peer,
                      rr->rr_peerlen);
}

static ssize_t rr_fn_recv(struct flow *f, char *buf, size_t len)
{
        return recv(flow_fd(f), buf, len, 0);
}

static ssize_t rr_fn_recvfrom(struct flow *f, char *buf, size_t len)
{
        struct rr_state *rr = flow_opaque(f);
        rr->rr_peerlen = sizeof(struct sockaddr_storage);
        return recvfrom(flow_fd(f), buf, len, 0, (void *)&rr->rr_peer,
                        &rr->rr_peerlen);
}

/* Allocate a message buffer for a rr flow. */

static void *rr_alloc(struct thread *t)
{
        const struct options *opts = t->opts;
        size_t len;

        if (t->f_mbuf)
                return t->f_mbuf;
        len = MAX(opts->request_size, opts->response_size);
        len = MIN(len, opts->buffer_size);

        t->f_mbuf = calloc_or_die(len, sizeof(char), t->cb);
        return t->f_mbuf;
}

static struct neper_stat *rr_latency_init(struct flow *f)
{
        const struct thread *t = flow_thread(f);
        int size;

        struct neper_histo *histo = t->histo_factory->create(t->histo_factory);

        size = sizeof(struct rr_snap_opaque) + t->percentiles * sizeof(double);

        return neper_stat_init(f, histo, size);
}

static ssize_t rr_send_size(struct thread *t) {
        if (t->opts->client) {
                return t->opts->request_size;
        }
        return t->opts->response_size;
}

static ssize_t rr_recv_size(struct thread *t) {
        if (t->opts->client) {
                return t->opts->response_size;
        }
        return t->opts->request_size;
}

static void rr_state_init(struct thread *t, int fd,
                          void (*state)(struct flow *, uint32_t),
                          uint32_t event)
{
        struct rr_state *rr = calloc_or_die(1, sizeof(struct rr_state), t->cb);

        // Request is always first.
        rr->rr_xfer = t->opts->request_size;

        common_gettime(&rr->rr_ts_2);  /* This initializes TCP_CRR stats. */

        switch (t->fn->fn_type) {
        case SOCK_STREAM:
                rr->rr_send = rr_fn_send;
                rr->rr_recv = rr_fn_recv;
                break;
        case SOCK_DGRAM:
                rr->rr_send = rr_fn_sendto;
                rr->rr_recv = rr_fn_recvfrom;
                break;
        }

        const struct flow_create_args args = {
                .thread  = t,
                .fd      = fd,
                .events  = event,
                .opaque  = rr,
                .handler = state,
                .mbuf_alloc = rr_alloc,
                .stat    = rr_latency_init
        };

        flow_create(&args);
}

/*
 * Return values of true for rr_do_send() and rr_do_recv() mean the transfer was
 * successfully completed and the state machine may therefore advance.
 */

static bool rr_do_send(struct flow *f, uint32_t events, rr_send_t rr_send)
{
        struct thread *t = flow_thread(f);
        const struct options *opts = t->opts;
        struct rr_state *rr = flow_opaque(f);

        if (events & ~(NEPER_EPOLL_MASK | EPOLLOUT))
                LOG_ERROR(t->cb, "%s(): unknown event(s) %x", __func__, events);

        if (events & NEPER_EPOLL_MASK) {
                flow_delete(f);
                return false;
        }

        ssize_t len = rr->rr_xfer;
        if ((len == opts->request_size) && opts->client)
                common_gettime(&rr->rr_ts_1);

        int flags = 0;
        if (len > opts->buffer_size) {
                len = opts->buffer_size;
                flags |= MSG_MORE;
        }

        ssize_t n = rr_send(f, flow_mbuf(f), len, flags);
        if (n == -1) {
                PLOG_ERROR(t->cb, "send");
                return false;
        }

        rr->rr_xfer -= n;
        if (rr->rr_xfer)
                return false;

        // Transition to receiving.
        rr->rr_xfer = rr_recv_size(t);
        return true;
}

static bool rr_do_recv(struct flow *f, uint32_t events, rr_recv_t rr_recv)
{
        struct thread *t = flow_thread(f);
        const struct options *opts = t->opts;
        struct rr_state *rr = flow_opaque(f);

        if (events & ~(NEPER_EPOLL_MASK | EPOLLIN))
                LOG_ERROR(t->cb, "%s(): unknown event(s) %x", __func__, events);

        if (events & NEPER_EPOLL_MASK) {
                flow_delete(f);
                return false;
        }

        ssize_t len = rr->rr_xfer;

        if (len > opts->buffer_size)
                len = opts->buffer_size;

        ssize_t n;
        do {
                n = rr_recv(f, flow_mbuf(f), len);
        } while(n == -1 && errno == EINTR);

        if (n == -1) {
                PLOG_ERROR(t->cb, "read");
                return false;
        }
        if (n == 0) {
                flow_delete(f);
                return false;
        }

        rr->rr_xfer -= n;
        if (rr->rr_xfer)
                return false;

        if (opts->client) {
                rr->rr_ts_0 = rr->rr_ts_2;
                common_gettime(&rr->rr_ts_2);
        }

        t->transactions++;

        // Transition to sending.
        rr->rr_xfer = rr_send_size(t);
        return true;
}

static void rr_snapshot(struct thread *t, struct neper_stat *stat,
                        struct neper_snap *snap)
{
        struct neper_histo *histo = stat->histo(stat);

        histo->epoch(histo);

        struct rr_snap_opaque *opaque = (void *)&snap->opaque;

        opaque->min = histo->min(histo);
        opaque->max = histo->max(histo);
        opaque->mean = histo->mean(histo);
        opaque->stddev = histo->stddev(histo);

        if (t->percentiles) {
                int i, j = 0;
                for (i = 0; i < PER_INDEX_COUNT; i++)
                        if (percentiles_chosen(&t->opts->percentiles, i))
                                opaque->percentile[j++] =
                                        histo->percent(histo, i);
        }
}

static bool rr_do_compl(struct flow *f,
                        const struct timespec *then,
                        const struct timespec *now)
{
        double elapsed = seconds_between(then, now);
        struct thread *t = flow_thread(f);
        bool last = false;

        struct neper_stat *stat = flow_stat(f);
        struct neper_histo *histo = stat->histo(stat);
        histo->event(histo, elapsed);

        if (t->data_pending) {
                /* data vs time mode, last rr? */
                if (!countdown_cond_commit(t->data_pending)) {
                        LOG_INFO(t->cb, "last transaction received");
                        last = true;
                }
        }

        stat->event(t, stat, 1, last, rr_snapshot);

        return last;
}

/* The state machine for RR clients: */

static void rr_client_state_1(struct flow *f, uint32_t events)
{
        struct thread *t = flow_thread(f);

        if (rr_do_recv(f, events, rr_fn_recv)) {
                struct rr_state *rr = flow_opaque(f);

                if (rr_do_compl(f, &rr->rr_ts_1, &rr->rr_ts_2))
                        return;

                if (!t->opts->delay && rr_do_send(f, EPOLLOUT, rr_fn_send))
                        return;

                flow_mod(f, rr_client_state_0, EPOLLOUT, true);
        }
}

static void rr_client_state_0(struct flow *f, uint32_t events)
{
        struct thread *t = flow_thread(f);

        if (t->data_pending && countdown_cond_dec(t->data_pending) < 0) {
                /* data vs time mode and no more transactons to send */
                return;
        }
        if (t->opts->delay && flow_postpone(f))
                return;
        if (rr_do_send(f, events, rr_fn_send))
                flow_mod(f, rr_client_state_1, EPOLLIN, true);
}

/* The state machine for CRR clients: */

static void crr_client_state_1(struct flow *f, uint32_t events)
{
        if (rr_do_recv(f, events, rr_fn_recv)) {
                struct rr_state *rr = flow_opaque(f);

                if (rr_do_compl(f, &rr->rr_ts_0, &rr->rr_ts_2))
                        return;
                flow_reconnect(f, crr_client_state_0, EPOLLOUT);
        }
}

static void crr_client_state_0(struct flow *f, uint32_t events)
{
        struct thread *t = flow_thread(f);

        if (t->data_pending && countdown_cond_dec(t->data_pending) < 0) {
                /* data vs time mode and no more transactons to send */
                return;
        }
        if (rr_do_send(f, events, rr_fn_send))
                flow_mod(f, crr_client_state_1, EPOLLIN, true);
}

/* The state machine for servers: */

static void rr_server_state_2(struct flow *f, uint32_t events)
{
        struct rr_state *rr = flow_opaque(f);
        struct thread *t = flow_thread(f);
        struct neper_stat *stat = flow_stat(f);
        struct neper_histo *histo = stat->histo(stat);

        if (rr_do_send(f, events, rr->rr_send)) {
                /* rr server has no meaningful latency to measure. */
                histo->event(histo, 0.0);
                stat->event(t, stat, 1, false, rr_snapshot);
                flow_mod(f, rr_server_state_0, EPOLLIN, false);
        }
}

static void rr_server_state_1(struct flow *f)
{
        flow_mod(f, rr_server_state_2, EPOLLOUT, false);
}

static void rr_server_state_0(struct flow *f, uint32_t events)
{
        struct rr_state *rr = flow_opaque(f);

        if (rr_do_recv(f, events, rr->rr_recv))
                rr_server_state_1(f);
}

/* These functions point the state machines at their first handler functions. */

void crr_flow_init(struct thread *t, int fd)
{
        void (*state)(struct flow *, uint32_t);
        uint32_t event;

        if (t->opts->client) {
                state = crr_client_state_0;
                event = EPOLLOUT;
        } else {
                state = rr_server_state_0;  /* crr & rr servers are identical */
                event = EPOLLIN;
        }

        rr_state_init(t, fd, state, event);
}

void rr_flow_init(struct thread *t, int fd)
{
        void (*state)(struct flow *, uint32_t);
        uint32_t event;

        if (t->opts->client) {
                state = rr_client_state_0;
                event = EPOLLOUT;
        } else {
                state = rr_server_state_0;
                event = EPOLLIN;
        }

        rr_state_init(t, fd, state, event);
}

/*
 * Statistics. Ignore everything below this line, which (a) has not been
 * changed and (b) is about to be completely replaced.
 */

static void rr_print_snap(struct thread *t, int flow_index, 
                          const struct neper_snap *snap, FILE *csv)
{

        if (snap && csv) {
                const struct rr_snap_opaque *rso = (void *)&snap->opaque;

                fprintf(csv, ",%.9f,%.9f,%.9f,%.9f",
                        rso->min, rso->mean, rso->max, rso->stddev);

                if (t->percentiles) {
                        const struct options *opts = t->opts;
                        int i, j = 0;

                        for (i = 0; i < PER_INDEX_COUNT; i++)
                                if (percentiles_chosen(&opts->percentiles, i))
                                        fprintf(csv, ",%.9f",
                                                rso->percentile[j++]);
                }

                fprintf(csv, "\n");
        }
}

static int
fn_add(struct neper_stat *stat, void *ptr)
{
        struct neper_histo *src = stat->histo(stat);
        struct neper_histo *des = ptr;
        des->add(des, src);
        return 0;
}

int rr_report_stats(struct thread *tinfo)
{
        const struct options *opts = tinfo[0].opts;
        const char *path = opts->all_samples;
        struct callbacks *cb = tinfo[0].cb;
        FILE *csv = NULL;
        int i;

        int num_events = thread_stats_events(tinfo);
        PRINT(cb, "num_transactions", "%d", num_events);

        struct neper_histo *sum =
                tinfo[0].histo_factory->create(tinfo[0].histo_factory);
        for (i = 0; i < opts->num_threads; i++)
                tinfo[i].stats->sumforeach(tinfo[i].stats, fn_add, sum);
        sum->epoch(sum);
        sum->print(sum);
        sum->fini(sum);

        if (path) {
                csv = print_header(path, "transactions", "", cb);
                print_latency_header(csv, &opts->percentiles);
        }

        struct neper_coef *coef = neper_stat_print(tinfo, csv, rr_print_snap);
        if (coef) {
                double thru = coef->thruput(coef);
                struct options *w_opts = (struct options *)opts;

                w_opts->local_rate = thru; /* bits/s */

                PRINT(cb, "throughput", "%.2f", thru);

                coef->fini(coef);
        } else {
                LOG_ERROR(cb, "%s: not able to find coef", __func__);
                return -1;
        }

        if (csv)
                fclose(csv);

        return 0;
}
