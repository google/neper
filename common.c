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

#include <fcntl.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "common.h"
#include "lib.h"
#include "logging.h"
#include "or_die.h"

#define kilo (1000)
#define kibi (1024)
#define mega (1000 * 1000)
#define mebi (1024 * 1024)
#define giga (1000 * 1000 * 1000)
#define gibi (1024 * 1024 * 1024)

/* (struct rate_conversion).str: .*{b,ib,B,iB} suffix'd elements used for
 * auto-.* throughput auto unit scaling, see auto_unit() below.
 */
const struct rate_conversion conversions[] = {
        { "b",       "bits/s",     1.0 / 8 },
        { "k",       "10^3bits/s", kilo / 8 },
        { "m",       "10^6bits/s", mega / 8 },
        { "g",       "10^9bits/s", giga / 8 },

        { "B",       "B/s",        1 },
        { "K",       "KBytes/s",   kibi },
        { "M",       "MBytes/s",   mebi },
        { "G",       "GBytes/s",   gibi },

        { "Kb",      "Kbit/s",     kilo / 8 },
        { "Mb",      "Mbit/s",     mega / 8 },
        { "Gb",      "Gbit/s",     giga / 8 },

        { "KB",      "KB/s",       kilo },
        { "MB",      "MB/s",       mega },
        { "GB",      "GB/s",       giga },

        { "Kib",     "Kibit/s",    kibi / 8 },
        { "Mib",     "Mibit/s",    mebi / 8 },
        { "Gib",     "Gibit/s",    gibi / 8 },

        { "KiB",     "KiB/s",      kibi },
        { "MiB",     "MiB/s",      mebi },
        { "GiB",     "GiB/s",      gibi },

        { "auto-b",  NULL,         0 },         /* auto bits base 10 */
        { "auto-ib", NULL,         0 },         /* auto bits base 2 */
        { "auto-B",  NULL,         0 },         /* auto bytes base 10 */
        { "auto-iB", NULL,         0 },         /* auto bytes base 2 */

        { NULL,      NULL,         0 }
};

/* We want to default to Mbit/s and need a struct for the macros in main(). */
const struct rate_conversion *neper_units_mb_pointer_hack = &conversions[9];

int get_family(const struct options *opts)
{
        if (opts->ipv4 && !opts->ipv6)
                return AF_INET;
        if (opts->ipv6 && !opts->ipv4)
                return AF_INET6;
        return AF_UNSPEC;
}

int count_commas(const char *ptr)
{
        int sum = 0;

        for (;;) {
                char ch = *ptr++;

                switch (ch) {
                case '\0':
                        return sum;

                case ',':
                        sum++;
                        break;

                default:
                        break;
                }
        }
}

int count_local_hosts(const struct options *opts)
{
        const char *ptr = opts->local_hosts;
        if (!ptr)
               return 0;
        return count_commas(ptr) + 1;
}

struct addrinfo **parse_local_hosts(const struct options *opts, int n,
                                    struct callbacks *cb)
{
        const char *ptr = opts->local_hosts;
        const char *port = "0";
        struct addrinfo **ai = NULL;
        char host[100];

        const struct addrinfo hints = {
                .ai_flags    = AI_PASSIVE,
                .ai_family   = get_family(opts),
                .ai_socktype = SOCK_STREAM
        };

        if (n) {
                ai = calloc_or_die(n, sizeof(struct addrinfo *), cb);
                int i;

                for (i = 0; i < n; i++) {
                        const char *comma = strchr(ptr, ',');
                        int len = comma ? (comma - ptr) : strlen(ptr);
                        if (len >= sizeof(host))
                                LOG_FATAL(cb, "host string overflow");
                        memcpy(host, ptr, len);
                        host[len] = 0;
                        ptr += len + 1;

                        ai[i] = getaddrinfo_or_die(host, port, &hints, cb);
                }
        }

        return ai;
}

void print_unit(const char *name, const void *var, struct callbacks *cb)
{
        const struct rate_conversion *p = *(const struct rate_conversion **)var;
        if (p)
                PRINT(cb, name, "%s", p->str);
}

const struct rate_conversion *auto_unit(const double throughput,
                                        const struct rate_conversion *opt,
                                        struct callbacks *cb)
{
        const struct rate_conversion *conv;
        const char *type = strchr(opt->str, '-') + 1;
        const int type_len = strlen(type);

        for (conv = conversions; conv->str; conv++) {
                int str_len = strlen(conv->str);
                if (str_len - 1 < type_len)
                        continue;
                if (strcmp(&conv->str[str_len - type_len], type) == 0) {
                        double val = throughput / conv->bytes_per_second;
                        if (val < 1000 && val > 1)
                                return conv;
                }
        }
        LOG_FATAL(cb, "internal error rate unit '%s'", opt->str);
        return NULL;  /* unreachable */
}

void set_reuseport(int fd, struct callbacks *cb)
{
        int optval = 1;
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)))
                PLOG_ERROR(cb, "setsockopt(SO_REUSEPORT)");
}

void set_reuseaddr(int fd, int on, struct callbacks *cb)
{
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
                PLOG_ERROR(cb, "setsockopt(SO_REUSEADDR)");
}

void set_tx_zerocopy(int fd, int on, struct callbacks *cb)
{
#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif
        if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &on, sizeof(on)))
                PLOG_ERROR(cb, "setsockopt(SO_ZEROCOPY)");
}

void set_debug(int fd, int onoff, struct callbacks *cb)
{
        if (setsockopt(fd, SOL_SOCKET, SO_DEBUG, &onoff, sizeof(onoff)))
                PLOG_ERROR(cb, "setsockopt(SO_DEBUG)");
}

void set_nonblocking(int fd, struct callbacks *cb)
{
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
                flags = 0;
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
                PLOG_FATAL(cb, "fcntl");
}

void set_freebind(int fd, struct callbacks *cb) {
        int value = true;
        if (setsockopt(fd, SOL_IP, IP_FREEBIND, &value, sizeof(value)))
                PLOG_ERROR(cb, "setsockopt(FREEBIND)");
}

void set_mark(int fd, int mark, struct callbacks *cb) {
        if (setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark)))
                PLOG_ERROR(cb, "setsockopt(MARK)");
}

void set_tcp_tx_delay(int fd, int delay, struct callbacks *cb)
{
        if (setsockopt(fd, SOL_TCP, TCP_TX_DELAY, &delay, sizeof(delay)))
                PLOG_ERROR(cb, "setsockopt(TCP_TX_DELAY)");
}

void set_congestion_control(int fd, const char *cc, struct callbacks *cb)
{
        if (setsockopt(fd, SOL_TCP, TCP_CONGESTION, cc, strlen(cc)))
                PLOG_ERROR(cb, "setsockopt(TCP_CONGESTION)");
}

void set_tcp_no_delay(int fd, struct callbacks *cb)
{
        int value = true;
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &value, sizeof(value)))
                PLOG_ERROR(cb, "setsockopt(TCP_NODELAY)");	
}

void fill_random(char *buf, int size)
{
        int fd, chunk, done = 0;

        fd = open("/dev/urandom", O_RDONLY);
        if (fd == -1)
                return;
        while (done < size) {
                chunk = read(fd, buf + done, size - done);
                if (chunk <= 0)
                        break;
                done += chunk;
        }
        close(fd);
}

int do_close(int fd)
{
        for (;;) {
                int ret = close(fd);
                if (ret == -1 && errno == EINTR)
                        continue;
                return ret;
        }
}

struct addrinfo *copy_addrinfo(const struct addrinfo *in)
{
        struct addrinfo *out = calloc(1, sizeof(*in) + in->ai_addrlen);
        out->ai_flags = in->ai_flags;
        out->ai_family = in->ai_family;
        out->ai_socktype = in->ai_socktype;
        out->ai_protocol = in->ai_protocol;
        out->ai_addrlen = in->ai_addrlen;
        out->ai_addr = (struct sockaddr *)(out + 1);
        memcpy(out->ai_addr, in->ai_addr, in->ai_addrlen);
        return out;
}

void reset_port(struct addrinfo *ai, int port, struct callbacks *cb)
{
        if (ai->ai_addr->sa_family == AF_INET)
                ((struct sockaddr_in *)ai->ai_addr)->sin_port = htons(port);
        else if (ai->ai_addr->sa_family == AF_INET6)
                ((struct sockaddr_in6 *)ai->ai_addr)->sin6_port = htons(port);
        else
                LOG_FATAL(cb, "invalid sa_family %d", ai->ai_addr->sa_family);
}

static void suicide_timeout_handler(int sig, siginfo_t *sig_info, void *arg)
{
        printf("timeout handler\n");
        exit(-1);
}

int create_suicide_timeout(int sec_to_suicide)
{
        timer_t timerid;
        struct sigevent sev;
        sigset_t mask;
        struct itimerspec its;
        struct sigaction sa;

        sa.sa_sigaction = suicide_timeout_handler;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
                perror("sigaction");
                return -1;
        }

        sigemptyset(&mask);
        sigaddset(&mask, SIGRTMIN);
        if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
                perror("sigprocmask(SIG_SETMASK)");
                return -1;
        }

        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo = SIGRTMIN;
        sev.sigev_value.sival_ptr = &timerid;
        if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
                perror("timer_create");
                return -1;
        }

        its.it_value.tv_sec = sec_to_suicide;
        its.it_value.tv_nsec = 0;
        its.it_interval.tv_sec = its.it_value.tv_sec;
        its.it_interval.tv_nsec = its.it_value.tv_nsec;
        if (timer_settime(timerid, 0, &its, NULL) == -1) {
                perror("timer_settime");
                return -1;
        }

        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
                perror("sigprocmask(SIG_UNBLOCK)");
                return -1;
        }
        return 0;
}
