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

#include <time.h>

#include "common.h"
#include "flow.h"
#include "loop.h"
#include "socket.h"
#include "thread.h"

static void handler_stop(struct flow *f, uint32_t events)
{
        struct thread *t = flow_thread(f);
        t->stop = 1;
}

/*
 * The main event loop, used by both clients and servers. Calls various init
 * functions and then processes events until the thread is marked as stopped.
 */

void *loop(struct thread *t)
{
        const struct options *opts = t->opts;
        struct epoll_event *events;

        const struct flow_create_args args = {
                .thread  = t,
                .fd      = t->stop_efd,
                .events  = EPOLLIN,
                .opaque  = NULL,
                .handler = handler_stop,
                .mbuf_alloc = NULL,
                .stat    = NULL
        };

        flow_create(&args);

        /* Server sockets must be created in order
         * so that the ebpf filter works.
         * Client sockets don't need to be so but we apply this logic anyway.
         * Wait for its turn to do fn_loop_init() according to t->index.
         */
        pthread_mutex_lock(t->loop_init_m);
        while (*t->loop_inited < t->index)
                pthread_cond_wait(t->loop_init_c, t->loop_init_m);
        t->fn->fn_loop_init(t);
        (*t->loop_inited)++;

        /* Support noburst offsets. This has to happen after fn_loop_init, which
         * creates flows and assigns them a per-thread f_id.
         */
        if (opts->noburst) {
                thread_init_noburst(t);
        }

        pthread_cond_broadcast(t->loop_init_c);
        pthread_mutex_unlock(t->loop_init_m);

        events = calloc_or_die(opts->maxevents, sizeof(*events), t->cb);
        pthread_barrier_wait(t->ready);
        while (!t->stop) {
                /* Serve pending event, compute timeout to next event */
                struct timespec timeout;
                bool indefinite = flow_serve_pending(t, &timeout);
                /* Passing a NULL timeout causes us to block indefinitely. */
                struct timespec *poll_timeout = indefinite ? NULL : &timeout;

                int nfds = t->poll_func(t->epfd, events, opts->maxevents, poll_timeout);
                int i;

                if (nfds == -1) {
                        if (errno == EINTR)
                                continue;
                        PLOG_FATAL(t->cb, "epoll_wait");
                }
                for (i = 0; i < nfds && !t->stop; i++)
                        flow_event(&events[i]);
        }
        thread_flush_stat(t);
        free(events);
        do_close(t->epfd);

        /* TODO: The first flow object is leaking here... */

        /* This is technically a thread callback so it must return a (void *) */
        return NULL;
}
