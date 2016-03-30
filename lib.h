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

#ifndef NEPER_LIB_H
#define NEPER_LIB_H

#include <stdbool.h>
#include "percentiles.h"

struct callbacks {
        void *logger;

        /* Print in key=value format and keep track of the line number.
         * Not thread-safe. */
        void (*print)(void *logger, const char *key, const char *value, ...);

        /* Use for undesired and unexpected events, that the program cannot
         * recover from. Use these whenever an event happens from which you
         * actually want all servers to die and dump a stack trace. */
        void (*log_fatal)(void *logger, const char *file, int line,
                          const char *function, const char *format, ...);

        /* Use for undesired and unexpected events that the program can recover
         * from. All ERRORs should be actionable - it should be appropriate to
         * file a bug whenever an ERROR occurs in production. */
        void (*log_error)(void *logger, const char *file, int line,
                          const char *function, const char *format, ...);

        /* Use for undesired but relatively expected events, which may indicate
         * a problem. For example, the server received a malformed query. */
        void (*log_warn)(void *logger, const char *file, int line,
                         const char *function, const char *format, ...);

        /* Use for state changes or other major events, or to aid debugging. */
        void (*log_info)(void *logger, const char *file, int line,
                         const char *function, const char *format, ...);

        /* Notify the logger to log to stderr. */
        void (*logtostderr)(void *logger);
};

struct options {
        int magic;
        int min_rto;
        int maxevents;
        int num_flows;
        int num_threads;
        int test_length;
        int buffer_size;
        int listen_backlog;
        int suicide_length;
        bool ipv4;
        bool ipv6;
        bool client;
        bool debug;
        bool dry_run;
        bool pin_cpu;
        bool reuseaddr;
        bool logtostderr;
        bool nonblocking;
        double interval;
        long long max_pacing_rate;
        const char *local_host;
        const char *host;
        const char *control_port;
        const char *port;
        const char *all_samples;

        /* tcp_stream */
        bool enable_read;
        bool enable_write;
        bool edge_trigger;
        unsigned long delay;

        /* tcp_rr */
        int request_size;
        int response_size;
        struct percentiles percentiles;
};

int tcp_stream(struct options *opts, struct callbacks *cb);
int tcp_rr(struct options *opts, struct callbacks *cb);

#endif
