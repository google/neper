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

#ifndef THIRD_PARTY_NEPER_THREAD_H
#define THIRD_PARTY_NEPER_THREAD_H

#include <pthread.h>
#include <stdint.h>

#include "lib.h"

struct addrinfo;
struct neper_fn;
struct neper_pq;
struct neper_stats;

/*
 * Support for rate limiting flows. The -D option is historically used to add
 * delay between writes in *_stream tests. The way it is implemented there
 * (as of 2017.05.01) is to do a usleep() after each write. However, this slows
 * down all the flows handled by one thread.
 *
 * In tcp_rr we implement the option differently, so that the thread will
 * determine when the next event is due for all the flows it handles, and
 * the delay is implemented as a timeout in epoll_wait.
 * This means that -D indicates the nominal distance between writes,
 * and this is not affected by the number of threads or the RTT of the
 * connection (provided it is lower than the -D argument).
 *
 * The implementation is as follows: each flows stores the time f_next_event
 * when the next event (typically a send()) must be scheduled. When epoll
 * returns ready for the flow, the current time is checked against the
 * deadline in function flow_postpone(). If the deadline is in the past
 * (considering timer granularity) then the deadline is updated and the
 * event handler is run. Otherwise, the flow is stored in an array in
 * the thread descriptor, which is scanned at each iteration of the event
 * loop to check if there are pending events.
 * Checking in the array has a worst case cost O(N), but because the nominal
 * delay is the same for all flows, epoll_wait() has 1ms granularity,
 * and the number of flows per thread is small, the amortized cost is
 * actually O(1) so we do not need more complex data structures to hold
 * pending events.
 *
 * The structure below stores the information for the limited flows.
 */
struct rate_limit {
        /* all timestamps are in ns */
        uint64_t start_time;            /* start of the thread */
        uint64_t now;                   /* last gettimeofday */
        uint64_t next_event;            /* time of next pending event */
        uint64_t delay_count;           /* stats: delayed events */
        uint64_t sleep_count;           /* stats: sleeps */
        uint64_t reschedule_count;      /* stats: rescheduled entries */
        int pending_count;              /* entries in pending_flows */
        struct flow **pending_flows;    /* size is flow_count */

        /* noburst enforces an interval between transactions. Unlike delay,
         * which simply delays the next send, noburst rate limits transactions.
         *
         * The first flow in each thread is offset based on the thread index.
         * Subsequent flows are offset by intervals based on the number of
         * threads. This ensures threads stagger and interleave the first
         * transaction of each flow. For example, 2 threads running 4 flows will
         * run in the order (t0,f0), (t1, f1), (t0, f2), (t1, f3) with the
         * noburst interval between each.
         *
         * When transactions complete, the flow's f_next_event is scheduled
         * based on the per-thread next_noburst_slot. This lets noburst act as a
         * global rate limit, but avoids sharing information between threads.
         */
        uint64_t next_noburst_slot;
};

/* Store per-thread io stats */
struct io_stats {
        uint64_t tx_ops;
        uint64_t tx_bytes;
        uint64_t rx_ops;
        uint64_t rx_bytes;

        /* Counters for RX zerocopy */
        uint64_t rx_zc_bytes;
};

typedef int (*poll_wait)(int epfd, struct epoll_event *events, int maxevents,
                         struct timespec *timeout);

struct thread {
        int index;
        pthread_t id;
        int epfd;                     /* The fd used by this thread for epoll */
        int stop_efd;
        int ai_socktype;              /* supplied by the application */
        const struct neper_fn *fn;    /* supplied by the application */
        struct addrinfo *ai;
        uint64_t transactions;
        const struct options *opts;
        struct callbacks *cb;
        struct addrinfo **local_hosts;
        int num_local_hosts;
        int flow_first;               /* global index of thread's first flow */
        int flow_limit;               /* number of flows to create on thread */
        int flow_count;               /* number of flows created on thread */
        int stop;
        void *f_mbuf;                 /* replaces per-flow buffers */
        pthread_barrier_t *ready;
        pthread_mutex_t *loop_init_m;
        pthread_cond_t *loop_init_c;
        int *loop_inited;
        struct timespec *time_start;
        pthread_mutex_t *time_start_mutex;
        struct rusage *rusage_start;
        struct neper_stats *stats;
        struct neper_rusage *rusage;
        struct io_stats io_stats;
        struct countdown_cond *data_pending;
        struct rate_limit rl;
        struct flow **flows;  /* indexed by flow_id(flow) */
        int flow_space;  /* space allocated in *flows */
        poll_wait poll_func;
        int64_t rounding_ns; /* used to round to millisecond granularity */
        /* The duration between sends on a thread when using noburst. */
        int64_t gap_ns;
};

int thread_stats_events(const struct thread *);
int thread_stats_snaps(const struct thread *);
void thread_init_noburst(struct thread *);

/* Reserves and returns the next available timeslot based on the noburst rate
 * limit. Successive calls return successive time slots.
 */
int64_t thread_next_slot(struct thread *);


struct neper_pq *thread_stats_pq(struct thread *);

int run_main_thread(struct options *, struct callbacks *,
                    const struct neper_fn *);
void thread_time_start(struct thread *, const struct timespec *now);
void thread_store_flow_or_die(struct thread *, struct flow *);
void thread_flush_stat(struct thread *);
void thread_clear_flow_or_die(struct thread*, struct flow *);

#endif
