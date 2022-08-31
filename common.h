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

#ifndef THIRD_PARTY_NEPER_COMMON_H
#define THIRD_PARTY_NEPER_COMMON_H

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lib.h"
#include "logging.h"
#include "or_die.h"

#define PROCFILE_SOMAXCONN "/proc/sys/net/core/somaxconn"

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY 0x4000000
#endif

struct flow;
struct thread;

/*
 * neper_fn is an attempt to parameterize the different functionality
 * required for each specific test.
 *
 * fn_loop_init() is called to initialize a thread. This is where servers
 * listen() and clients call connect().
 *
 * fn_flow_init() is called to initialize a flow. This is where the state
 * machine for each socket is registered with epoll.
 *
 * fn_report() kicks off the statistical analysis.
 *
 * fn_type is an integer which specifics the socket type as either TCP or UDP
 * (which cannot be determined from the command-line options).
 */

struct neper_fn {
        void (*fn_loop_init)(struct thread *);
        void (*fn_flow_init)(struct thread *, int fd);
        int (*fn_report)(struct thread *);
        void (*fn_ctrl_client)(int ctrl_conn, struct callbacks *cb);
        void (*fn_ctrl_server)(int ctrl_conn, struct callbacks *cb);
        void (*fn_pre_connect)(struct thread *thread, int fd, struct addrinfo *ai);
        void (*fn_post_listen)(struct thread *thread, int fd, struct addrinfo *ai);

        int fn_type;
};

struct rate_conversion {
        const char *str;
        const char *unit;
        double bytes_per_second;
};

extern const struct rate_conversion conversions[];

static inline void common_gettime(struct timespec *ts)
{
        clock_gettime(CLOCK_MONOTONIC, ts);
}

static inline int timespec_is_zero(const struct timespec *ts)
{
        return ts->tv_sec == 0 && ts->tv_nsec == 0;
}

static inline double seconds_between(const struct timespec *a,
                                     const struct timespec *b)
{
        return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) * 1e-9;
}

/* Stats are computed wrong with 2 samples or less. Force a minimum value,
 * the larger the better (note, test length is at least 1).
 */
static inline void adjust_interval(double *interval, int test_length)
{
        const int min_samples = 5;

        if (test_length / *interval < min_samples)
                *interval = 1.0 / min_samples;
}

extern const struct rate_conversion *neper_units_mb_pointer_hack;

int get_family(const struct options *);
int count_commas(const char *);
int count_local_hosts(const struct options *opts);
struct addrinfo **parse_local_hosts(const struct options *opts, int n,
                                    struct callbacks *cb);
void set_reuseport(int fd, struct callbacks *cb);
void set_nonblocking(int fd, struct callbacks *cb);
void set_reuseaddr(int fd, int on, struct callbacks *cb);
void set_zerocopy(int fd, int on, struct callbacks *cb);
void set_freebind(int fd, struct callbacks *cb);
void set_debug(int fd, int onoff, struct callbacks *cb);
void set_mark(int fd, int mark, struct callbacks *cb);
void fill_random(char *buf, int size);
int do_close(int fd);
struct addrinfo *copy_addrinfo(const struct addrinfo *);
void reset_port(struct addrinfo *ai, int port, struct callbacks *cb);
int create_suicide_timeout(int sec_to_suicide);
void print_unit(const char *name, const void *var, struct callbacks *);
const struct rate_conversion *auto_unit(const double throughput,
                                        const struct rate_conversion *opt,
                                        struct callbacks *);
#endif
