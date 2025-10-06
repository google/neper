/*
 * Copyright 2022 Google Inc.
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
#include "psp_lib.h"
#include "socket.h"
#include "stream.h"
#include "thread.h"

#include "psp_kernel.h"

static int kmfd;
static pthread_t km_thread;
static const int port = 8973; // arbitrary port number
static pthread_mutex_t psp_key_lock = PTHREAD_MUTEX_INITIALIZER;

static struct listener *listeners;
static int nlisteners;
static int next_listener;
static int listener_space = 1;

static void psp_resize_listeners_or_die(struct thread *ts) {
        int new_max = listener_space << 2;

        listeners = realloc_or_die(listeners, sizeof(struct listener) * new_max,
                                   ts->cb);
        listener_space = new_max;
}

void psp_ctrl_client(int ctrl_conn, struct callbacks *cb) {
        struct sockaddr_in6 kmaddr;
        socklen_t kmaddrlen = sizeof(kmaddr);

        if (getpeername(ctrl_conn, (struct sockaddr *)&kmaddr, &kmaddrlen) < 0) {
                LOG_FATAL(cb, "Can't get peer address: %s", strerror(errno));
        }
        kmaddr.sin6_port = htons(port);

        /* start key mgt service.  wait for connection */
        kmfd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (kmfd < 0) {
                LOG_FATAL(cb, "Can't create km client socket: %s", strerror(errno));
        }
        if (connect(kmfd, (const struct sockaddr *)&kmaddr, sizeof(kmaddr)) < 0) {
                LOG_FATAL(cb, "Can't connect km client socket: %s", strerror(errno));
        }
        LOG_INFO(cb, "Connected to km socket");
}

static int get_listen_fd(struct callbacks *cb, struct sockaddr_in6 *addr) {
        int i;

        /* key_server is single threaded exclusive user of nlisteners,
         * so it is safe to access without locking.
         */
        i = next_listener;
        do {
                if (addr->sin6_port == listeners[i].listenaddr.sin6_port) {
                        next_listener = (i + 1) % nlisteners;
                        return listeners[i].listenfd;
                }

                i = (i + 1) % nlisteners;
        } while (i != next_listener);

        return -1;
}

static void *psp_key_server(void *arg)
{
        struct callbacks *cb = arg;
        int on = 1;
        int kmlfd, kmfd, len;
        struct sockaddr_in6 kmaddr;
        struct sockaddr_in6 acceptaddr;
        socklen_t acceptaddrlen = sizeof(acceptaddr);

        memset(&kmaddr, 0, sizeof(kmaddr));
        kmaddr.sin6_family = AF_INET6;
        kmaddr.sin6_port = htons(port);

        /* start key mgt service.  wait for connection */
        kmlfd = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        if (kmlfd < 0) {
                LOG_FATAL(cb, "Can't create listen socket: %s", strerror(errno));
        }
        if (setsockopt(kmlfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
                LOG_FATAL(cb, "Can't set SO_REUSEADDR on listen socket: %s", strerror(errno));
        }
        if (bind(kmlfd, (const struct sockaddr *)&kmaddr, sizeof(kmaddr)) < 0) {
                LOG_FATAL(cb, "Can't bind listen socket: %s", strerror(errno));
        }
        if (listen(kmlfd, 5) < 0) {
                LOG_FATAL(cb, "Can't listen on listen socket: %s", strerror(errno));
        }
        LOG_INFO(cb, "Waiting for connection on listen socket");
        kmfd = accept(kmlfd, (struct sockaddr *)&acceptaddr, &acceptaddrlen);
        if (kmfd < 0) {
                LOG_FATAL(cb, "Can't accept on listen socket: %s", strerror(errno));
        }
        close(kmlfd);

        len = sizeof(struct key_request);
        if (setsockopt(kmfd, SOL_SOCKET, SO_RCVLOWAT, &len, sizeof(len)) < 0) {
                LOG_FATAL(cb, "setsockopt(..., SO_RCVLOWAT, ...) failed");
        }

        for (;;) {
                struct key_request req;
                struct key_response resp;
                struct psp_spi_tuple listen_tuple;
                int err, count;
                socklen_t listen_size;
                int listenfd;

                count = read(kmfd, &req, sizeof(req));
                if (count < 0) {
                        LOG_ERROR(cb, "Key management read failed: %s", strerror(errno));
                } else if (count == 0) {
                        LOG_FATAL(cb, "Unexpected EOF on key management connection");
                } else if (count != sizeof(req)) {
                        LOG_FATAL(cb, "Short read of key request");
                }

                LOG_INFO(cb, "Got km request");

                listenfd = get_listen_fd(cb, &req.addr);
                if (listenfd < 0) {
                        LOG_FATAL(cb, "Port not found");
                }

                listen_tuple = req.client_tuple;
                listen_size = sizeof(listen_tuple);

                LOG_INFO(cb, "Setting listen key on fd %d", listenfd);

                err = getsockopt(listenfd, IPPROTO_TCP, TCP_PSP_LISTENER,
                    &listen_tuple, &listen_size);
                if (err < 0) {
                        LOG_FATAL(cb, "TCP_PSP_LISTENER failed: %s", strerror(errno));
                }

                resp.server_tuple = listen_tuple;

                LOG_INFO(cb, "Sending km reply");

                count = write(kmfd, &resp, sizeof(resp));
                if (count < 0) {
                        LOG_FATAL(cb, "Key response write failed: %s", strerror(errno));
                } else if (count == 0) {
                        LOG_FATAL(cb, "Nothing written in key response");
                } else if (count != sizeof(resp)) {
                        LOG_FATAL(cb, "Short write of key response");
                }
        }
        pthread_exit(NULL);
}

void psp_ctrl_server(int ctrl_conn, struct callbacks *cb) {
        pthread_create(&km_thread, NULL, psp_key_server, cb);
}

void psp_pre_connect(struct thread *t, int s, struct addrinfo *ai)
{
        struct key_request req;
        struct key_response resp;

        int err, count;
        socklen_t caller_size = sizeof(req.client_tuple);

        LOG_INFO(t->cb, "in pre_connect");

        if (ai->ai_family != AF_INET6) {
                LOG_FATAL(t->cb, "PSP is IPv6-only, address family %d not implemented", ai->ai_family);
        }

        if (ai->ai_addrlen != sizeof(struct sockaddr_in6)) {
                LOG_FATAL(t->cb, "PSP is IPv6-only, wrong size sockaddr supplied: %d (%d expected)",
                    ai->ai_addrlen, (int)sizeof(struct sockaddr_in6));
        }

        /*
         * locking here as key management socket is shared resource, and mixed
         * up requests & responses can lead to connections that don't work
         */
        pthread_mutex_lock(&psp_key_lock);
        LOG_INFO(t->cb, "getting rx key");
        /* get our rx key */
        err = getsockopt(s, IPPROTO_TCP, TCP_PSP_RX_SPI_KEY,
            &req.client_tuple, &caller_size);
        if (err < 0) {
                PLOG_FATAL(t->cb, "TCP_PSP_RX_SPI_KEY failed");
        }

        req.addr = *(struct sockaddr_in6 *)ai->ai_addr;
        LOG_INFO(t->cb, "Sending km request");

        /* trade keys, fill in server_tuple */
        count = write(kmfd, &req, sizeof(req));
        if (count != sizeof(req)) {
                LOG_FATAL(t->cb, "Short write of key request");
        }

        count = sizeof(resp);
        if (setsockopt(kmfd, SOL_SOCKET, SO_RCVLOWAT,
                       &count, sizeof(count)) < 0) {
                LOG_FATAL(t->cb, "setsockopt(..., SO_RCVLOWAT, ...) failed");
        }

        LOG_INFO(t->cb, "waiting for km reply");
        count = read(kmfd, &resp, sizeof(resp));
        if (count == 0) {
                LOG_FATAL(t->cb, "EOF on key management socket");
        } else if (count != sizeof(resp)) {
                LOG_FATAL(t->cb, "Short read of key reply");
        }
        /* set our tx key */

        err = setsockopt(s, IPPROTO_TCP, TCP_PSP_TX_SPI_KEY,
            &resp.server_tuple, sizeof(resp.server_tuple));
        if (err < 0) {
                LOG_FATAL(t->cb, "TCP_PSP_TX_SPI_KEY failed: %s", strerror(errno));
        }
        LOG_INFO(t->cb, "set tx key");
        pthread_mutex_unlock(&psp_key_lock);
}

void psp_post_listen(struct thread *t, int s, struct addrinfo *ai) {
        struct sockaddr_in6 *sin6;

        if (ai->ai_family != AF_INET6) {
                LOG_FATAL(t->cb, "PSP is IPv6-only, address family %d not implemented", ai->ai_family);
        }

        if (ai->ai_addrlen != sizeof(struct sockaddr_in6)) {
                LOG_FATAL(t->cb, "PSP is IPv6-only, wrong size sockaddr supplied: %d (%d expected)",
                    ai->ai_addrlen, (int)sizeof(struct sockaddr_in6));
        }

        sin6 = (struct sockaddr_in6 *)ai->ai_addr;

        LOG_INFO(t->cb, "registering PSP listener on port %d", ntohs(sin6->sin6_port));

        pthread_mutex_lock(&psp_key_lock);

        if ((nlisteners + 1) >= listener_space)
                psp_resize_listeners_or_die(t);

        listeners[nlisteners].listenfd = s;
        listeners[nlisteners].listenaddr = *sin6;
        nlisteners++;

        pthread_mutex_unlock(&psp_key_lock);
}
