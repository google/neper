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

#include "flow.h"
#include "common.h"
#include "interval.h"
#include "lib.h"
#include "logging.h"
#include "numlist.h"

void epoll_add_or_die(int epollfd, int fd, uint32_t events, struct callbacks *cb)
{
        struct epoll_event ev;
        struct flow *flow;

        flow = calloc(1, sizeof(struct flow));
        flow->fd = fd;
        ev.events = events;
        ev.data.ptr = flow;
        epoll_ctl_or_die(epollfd, EPOLL_CTL_ADD, fd, &ev, cb);
}

#ifndef SO_MAX_PACING_RATE
#define SO_MAX_PACING_RATE 47
#endif

struct flow *addflow(int tid, int epfd, int fd, int flow_id, uint32_t events,
                     struct options *opts, struct callbacks *cb)
{
        struct epoll_event ev;
        struct flow *flow;

        if (opts->debug)
                set_debug(fd, 1, cb);
        if (opts->max_pacing_rate) {
                uint32_t m = opts->max_pacing_rate;
                setsockopt(fd, SOL_SOCKET, SO_MAX_PACING_RATE, &m, sizeof(m));
        }
        set_nonblocking(fd, cb);
        if (opts->reuseaddr)
                set_reuseaddr(fd, 1, cb);
        flow = calloc(1, sizeof(struct flow));
        flow->fd = fd;
        flow->id = flow_id;
        flow->latency = numlist_create(cb);
        ev.events = EPOLLRDHUP | events;
        ev.data.ptr = flow;
        epoll_ctl_or_die(epfd, EPOLL_CTL_ADD, fd, &ev, cb);
        LOG_INFO(cb, "tid=%d, flow_id=%d", tid, flow->id);
        return flow;
}

void delflow(int tid, int epfd, struct flow *flow, struct callbacks *cb)
{
        interval_destroy(flow->itv);
        epoll_del_or_err(epfd, flow->fd, cb);
        do_close(flow->fd);
        LOG_INFO(cb, "tid=%d, flow_id=%d", tid, flow->id);
        free(flow);
}
