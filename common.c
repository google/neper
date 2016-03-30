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

#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/tcp.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

struct rate_conversion {
        const char *prefix;
        double bytes_per_second;
};

#define kilo (1000)
#define kibi (1024)
#define mega (1000 * 1000)
#define mebi (1024 * 1024)
#define giga (1000 * 1000 * 1000)
#define gibi (1024 * 1024 * 1024)

static const struct rate_conversion conversions[] = {
        { "b",   0.125 },
        { "B",   1 },
        { "kb",  kilo / 8 },
        { "Kib", kibi / 8 },
        { "kB",  kilo },
        { "KiB", kibi },
        { "Mb",  mega / 8 },
        { "Mib", mebi / 8 },
        { "MB",  mega },
        { "MiB", mebi },
        { "Gb",  giga / 8 },
        { "Gib", gibi / 8 },
        { "GB",  giga },
        { "GiB", gibi },
        { NULL,  0 }
};

struct addrinfo *do_getaddrinfo(const char *host, const char *port, int flags,
                                const struct options *opts,
                                struct callbacks *cb)
{
        struct addrinfo hints, *result;

        memset(&hints, 0, sizeof(hints));
        if (opts->ipv4 && !opts->ipv6)
                hints.ai_family = AF_INET;
        else if (opts->ipv6 && !opts->ipv4)
                hints.ai_family = AF_INET6;
        else
                hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;  /* Stream socket */
        hints.ai_flags = flags;
        hints.ai_protocol = 0;            /* Any protocol */

        LOG_INFO(cb, "before getaddrinfo");
        int s = getaddrinfo(host, port, &hints, &result);
        LOG_INFO(cb, "after getaddrinfo");
        if (s)
                LOG_FATAL(cb, "getaddrinfo: %s", gai_strerror(s));

        return result;
}

long long parse_rate(const char *str, struct callbacks *cb)
{
        const struct rate_conversion *conv;
        char *suffix;
        double val;

        errno = 0;
        val = strtod(str, &suffix);
        if ((errno == ERANGE && (val == HUGE_VAL || val == -HUGE_VAL)) ||
            (errno != 0 && val == 0))
                PLOG_FATAL(cb, "strtod");
        if (suffix == str)
                LOG_FATAL(cb, "no digits were found");
        if (suffix[0] == '\0')
                return val;
        for (conv = conversions; conv->prefix; conv++) {
                if (strncmp(suffix, conv->prefix, strlen(conv->prefix)) == 0)
                        return val * conv->bytes_per_second;
        }
        LOG_FATAL(cb, "invalid suffix `%s'", suffix);
        return 0;  /* unreachable */
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

void set_min_rto(int fd, int min_rto_ms, struct callbacks *cb)
{
        int min_rto = min_rto_ms * 1000 * 1000;  /* in nanoseconds */
#ifndef TCP_MIN_RTO
#define TCP_MIN_RTO 1713
#endif
        if (setsockopt(fd, SOL_TCP, TCP_MIN_RTO, &min_rto, sizeof(min_rto)))
                PLOG_ERROR(cb, "setsockopt(TCP_MIN_RTO)");
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

void set_local_host(int fd, struct options *opts, struct callbacks *cb)
{
        struct addrinfo *result, *rp;
        const char *port = "0";
        int flags = 0;

        result = do_getaddrinfo(opts->local_host, port, flags, opts, cb);

        for (rp = result; rp; rp = rp->ai_next) {
                if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0)
                        goto done;
                PLOG_ERROR(cb, "bind");
                do_close(fd);
        }
        LOG_FATAL(cb, "Could not bind");
done:
        freeaddrinfo(result);
}

int procfile_int(const char *path, struct callbacks *cb)
{
        int result = 0;
        FILE *f = fopen(path, "r");
        if (!f)
                PLOG_FATAL(cb, "fopen '%s'", path);
        if (fscanf(f, "%d", &result) != 1)
		PLOG_FATAL(cb, "fscanf");
        fclose(f);
        return result;
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

int do_connect(int s, const struct sockaddr *addr, socklen_t addr_len)
{
        for (;;) {
                int ret = connect(s, addr, addr_len);
                if (ret == -1 && (errno == EINTR || errno == EALREADY))
                        continue;
                if (ret == -1 && errno == EISCONN)
                        return 0;
                return ret;
        }
}

struct addrinfo *copy_addrinfo(struct addrinfo *in)
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

int try_connect(const char *host, const char *port, struct addrinfo **ai,
                struct options *opts, struct callbacks *cb)
{
        struct addrinfo *result, *rp;
        int sfd = 0, allowed_retry = 30;
        int flags = 0;

        result = do_getaddrinfo(host, port, flags, opts, cb);
retry:
        /* getaddrinfo() returns a list of address structures.
         * Try each address until we successfully connect().
         */
        for (rp = result; rp != NULL; rp = rp->ai_next) {
                sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (sfd == -1) {
                        if (errno == EMFILE || errno == ENFILE ||
                            errno == ENOBUFS || errno == ENOMEM)
                                PLOG_FATAL(cb, "socket");
                        /* Other errno's are not fatal. */
                        PLOG_ERROR(cb, "socket");
                        continue;
                }
                if (do_connect(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
                        break;
                PLOG_ERROR(cb, "connect");
                do_close(sfd);
        }
        if (rp == NULL) {
                if (allowed_retry-- > 0) {
                        sleep(1);
                        goto retry;
                }
                LOG_FATAL(cb, "Could not connect");
        }
        *ai = copy_addrinfo(rp);
        freeaddrinfo(result);
        return sfd;
}

void parse_all_samples(char *arg, void *out, struct callbacks *cb)
{
        if (arg)
                *(const char **)out = arg;
        else
                *(const char **)out = "samples.csv";
}

void parse_max_pacing_rate(char *arg, void *out, struct callbacks *cb)
{
        *(long long *)out = parse_rate(arg, cb);
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
