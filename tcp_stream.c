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

#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include "common.h"
#include "flow.h"
#include "interval.h"
#include "lib.h"
#include "logging.h"
#include "sample.h"
#include "thread.h"

static inline uint32_t epoll_events(struct options *opts)
{
        uint32_t events = 0;
        if (opts->enable_write)
                events |= EPOLLOUT;
        if (opts->enable_read)
                events |= EPOLLIN;
        if (opts->edge_trigger)
                events |= EPOLLET;
        return events;
}

/**
 * The function expects @fd_listen is in a "ready" state in the @epfd
 * epoll set, and directly calls accept() on @fd_listen. The readiness
 * should guarantee that the accept() doesn't block.
 *
 * After a client socket fd is obtained, a new flow is created as part
 * of the thread @t.
 */
static void server_accept(int fd_listen, int epfd, struct thread *t)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        struct sockaddr_storage cli_addr;
        socklen_t cli_len;
        struct flow *flow;
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
                       epoll_events(opts), opts, cb);
        flow->itv = interval_create(opts->interval, t);
}

static void process_events(struct thread *t, int epfd,
                           struct epoll_event *events, int nfds, int fd_listen,
                           char *buf)
{
        struct options *opts = t->opts;
        struct callbacks *cb = t->cb;
        struct timespec ts;
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
                if (opts->enable_read && (events[i].events & EPOLLIN)) {
read_again:
                        num_bytes = read(flow->fd, buf, opts->buffer_size);
                        if (num_bytes == -1) {
                                if (errno != EAGAIN)
                                        PLOG_ERROR(cb, "read");
                                continue;
                        }
                        if (num_bytes == 0) {
                                delflow(t->index, epfd, flow, cb);
                                continue;
                        }
                        flow->bytes_read += num_bytes;
                        flow->transactions++;
                        interval_collect(flow, t);
                        if (opts->edge_trigger)
                                goto read_again;
                }
                if (opts->enable_write && (events[i].events & EPOLLOUT)) {
write_again:
                        num_bytes = write(flow->fd, buf, opts->buffer_size);
                        if (num_bytes == -1) {
                                if (errno != EAGAIN)
                                        PLOG_ERROR(cb, "write");
                                continue;
                        }
                        if (opts->delay) {
                                ts.tv_sec = opts->delay / (1000*1000*1000);
                                ts.tv_nsec = opts->delay % (1000*1000*1000);
                                nanosleep(&ts, NULL);
                        }
                        if (opts->edge_trigger)
                                goto write_again;
                }
        }
}

static void client_connect(int flow_id, int epfd, struct thread *t)
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

        flow = addflow(t->index, epfd, fd, flow_id, epoll_events(opts), opts,
                       cb);
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
        LOG_INFO(cb, "t->stop_efd=%d", t->stop_efd);
        epoll_add_or_die(epfd, t->stop_efd, EPOLLIN, cb);
        for (i = 0; i < flows_in_this_thread; i++)
                client_connect(i, epfd, t);
        events = calloc(opts->maxevents, sizeof(struct epoll_event));
        buf = malloc(opts->buffer_size);
        if (!buf)
                PLOG_FATAL(cb, "malloc");
        if (opts->enable_write)
                fill_random(buf, opts->buffer_size);
        pthread_barrier_wait(t->ready);
        while (!t->stop) {
                int ms = opts->nonblocking ? 10 /* milliseconds */ : -1;
                int nfds = epoll_wait(epfd, events, opts->maxevents, ms);
                if (nfds == -1) {
                        if (errno == EINTR)
                                continue;
                        PLOG_FATAL(cb, "epoll_wait");
                }
                process_events(t, epfd, events, nfds, -1, buf);
        }
        free(buf);
        free(events);
        do_close(epfd);
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
        buf = malloc(opts->buffer_size);
        if (!buf)
                PLOG_FATAL(cb, "malloc");
        if (opts->enable_write)
                fill_random(buf, opts->buffer_size);
        pthread_barrier_wait(t->ready);
        while (!t->stop) {
                int ms = opts->nonblocking ? 10 /* milliseconds */ : -1;
                int nfds = epoll_wait(epfd, events, opts->maxevents, ms);
                if (nfds == -1) {
                        if (errno == EINTR)
                                continue;
                        PLOG_FATAL(cb, "epoll_wait");
                }
                process_events(t, epfd, events, nfds, fd_listen, buf);
        }
        free(buf);
        free(events);
        do_close(epfd);
}

static void *worker_thread(void *arg)
{
        struct thread *t = arg;
        reset_port(t->ai, atoi(t->opts->port), t->cb);
        if (t->opts->client)
                run_client(t);
        else
                run_server(t);
        return NULL;
}

static void report_stats(struct thread *tinfo)
{
        struct timespec *start_time;
        struct sample *p, *samples;
        int num_samples, i, j, tid, flow_id, start_index, end_index;
        ssize_t start_total, current_total, **per_flow;
        double duration, total_bytes, throughput, correlation_coefficient,
               sum_xy = 0, sum_xx = 0, sum_yy = 0;
        struct options *opts = tinfo[0].opts;
        struct callbacks *cb = tinfo[0].cb;

        num_samples = 0;
        for (i = 0; i < opts->num_threads; i++)
                for (p = tinfo[i].samples; p; p = p->next)
                        num_samples++;
        if (num_samples == 0) {
                LOG_WARN(cb, "no sample collected");
                return;
        }
        samples = calloc(num_samples, sizeof(struct sample));
        j = 0;
        for (i = 0; i < opts->num_threads; i++)
                for (p = tinfo[i].samples; p; p = p->next)
                        samples[j++] = *p;
        qsort(samples, num_samples, sizeof(samples[0]), compare_samples);
        if (opts->all_samples)
                print_samples(0, samples, num_samples, opts->all_samples, cb);
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
        start_total = samples[start_index].bytes_read;
        current_total = start_total;
        per_flow = calloc(opts->num_threads, sizeof(ssize_t *));
        for (i = 0; i < opts->num_threads; i++) {
                int max_flow_id = 0;
                for (p = tinfo[i].samples; p; p = p->next) {
                        if (p->flow_id > max_flow_id)
                                max_flow_id = p->flow_id;
                }
                per_flow[i] = calloc(max_flow_id + 1, sizeof(ssize_t));
        }
        tid = samples[start_index].tid;
        flow_id = samples[start_index].flow_id;
        per_flow[tid][flow_id] = start_total;
        for (j = start_index + 1; j <= end_index; j++) {
                tid = samples[j].tid;
                flow_id = samples[j].flow_id;
                current_total -= per_flow[tid][flow_id];
                per_flow[tid][flow_id] = samples[j].bytes_read;
                current_total += per_flow[tid][flow_id];
                duration = seconds_between(start_time, &samples[j].timestamp);
                total_bytes = current_total - start_total;
                sum_xy += duration * total_bytes;
                sum_xx += duration * duration;
                sum_yy += total_bytes * total_bytes;
        }
        throughput = total_bytes / duration;
        correlation_coefficient = sum_xy / sqrt(sum_xx * sum_yy);
        PRINT(cb, "throughput_Mbps", "%.2f", throughput * 8 / 1e6);
        PRINT(cb, "correlation_coefficient", "%.2f", correlation_coefficient);
        for (i = 0; i < opts->num_threads; i++)
                free(per_flow[i]);
        free(per_flow);
        PRINT(cb, "time_end", "%ld.%09ld", samples[num_samples-1].timestamp.tv_sec,
              samples[num_samples-1].timestamp.tv_nsec);
        free(samples);
}

int tcp_stream(struct options *opts, struct callbacks *cb)
{
        if (opts->delay)
                prctl(PR_SET_TIMERSLACK, 1UL);
        return run_main_thread(opts, cb, worker_thread, report_stats);
}
