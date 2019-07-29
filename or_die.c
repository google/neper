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
#include "or_die.h"

/* Simple syscall wrappers to be used when errors are fatal to the caller. */

void bind_or_die(int s, const struct addrinfo *ai, struct callbacks *cb)
{
        while (ai) {
                if (!bind(s, ai->ai_addr, ai->ai_addrlen))
                        return;
                ai = ai->ai_next;
        }
        PLOG_FATAL(cb, "bind");
}

void *calloc_or_die(size_t nmemb, size_t size, struct callbacks *cb)
{
        void *ptr = calloc(nmemb, size);
        if (!ptr)
                PLOG_FATAL(cb, "calloc");
        return ptr;
}

void connect_or_die(int s, const struct addrinfo *ai, struct callbacks *cb)
{
        for (;;) {
                if (!connect(s, ai->ai_addr, ai->ai_addrlen))
                        return;

                switch (errno) {
                case EINPROGRESS:  /* normal and limited to async sockets */
                case EISCONN:
                        return;
                case EALREADY:
                case EINTR:
                        continue;
                }

                PLOG_FATAL(cb, "connect");
        }
}

int epoll_create1_or_die(struct callbacks *cb)
{
        int epfd = epoll_create1(0);
        if (epfd == -1)
                PLOG_FATAL(cb, "epoll_create1");
        return epfd;
}

struct addrinfo *getaddrinfo_or_die(const char *host, const char *port,
                                    const struct addrinfo *hints,
                                    struct callbacks *cb)
{
        struct addrinfo *ai;
        int s = getaddrinfo(host, port, hints, &ai);
        if (s)
                LOG_FATAL(cb, "getaddrinfo: %s", gai_strerror(s));
        return ai;
}

void listen_or_die(int s, int backlog, struct callbacks *cb)
{
        if (listen(s, backlog))
                PLOG_FATAL(cb, "listen");
}

void *malloc_or_die(size_t size, struct callbacks *cb)
{
        void *ptr = malloc(size);
        if (!ptr)
                PLOG_FATAL(cb, "malloc");
        return ptr;
}

int socket_or_die(int domain, int type, int protocol, struct callbacks *cb)
{
        int s = socket(domain, type, protocol);
        if (s == -1)
                PLOG_FATAL(cb, "socket");
        return s;
}
