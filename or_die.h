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

#ifndef THIRD_PARTY_NEPER_OR_DIE_H
#define THIRD_PARTY_NEPER_OR_DIE_H

#include <sys/param.h>

struct addrinfo;
struct callbacks;
struct epoll_event;

void bind_or_die(int, const struct addrinfo *, struct callbacks *);
void *calloc_or_die(size_t nmemb, size_t size, struct callbacks *);
void connect_or_die(int, const struct addrinfo *, struct callbacks *);
int epoll_create1_or_die(struct callbacks *);
struct addrinfo *getaddrinfo_or_die(const char *host, const char *port,
                                    const struct addrinfo *hints,
                                    struct callbacks *);
void listen_or_die(int, int backlog, struct callbacks *);
void *malloc_or_die(size_t, struct callbacks *);
void *map_hugetlb_or_die(size_t, struct callbacks *);
void *realloc_or_die(void *ptr, size_t, struct callbacks *);
int socket_or_die(int domain, int type, int protocol, struct callbacks *);

#endif
