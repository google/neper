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

#ifndef NO_LIBNUMA
#include "third_party/libnuma/numa.h"
#include <linux/filter.h>
#include <linux/bpf.h>
#endif

#ifndef SO_MAX_PACING_RATE
#define SO_MAX_PACING_RATE 47
#endif

#ifndef TCP_FASTOPEN_CONNECT
#define TCP_FASTOPEN_CONNECT 30
#endif

/*
 * Set sockopts that has to be set before the socket is stablished and
 * are common to all data sockets.
 */

static void socket_init_not_established(struct thread *t, int s)
{
        const struct options *opts = t->opts;
        struct callbacks *cb = t->cb;

        if (opts->debug)
                set_debug(s, 1, cb);
        if (opts->max_pacing_rate) {
                uint32_t m = opts->max_pacing_rate;
                setsockopt(s, SOL_SOCKET, SO_MAX_PACING_RATE, &m, sizeof(m));
        }
        if (opts->reuseaddr)
                set_reuseaddr(s, 1, cb);
        if (opts->freebind)
                set_freebind(s, cb);
        if (opts->zerocopy)
                set_zerocopy(s, 1, cb);
        if (opts->client) {
                struct linger l;
                l.l_onoff = 1;
                l.l_linger = 0;
                int err = setsockopt(s, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
                if (err)
                        PLOG_ERROR(t->cb, "setsockopt(SO_LINGER)");
        }
}

/*
 * Set sockopts that has to be set after the socket is established and
 * are common to all data sockets.
 */

static void socket_init_established(struct thread *t, int s)
{
        struct callbacks *cb = t->cb;

        set_nonblocking(s, cb);
}


/*
 * The function expects @fd_listen is in a "ready" state in the @epfd
 * epoll set, and directly calls accept() on @fd_listen. The readiness
 * should guarantee that the accept() doesn't block.
 *
 * After a client socket fd is obtained, a new flow is created as part
 * of the thread @t.  The state of the flow is set to "waiting for a
 * request".
 */

static void socket_accept(struct flow *f)
{
        struct thread *t = flow_thread(f);
        int fd_listen = flow_fd(f);
        struct sockaddr_storage cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int s = accept(fd_listen, (struct sockaddr *)&cli_addr, &cli_len);
        if (s == -1) {
                switch (errno) {
                case EINTR:
                case ECONNABORTED:
                        break;

                default:
                        PLOG_ERROR(t->cb, "accept");
                        break;
                }
        } else {
                // TODO(soheil): we can probably remove this line.
                socket_init_not_established(t, s);
                socket_init_established(t, s);
                t->fn->fn_flow_init(t, s);
        }
}

static void handler_accept(struct flow *flow, uint32_t events)
{
        socket_accept(flow);
}

#ifndef NO_LIBNUMA
static void attach_reuseport_ebpf(int fd, int num_sock, struct callbacks *cb)
{
        static char bpf_log_buf[65536];
        static const char bpf_license[] = "";
        int num_numa = numa_num_configured_nodes();
        int sock_per_numa = (num_sock + num_numa -1) / num_numa;
        struct bpf_insn *prog;
        size_t prog_size;
        /*
         * This ebpf randomly picks a socket pinned on the numa_id:
         * if (num_sock <= num_numa) {
         *   prog1:
         *   result = numa_id();
         *   if (result >= num_sock)
         *     rand() % num_sock
         * }
         *
         * else {
         *   prog2:
         *   result = rand() % sock_per_numa * num_numa + numa_id
         *   if (result >= num_sock) {
         *     result -= (rand() % (sock_per_numa - 1) + 1) * num_numa
         *   }
         * }
         */
        struct bpf_insn prog1[] = {
                { BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_numa_node_id },
                { BPF_JMP | BPF_JSGE | BPF_K, BPF_REG_0, 0, 1, num_sock},
                { BPF_JMP | BPF_EXIT, 0, 0, 0, 0 },
                { BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32  },
                { BPF_ALU | BPF_MOD | BPF_K, BPF_REG_0, 0, 0, num_sock},
                { BPF_JMP | BPF_EXIT, 0, 0, 0, 0 },
        };
        struct bpf_insn prog2[] = {
                { BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32  },
                // move prandom_u32 to R8 for later use
                { BPF_ALU | BPF_MOV | BPF_X, BPF_REG_8, BPF_REG_0, 0, 0  },
                // result = rand() % sock_per_numa * num_numa + numa_id
                { BPF_ALU | BPF_MOD | BPF_K, BPF_REG_0, 0, 0, sock_per_numa},
                { BPF_ALU | BPF_MUL | BPF_K, BPF_REG_0, 0, 0, num_numa},
                { BPF_ALU | BPF_MOV | BPF_X, BPF_REG_9, BPF_REG_0, 0, 0  },
                { BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_numa_node_id },
                { BPF_ALU | BPF_ADD | BPF_X, BPF_REG_0, BPF_REG_9, 0, 0  },
                // if (result < num_sock) goto exit
                { BPF_JMP | BPF_JSGE | BPF_K, BPF_REG_0, 0, 1, num_sock},
                { BPF_JMP | BPF_EXIT, 0, 0, 0, 0 },
                // reduce: result -= (rand()%(sock_per_numa-1)+1)*num_numa
                { BPF_ALU | BPF_MOV | BPF_X, BPF_REG_2, BPF_REG_8, 0, 0  },
                { BPF_ALU | BPF_MOD | BPF_K, BPF_REG_2, 0, 0, sock_per_numa - 1},
                { BPF_ALU | BPF_ADD | BPF_K, BPF_REG_2, 0, 0, 1},
                { BPF_ALU | BPF_MUL | BPF_K, BPF_REG_2, 0, 0, num_numa},
                { BPF_ALU | BPF_SUB | BPF_X, BPF_REG_0, BPF_REG_2, 0, 0  },
                { BPF_JMP | BPF_EXIT, 0, 0, 0, 0 },
        };
        union bpf_attr attr;
        int bpf_fd;

        if (num_sock <= num_numa) {
                prog = prog1;
                prog_size = sizeof(prog1) / sizeof(prog1[0]);
        } else {
                prog = prog2;
                prog_size = sizeof(prog2) / sizeof(prog2[0]);
        }
        memset(&attr, 0, sizeof(attr));
        attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
        attr.insn_cnt = prog_size;
        attr.insns = (unsigned long) prog;
        attr.license = (unsigned long) &bpf_license;
        attr.log_buf = (unsigned long) &bpf_log_buf;
        attr.log_size = sizeof(bpf_log_buf);
        attr.log_level = 1;

        bpf_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
        if (bpf_fd < 0)
                PLOG_FATAL(cb, "syscall BPF_PROG_LOAD failed with errno %d, %s\n",
                           errno, bpf_log_buf);

        if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF,
                       &bpf_fd, sizeof(bpf_fd)) == -1)
                PLOG_FATAL(cb,
                           "SO_ATTACH_REUSEPORT_EBPF failed with errno %d\n",
                           errno);

        close(bpf_fd);
}
#endif

static int socket_bind_listener(struct thread *t, struct addrinfo *ai)
{
        int s = socket_or_die(ai->ai_family, ai->ai_socktype, 0, t->cb);
        set_reuseport(s, t->cb);
        set_reuseaddr(s, 1, t->cb);
#ifndef NO_LIBNUMA
        if (t->opts->pin_numa && t->index == 0
            && ai->ai_socktype == SOCK_STREAM)
                attach_reuseport_ebpf(s, t->opts->num_threads, t->cb);
#endif
        bind_or_die(s, ai, t->cb);
        return s;
}

void socket_listen(struct thread *t)
{
        const struct options *opts = t->opts;
        struct callbacks *cb = t->cb;

        struct addrinfo hints = {
                .ai_flags    = AI_PASSIVE,
                .ai_family   = t->ai->ai_family,
                .ai_socktype = t->ai_socktype
        };

        struct addrinfo *ai = getaddrinfo_or_die(opts->host, opts->port, &hints,
                                                 cb);
        int port = atoi(opts->port);
        int i, n, s;

        struct flow_create_args args = {
                .thread  = t,
                .fd      = -1,
                .opaque  = NULL,
                .events  = EPOLLIN,
                .handler = handler_accept,
                .mbuf_alloc = NULL,
                .stat    = NULL
        };

        switch (ai->ai_socktype) {
        case SOCK_STREAM:
                n = opts->num_ports ? opts->num_ports : 1;
                for (i = 0; i < n; i++) {
                        s = socket_bind_listener(t, ai);
                        socket_init_not_established(t, s);
                        listen_or_die(s, t->opts->listen_backlog, cb);
                        args.fd = s;
                        flow_create(&args);
                        reset_port(ai, ++port, cb);
                }
                break;

        case SOCK_DGRAM:
                s = socket_bind_listener(t, ai);
                socket_init_not_established(t, s);
                socket_init_established(t, s);
                t->fn->fn_flow_init(t, s);
                break;
        }

        freeaddrinfo(ai);
}

int socket_connect_one(struct thread *t, int flags)
{
        struct addrinfo *ai = t->ai;

        int s = socket_or_die(ai->ai_family, t->ai_socktype | flags, 0, t->cb);

        if (!t->local_hosts && t->opts->source_port > 0) {
                int flow_idx = (t->flow_first + t->flow_count);
                int port = flow_idx + t->opts->source_port;

                if (ai->ai_family == AF_INET) {
                        struct sockaddr_in source;

                        source.sin_family = AF_INET;
                        source.sin_addr.s_addr = INADDR_ANY;
                        source.sin_port = htons(port);
                        if (bind(s, &source, sizeof(source))) {
                                PLOG_FATAL(t->cb, "bind for source port");
                        }
                } else {
                        struct sockaddr_in6 source;

                        source.sin6_family = AF_INET6;
                        source.sin6_addr = in6addr_any;
                        source.sin6_port = htons(port);
                        if (bind(s, &source, sizeof(source))) {
                                PLOG_FATAL(t->cb, "bind for source port");
                        }
                }
        }

        /* If the server has multiple listen ports then use them round-robin. */
        int n = t->opts->num_ports ? t->opts->num_ports : 1;
        int i = (t->flow_first + t->flow_count) % n;
        int port = atoi(t->opts->port) + i;
        reset_port(ai, port, t->cb);

        if (t->opts->tcp_fastopen && t->ai_socktype == SOCK_STREAM) {
                int enable = 1;
                setsockopt(s, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, &enable,
                           sizeof(enable));
        }

        socket_init_not_established(t, s);
        if (t->local_hosts) {
                int i = (t->flow_first + t->flow_count) % t->num_local_hosts;
                bind_or_die(s, t->local_hosts[i], t->cb);
        }
        connect_or_die(s, ai, t->cb);
        socket_init_established(t, s);
        return s;
}

void socket_connect_all(struct thread *t)
{
        int i, flags = t->opts->async_connect ? SOCK_NONBLOCK : 0;
        for (i = 0; i < t->flow_limit; i++) {
                int s = socket_connect_one(t, flags);
                t->fn->fn_flow_init(t, s);
        }
}
