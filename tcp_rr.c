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

#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "common.h"
#include "flow.h"
#include "interval.h"
#include "lib.h"
#include "numlist.h"
#include "percentiles.h"
#include "sample.h"
#include "thread.h"

static inline void track_write_time(struct options *opts, struct flow *flow)
{
        if (flow->bytes_to_write == opts->request_size)
                clock_gettime(CLOCK_MONOTONIC, &flow->write_time);
}

static inline void track_finish_time(struct flow *flow)
{
        struct timespec finish_time;

        clock_gettime(CLOCK_MONOTONIC, &finish_time);
        numlist_add(flow->latency, seconds_between(&flow->write_time,
                                                   &finish_time));
}

static void client_events(struct thread *t, int epfd,
                          struct epoll_event *events, int nfds, char *buf)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        struct flow *flow;
        ssize_t num_bytes;
        int i;

        for (i = 0; i < nfds; i++) {
                flow = events[i].data.ptr;
                if (flow->fd == t->stop_efd) {
                        t->stop = 1;
                        break;
                }
                if (events[i].events & EPOLLRDHUP) {
                        delflow(t->index, epfd, flow, cb);
                        continue;
                }
                if (events[i].events & EPOLLOUT) {
                        ssize_t to_write = flow->bytes_to_write;
                        int flags = 0;

                        if (to_write > opts->buffer_size) {
                                to_write = opts->buffer_size;
                                flags |= MSG_MORE;
                        }
                        track_write_time(opts, flow);
                        num_bytes = send(flow->fd, buf, to_write, flags);
                        if (num_bytes == -1) {
                                PLOG_ERROR(cb, "write");
                                continue;
                        }
                        flow->bytes_to_write -= num_bytes;
                        if (flow->bytes_to_write > 0)
                                continue;
                        /* Successfully sent request, now wait for response */
                        events[i].events = EPOLLRDHUP | EPOLLIN;
                        epoll_ctl_or_die(epfd, EPOLL_CTL_MOD, flow->fd,
                                         &events[i], cb);
                        flow->bytes_to_read = opts->response_size;
                } else if (events[i].events & EPOLLIN) {
                        ssize_t to_read = flow->bytes_to_read;

                        if (to_read > opts->buffer_size)
                                to_read = opts->buffer_size;
                        num_bytes = read(flow->fd, buf, to_read);
                        if (num_bytes == -1) {
                                PLOG_ERROR(cb, "read");
                                continue;
                        }
                        if (num_bytes == 0) {
                                delflow(t->index, epfd, flow, cb);
                                continue;
                        }
                        flow->bytes_read += num_bytes;
                        flow->bytes_to_read -= num_bytes;
                        if (flow->bytes_to_read > 0)
                                continue;
                        t->transactions++;
                        flow->transactions++;
                        track_finish_time(flow);
                        interval_collect(flow, t);
                        /* Successfully read resp., now wait to send request */
                        events[i].events = EPOLLRDHUP | EPOLLOUT;
                        epoll_ctl_or_die(epfd, EPOLL_CTL_MOD, flow->fd,
                                         &events[i], cb);
                        flow->bytes_to_write = opts->request_size;
                }
        }
}

static void *buf_alloc(struct options *opts)
{
        size_t alloc_size = opts->request_size;

        if (alloc_size < opts->response_size)
                alloc_size = opts->response_size;
        if (alloc_size > opts->buffer_size)
                alloc_size = opts->buffer_size;
        return calloc(alloc_size, sizeof(char));
}

static void client_connect(int i, int epfd, struct thread *t)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        struct addrinfo *ai = t->ai;
        struct flow *flow;
        int fd;

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == -1)
                PLOG_FATAL(cb, "socket");
        if (opts->min_rto)
                set_min_rto(fd, opts->min_rto, cb);
        if (opts->debug)
                set_debug(fd, 1, cb);
        if (opts->local_host)
                set_local_host(fd, opts, cb);
        if (do_connect(fd, ai->ai_addr, ai->ai_addrlen))
                PLOG_FATAL(cb, "do_connect");

        flow = addflow(t->index, epfd, fd, i, EPOLLOUT, opts, cb);
        flow->bytes_to_write = opts->request_size;
        flow->itv = interval_create(opts->interval, t);
}

/**
 * The function expects @fd_listen is in a "ready" state in the @epfd
 * epoll set, and directly calls accept() on @fd_listen. The readiness
 * should guarantee that the accept() doesn't block.
 *
 * After a client socket fd is obtained, a new flow is created as part
 * of the thread @t.  The state of the flow is set to "waiting for a
 * request".
 */
static void server_accept(int fd_listen, int epfd, struct thread *t)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        struct sockaddr_storage cli_addr;
        struct flow *flow;
        socklen_t cli_len;
        int client;

        cli_len = sizeof(cli_addr);
        client = accept(fd_listen, (struct sockaddr *)&cli_addr, &cli_len);
        if (client == -1) {
                if (errno == EINTR || errno == ECONNABORTED)
                        return;
                PLOG_ERROR(cb, "accept");
                return;
        }
        flow = addflow(t->index, epfd, client, t->next_flow_id++,
                       EPOLLIN, opts, cb);
        flow->bytes_to_read = opts->request_size;
        flow->itv = interval_create(opts->interval, t);
}

static void run_client(struct thread *t)
{
        struct options *opts = t->opts;
        const int flows_in_this_thread = flows_in_thread(opts->num_flows,
                                                         opts->num_threads,
                                                         t->index);
        struct callbacks *cb = t->cb;
        struct epoll_event *events;
        int epfd, i;
        char *buf;

        LOG_INFO(cb, "flows_in_this_thread=%d", flows_in_this_thread);
        epfd = epoll_create1(0);
        if (epfd == -1)
                PLOG_FATAL(cb, "epoll_create1");
        epoll_add_or_die(epfd, t->stop_efd, EPOLLIN, cb);
        for (i = 0; i < flows_in_this_thread; i++)
                client_connect(i, epfd, t);
        events = calloc(opts->maxevents, sizeof(struct epoll_event));
        buf = buf_alloc(opts);
        pthread_barrier_wait(t->ready);
        while (!t->stop) {
                int ms = opts->nonblocking ? 10 /* milliseconds */ : -1;
                int nfds = epoll_wait(epfd, events, opts->maxevents, ms);
                if (nfds == -1) {
                        if (errno == EINTR)
                                continue;
                        PLOG_FATAL(cb, "epoll_wait");
                }
                client_events(t, epfd, events, nfds, buf);
        }
        free(buf);
        free(events);
        do_close(epfd);
}

static void server_events(struct thread *t, int epfd,
                          struct epoll_event *events, int nfds, int fd_listen,
                          char *buf)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        ssize_t num_bytes;
        int i;

        for (i = 0; i < nfds; i++) {
                struct flow *flow = events[i].data.ptr;
                if (flow->fd == t->stop_efd) {
                        t->stop = 1;
                        break;
                }
                if (flow->fd == fd_listen) {
                        server_accept(fd_listen, epfd, t);
                        continue;
                }
                if (events[i].events & EPOLLRDHUP) {
                        delflow(t->index, epfd, flow, cb);
                        continue;
                }
                if (events[i].events & EPOLLIN) {
                        ssize_t to_read = flow->bytes_to_read;

                        if (to_read > opts->buffer_size)
                                to_read = opts->buffer_size;
                        num_bytes = read(flow->fd, buf, to_read);
                        if (num_bytes == -1) {
                                PLOG_ERROR(cb, "read");
                                continue;
                        }
                        if (num_bytes == 0) {
                                delflow(t->index, epfd, flow, cb);
                                continue;
                        }
                        flow->bytes_read += num_bytes;
                        flow->bytes_to_read -= num_bytes;
                        if (flow->bytes_to_read > 0)
                                continue;
                        /* Successfully read request, now send a response */
                        events[i].events = EPOLLRDHUP | EPOLLOUT;
                        if (epoll_ctl(epfd, EPOLL_CTL_MOD, flow->fd,
                                      &events[i])) {
                                /* not necessarily fatal, just drop */
                                delflow(t->index, epfd, flow, cb);
                                continue;
                        }
                        flow->bytes_to_write = opts->response_size;
                } else if (events[i].events & EPOLLOUT) {
                        ssize_t to_write = flow->bytes_to_write;
                        int flags = 0;

                        if (to_write > opts->buffer_size) {
                                to_write = opts->buffer_size;
                                flags |= MSG_MORE;
                        }
                        num_bytes = send(flow->fd, buf, to_write, flags);
                        if (num_bytes == -1) {
                                PLOG_ERROR(cb, "write");
                                continue;
                        }
                        flow->bytes_to_write -= num_bytes;
                        if (flow->bytes_to_write > 0)
                                continue;
                        t->transactions++;
                        flow->transactions++;
                        interval_collect(flow, t);
                        /* Successfully write response, now read a request */
                        events[i].events = EPOLLRDHUP | EPOLLIN;
                        if (epoll_ctl(epfd, EPOLL_CTL_MOD, flow->fd,
                                      &events[i])) {
                                /* not necessarily fatal, just drop */
                                delflow(t->index, epfd, flow, cb);
                                continue;
                        }
                        flow->bytes_to_read = opts->request_size;
                }
        }
}

static void run_server(struct thread *t)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        struct addrinfo *ai = t->ai;
        struct epoll_event *events;
        int fd_listen, epfd;
        char *buf;

        fd_listen = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd_listen == -1)
                PLOG_FATAL(cb, "socket");
        set_reuseport(fd_listen, cb);
        set_reuseaddr(fd_listen, 1, cb);
        if (bind(fd_listen, ai->ai_addr, ai->ai_addrlen))
                PLOG_FATAL(cb, "bind");
        if (opts->min_rto)
                set_min_rto(fd_listen, opts->min_rto, cb);
        if (listen(fd_listen, opts->listen_backlog))
                PLOG_FATAL(cb, "listen");
        epfd = epoll_create1(0);
        if (epfd == -1)
                PLOG_FATAL(cb, "epoll_create1");
        epoll_add_or_die(epfd, t->stop_efd, EPOLLIN, cb);
        epoll_add_or_die(epfd, fd_listen, EPOLLIN, cb);
        events = calloc(opts->maxevents, sizeof(struct epoll_event));
        buf = buf_alloc(opts);
        pthread_barrier_wait(t->ready);
        while (!t->stop) {
                int ms = opts->nonblocking ? 10 /* milliseconds */ : -1;
                int nfds = epoll_wait(epfd, events, opts->maxevents, ms);
                if (nfds == -1) {
                        if (errno == EINTR)
                                continue;
                        PLOG_FATAL(cb, "epoll_wait");
                }
                server_events(t, epfd, events, nfds, fd_listen, buf);
        }
        free(buf);
        free(events);
        do_close(epfd);
}

static void *thread_start(void *arg)
{
        struct thread *t = arg;
        reset_port(t->ai, atoi(t->opts->port), t->cb);
        if (t->opts->client)
                run_client(t);
        else
                run_server(t);
        return NULL;
}

static void report_latency(struct sample *samples, int start, int end,
                           struct options *opts, struct callbacks *cb)
{
        struct numlist *all = samples[start].latency;
        int i;

        if (!opts->client)
                return;

        for (i = start + 1; i <= end; i++)
                numlist_concat(all, samples[i].latency);

        PRINT(cb, "latency_min", "%f", numlist_min(all));
        PRINT(cb, "latency_max", "%f", numlist_max(all));
        PRINT(cb, "latency_mean", "%f", numlist_mean(all));
        PRINT(cb, "latency_stddev", "%f", numlist_stddev(all));

        for (i = 0; i <= 100; i++) {
                if (opts->percentiles.chosen[i]) {
                        char key[13];
                        sprintf(key, "latency_p%d", i);
                        PRINT(cb, key, "%f", numlist_percentile(all, i));
                }
        }
}

static void report_stats(struct thread *tinfo)
{
        struct sample *p, *samples;
        struct timespec *start_time;
        int num_samples, i, j, tid, flow_id, start_index, end_index;
        unsigned long start_total, current_total, **per_flow;
        double duration, total_work, throughput, correlation_coefficient,
               sum_xy = 0, sum_xx = 0, sum_yy = 0;
        struct options *opts = tinfo[0].opts;
        struct callbacks *cb = tinfo[0].cb;

        num_samples = 0;
        current_total = 0;
        for (i = 0; i < opts->num_threads; i++) {
                for (p = tinfo[i].samples; p; p = p->next)
                        num_samples++;
                current_total += tinfo[i].transactions;
        }
        PRINT(cb, "num_transactions", "%lu", current_total);
        if (num_samples == 0) {
                LOG_WARN(cb, "no sample collected");
                return;
        }
        samples = calloc(num_samples, sizeof(samples[0]));
        if (!samples)
                LOG_FATAL(cb, "calloc samples");
        j = 0;
        for (i = 0; i < opts->num_threads; i++)
                for (p = tinfo[i].samples; p; p = p->next)
                        samples[j++] = *p;
        qsort(samples, num_samples, sizeof(samples[0]), compare_samples);
        if (opts->all_samples) {
                print_samples(&opts->percentiles, samples, num_samples,
                              opts->all_samples, cb);
        }
        start_index = 0;
        end_index = num_samples - 1;
        PRINT(cb, "start_index", "%d", start_index);
        PRINT(cb, "end_index", "%d", end_index);
        PRINT(cb, "num_samples", "%d", num_samples);
        if (start_index >= end_index) {
                LOG_WARN(cb, "insufficient number of samples");
                return;
        }
        start_time = &samples[start_index].timestamp;
        start_total = samples[start_index].transactions;
        current_total = start_total;
        per_flow = calloc(opts->num_threads, sizeof(unsigned long *));
        if (!per_flow)
                LOG_FATAL(cb, "calloc per_flow");
        for (i = 0; i < opts->num_threads; i++) {
                int max_flow_id = 0;
                for (p = tinfo[i].samples; p; p = p->next) {
                        if (p->flow_id > max_flow_id)
                                max_flow_id = p->flow_id;
                }
                per_flow[i] = calloc(max_flow_id + 1, sizeof(unsigned long));
                if (!per_flow[i])
                        LOG_FATAL(cb, "calloc per_flow[%d]", i);
        }
        tid = samples[start_index].tid;
        assert(tid >= 0 && tid < opts->num_threads);
        flow_id = samples[start_index].flow_id;
        per_flow[tid][flow_id] = start_total;
        for (j = start_index + 1; j <= end_index; j++) {
                tid = samples[j].tid;
                assert(tid >= 0 && tid < opts->num_threads);
                flow_id = samples[j].flow_id;
                current_total -= per_flow[tid][flow_id];
                per_flow[tid][flow_id] = samples[j].transactions;
                current_total += per_flow[tid][flow_id];
                duration = seconds_between(start_time, &samples[j].timestamp);
                total_work = current_total - start_total;
                sum_xy += duration * total_work;
                sum_xx += duration * duration;
                sum_yy += total_work * total_work;
        }
        throughput = total_work / duration;
        correlation_coefficient = sum_xy / sqrt(sum_xx * sum_yy);
        PRINT(cb, "throughput", "%.2f", throughput);
        PRINT(cb, "correlation_coefficient", "%.2f", correlation_coefficient);
        for (i = 0; i < opts->num_threads; i++)
                free(per_flow[i]);
        free(per_flow);
        PRINT(cb, "time_end", "%ld.%09ld", samples[num_samples-1].timestamp.tv_sec,
              samples[num_samples-1].timestamp.tv_nsec);
        report_latency(samples, start_index, end_index, opts, cb);
        free(samples);
}

int tcp_rr(struct options *opts, struct callbacks *cb)
{
        return run_main_thread(opts, cb, thread_start, report_stats);
}
