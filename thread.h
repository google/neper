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

#ifndef NEPER_THREAD_H
#define NEPER_THREAD_H

#include <pthread.h>
#include <stdbool.h>
#include "lib.h"

struct sample;

struct thread {
        int index;
        pthread_t id;
        int stop_efd;
        struct addrinfo *ai;
        struct sample *samples;
        unsigned long transactions;
        struct options *opts;
        struct callbacks *cb;
        int next_flow_id;
        int stop;
        pthread_barrier_t *ready;
        struct timespec *time_start;
        pthread_mutex_t *time_start_mutex;
        struct rusage *rusage_start;
};

int run_main_thread(struct options *opts, struct callbacks *cb,
                    void *(*thread_func)(void *),
                    void (*report_stats)(struct thread *));

#endif
