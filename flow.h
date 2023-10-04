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

#ifndef THIRD_PARTY_NEPER_FLOW_H
#define THIRD_PARTY_NEPER_FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

struct flow;  /* note: struct is defined opaquely within flow.c */
struct neper_stat;
struct thread;

struct tcpdirect_udma_mbuf {
        struct msghdr msg;
        int dmabuf_fd;
        int pages_fd;

        int devfd;
        int memfd;
        int buf;
        int buf_pages;
};

struct tcpdirect_cuda_mbuf {
        int gpu_mem_fd_;
        int dma_buf_fd_;
        void *gpu_tx_mem_;
        void *cpy_buffer;
        size_t bytes_received;
        size_t bytes_sent;
        void *tokens;
        void *vectors;
};

typedef void (*flow_handler)(struct flow *, uint32_t);

/* Simple accessors. */

int                flow_fd(const struct flow *);
int                flow_id(const struct flow *);
void              *flow_mbuf(const struct flow *);
void              *flow_opaque(const struct flow *);
struct neper_stat *flow_stat(const struct flow *);
struct thread     *flow_thread(const struct flow *);

int flow_postpone(struct flow *);
int flow_serve_pending(struct thread *t);  /* process postponed events */
void flow_event(const struct epoll_event *);  /* process one epoll event */
void flow_mod(struct flow *, flow_handler, uint32_t events, bool or_die);
void flow_reconnect(struct flow *, flow_handler, uint32_t events);

struct flow_create_args {
        struct thread *thread;      /* owner of this flow */
        int fd;                     /* the associated fd for epoll */
        uint32_t events;            /* the epoll event mask */
        void *opaque;               /* state opaque to the calling layer */
        flow_handler handler;       /* state machine: initial callback */
        void *(*mbuf_alloc)(struct thread *);  /* allocates message buffer */
        struct neper_stat *(*stat)(struct flow *); /* stats callback */
};

void flow_create(const struct flow_create_args *);
void flow_delete(struct flow *);

#endif
