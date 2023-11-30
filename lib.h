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

#ifndef THIRD_PARTY_NEPER_LIB_H
#define THIRD_PARTY_NEPER_LIB_H

#include <stdbool.h>
#include "countdown_cond.h"
#include "percentiles.h"

struct countdown_cond;

struct callbacks {
        void *logger;

        /* Print in key=value format and keep track of the line number.
         * Not thread-safe. */
        void (*print)(void *logger, const char *key, const char *value, ...)
            __attribute__((format(printf, 3, 4)));

        /* Use for undesired and unexpected events, that the program cannot
         * recover from. Use these whenever an event happens from which you
         * actually want all servers to die and dump a stack trace. */
        void (*log_fatal)(void *logger, const char *file, int line,
                          const char *function, const char *format, ...)
            __attribute__((format(printf, 5, 6)));

        /* Use for undesired and unexpected events that the program can recover
         * from. All ERRORs should be actionable - it should be appropriate to
         * file a bug whenever an ERROR occurs in production. */
        void (*log_error)(void *logger, const char *file, int line,
                          const char *function, const char *format, ...)
            __attribute__((format(printf, 5, 6)));

        /* Use for undesired but relatively expected events, which may indicate
         * a problem. For example, the server received a malformed query. */
        void (*log_warn)(void *logger, const char *file, int line,
                         const char *function, const char *format, ...)
            __attribute__((format(printf, 5, 6)));

        /* Use for state changes or other major events, or to aid debugging. */
        void (*log_info)(void *logger, const char *file, int line,
                         const char *function, const char *format, ...)
            __attribute__((format(printf, 5, 6)));

        /* Notify the logger to log to stderr. */
        void (*logtostderr)(void *logger);
};

struct options {
        int magic;
        int maxevents;
        int num_flows;
        int num_threads;
        int num_clients;
        int num_ports;
        int test_length;
        int buffer_size;
        int listen_backlog;
        int suicide_length;
        int recv_flags;
        int send_flags;
        int mark;
        int tcp_tx_delay;
        bool stime_use_proc; /* Enable use of /proc/stat values for stime */
        bool ipv4;
        bool ipv6;
        bool client;
        bool debug;
        bool dry_run;
        bool pin_cpu;
#ifndef NO_LIBNUMA
        bool pin_numa;
#endif
        bool reuseaddr;
        bool logtostderr;
        bool nolog;
        bool nonblocking;
        bool freebind;
        bool tcp_fastopen;
        bool skip_rx_copy;
        bool zerocopy;
        bool time_wait;
        double interval;
        long long max_pacing_rate;
        const char *local_hosts;
        const char *host;
        const char *control_port;
        const char *port;
        int source_port;
        unsigned long iostat_ms;
        const char *all_samples;
        const char secret[32]; /* includes test name */
        bool async_connect;

        /* tcp_stream */
#if defined(WITH_TCPDEVMEM_CUDA) || defined(WITH_TCPDEVMEM_UDMABUF)
        bool tcpd_validate;
        bool tcpd_rx_cpy;
        const char *tcpd_nic_pci_addr;
        const char *tcpd_gpu_pci_addr;
        unsigned long long tcpd_phys_len;
        const char *tcpd_src_ip;
        const char *tcpd_dst_ip;
        const char *tcpd_link_name;
        int queue_start;
        int queue_num;
#endif /* WITH_TCPDEVMEM_CUDA || WITH_TCPDEVMEM_UDMABUF */
        bool enable_read;
        bool enable_write;
        bool enable_tcp_maerts;
        bool edge_trigger;
        unsigned long delay;  /* ns, also used in tcp_rr */
        const struct rate_conversion *throughput_opt;

        unsigned long long local_rate;  /* updated in report */
        unsigned long long remote_rate; /* updated in final msg */

        /* tcp_rr */
        int request_size;
        int response_size;
        struct percentiles percentiles;

        /* tcp_crr */
        bool nostats;
};

#ifdef __cplusplus
extern "C" {
#endif

int tcp_stream(struct options *, struct callbacks *);
int psp_stream(struct options *, struct callbacks *);
int udp_stream(struct options *, struct callbacks *);
int tcp_rr(struct options *, struct callbacks *);
int udp_rr(struct options *, struct callbacks *);
int psp_rr(struct options *, struct callbacks *);
int tcp_crr(struct options *, struct callbacks *);
int psp_crr(struct options *, struct callbacks *);

#ifdef __cplusplus
}
#endif

#endif
