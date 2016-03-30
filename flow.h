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

#ifndef NEPER_FLOW_H
#define NEPER_FLOW_H

#include <stdint.h>
#include <sys/types.h>

struct callbacks;
struct interval;
struct numlist;
struct options;

struct flow {
        int fd;
        int id;
        ssize_t bytes_read;
        ssize_t bytes_to_read;
        ssize_t bytes_to_write;
        unsigned long transactions;
        struct timespec write_time;
        struct numlist *latency;
        struct interval *itv;
};

void epoll_add_or_die(int epollfd, int fd, uint32_t, struct callbacks *cb);
struct flow *addflow(int tid, int epfd, int fd, int flow_id, uint32_t events,
                     struct options *opts, struct callbacks *cb);
void delflow(int tid, int epfd, struct flow *flow, struct callbacks *cb);

#endif
