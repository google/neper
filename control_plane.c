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

#include "control_plane.h"
#include <netinet/tcp.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include "hexdump.h"
#include "lib.h"
#include "logging.h"

static int recv_magic(int fd, struct callbacks *cb, const char *fn)
{
        int n, magic = 0;

        while ((n = read(fd, &magic, sizeof(magic))) == -1) {
                if (errno == EINTR || errno == EAGAIN)
                        continue;
                PLOG_FATAL(cb, "%s: read", fn);
        }
        if (n != sizeof(magic))
                LOG_FATAL(cb, "%s: Incomplete read %d", fn, n);
        return ntohl(magic);
}

static void send_magic(int fd, int magic, struct callbacks *cb, const char *fn)
{
        int n;

        magic = htonl(magic);
        while ((n = write(fd, &magic, sizeof(magic))) == -1) {
                if (errno == EINTR || errno == EAGAIN)
                        continue;
                PLOG_FATAL(cb, "%s: write", fn);
        }
        if (n != sizeof(magic))
                LOG_FATAL(cb, "%s: Incomplete write %d", fn, n);
}

static const char control_port_secret[] = "neper control port secret";
#define SECRET_SIZE (sizeof(control_port_secret))

static int ctrl_connect(const char *host, const char *port,
                        struct addrinfo **ai, struct options *opts,
                        struct callbacks *cb)
{
        int ctrl_conn, magic, optval = 1;
        ctrl_conn = try_connect(host, port, ai, opts, cb);
        if (setsockopt(ctrl_conn, IPPROTO_TCP, TCP_NODELAY, &optval,
                       sizeof(optval)))
                PLOG_ERROR(cb, "setsockopt(TCP_NODELAY)");
        while (write(ctrl_conn, control_port_secret, SECRET_SIZE) == -1) {
                if (errno == EINTR)
                        continue;
                PLOG_FATAL(cb, "write");
        }
        /* if authentication passes, server should write back a magic number */
        magic = recv_magic(ctrl_conn, cb, __func__);
        if (magic != opts->magic)
                LOG_FATAL(cb, "magic mismatch: %d != %d", magic, opts->magic);
        return ctrl_conn;
}

static int ctrl_listen(const char *host, const char *port,
                       struct addrinfo **ai, struct options *opts,
                       struct callbacks *cb)
{
        struct addrinfo *result, *rp;
        int flags = AI_PASSIVE;
        int fd_listen = 0;

        result = do_getaddrinfo(host, port, flags, opts, cb);
        for (rp = result; rp != NULL; rp = rp->ai_next) {
                fd_listen = socket(rp->ai_family, rp->ai_socktype,
                                   rp->ai_protocol);
                if (fd_listen == -1) {
                        PLOG_ERROR(cb, "socket");
                        continue;
                }
                set_reuseport(fd_listen, cb);
                set_reuseaddr(fd_listen, 1, cb);
                if (bind(fd_listen, rp->ai_addr, rp->ai_addrlen) == 0)
                        break;
                PLOG_ERROR(cb, "bind");
                do_close(fd_listen);
        }
        if (rp == NULL)
                LOG_FATAL(cb, "Could not bind");
        *ai = copy_addrinfo(rp);
        freeaddrinfo(result);
        if (listen(fd_listen, opts->listen_backlog))
                PLOG_FATAL(cb, "listen");
        return fd_listen;
}

static int ctrl_accept(int ctrl_port, int *num_incidents, struct callbacks *cb,
                       int magic)
{
        char buf[1024], dump[8192], host[NI_MAXHOST], port[NI_MAXSERV];
        struct sockaddr_storage cli_addr;
        socklen_t cli_len;
        int ctrl_conn, s;
        ssize_t len;
retry:
        cli_len = sizeof(cli_addr);
        while ((ctrl_conn = accept(ctrl_port, (struct sockaddr *)&cli_addr,
                                   &cli_len)) == -1) {
                if (errno == EINTR || errno == ECONNABORTED)
                        continue;
                PLOG_FATAL(cb, "accept");
        }
        s = getnameinfo((struct sockaddr *)&cli_addr, cli_len,
                        host, sizeof(host), port, sizeof(port),
                        NI_NUMERICHOST | NI_NUMERICSERV);
        if (s) {
                LOG_ERROR(cb, "getnameinfo: %s", gai_strerror(s));
                strcpy(host, "(unknown)");
                strcpy(port, "(unknown)");
        }
        memset(buf, 0, sizeof(buf));
        while ((len = read(ctrl_conn, buf, sizeof(buf))) == -1) {
                if (errno == EINTR)
                        continue;
                PLOG_ERROR(cb, "read");
                do_close(ctrl_conn);
                goto retry;
        }
        if (memcmp(buf, control_port_secret, SECRET_SIZE) != 0) {
                if (num_incidents)
                        (*num_incidents)++;
                if (hexdump(buf, len, dump, sizeof(dump))) {
                        LOG_WARN(cb, "Invalid secret from %s:%s\n%s", host,
                                 port, dump);
                } else
                        LOG_WARN(cb, "Invalid secret from %s:%s", host, port);
                do_close(ctrl_conn);
                goto retry;
        }
        /* tell client that authentication passes */
        send_magic(ctrl_conn, magic, cb, __func__);
        LOG_INFO(cb, "Control connection established with %s:%s", host, port);
        return ctrl_conn;
}

static void ctrl_wait_client(int ctrl_conn, int expect, struct callbacks *cb)
{
        int magic;

        while ((magic = recv_magic(ctrl_conn, cb, __func__)) != expect)
                LOG_WARN(cb, "Unexpected magic %d", magic);
}

static void ctrl_notify_server(int ctrl_conn, int magic, struct callbacks *cb)
{
        send_magic(ctrl_conn, magic, cb, __func__);
        if (shutdown(ctrl_conn, SHUT_WR))
                PLOG_ERROR(cb, "shutdown");
}

struct control_plane {
        struct options *opts;
        struct callbacks *cb;
        int num_incidents;
        int ctrl_conn;
        int ctrl_port;
};

struct control_plane* control_plane_create(struct options *opts,
                                           struct callbacks *cb)
{
        struct control_plane *cp;

        cp = calloc(1, sizeof(*cp));
        cp->opts = opts;
        cp->cb = cb;
        return cp;
}

void control_plane_start(struct control_plane *cp, struct addrinfo **ai)
{
        if (cp->opts->client) {
                cp->ctrl_conn = ctrl_connect(cp->opts->host,
                                             cp->opts->control_port, ai,
                                             cp->opts, cp->cb);
                LOG_INFO(cp->cb, "connected to control port");
        } else {
                cp->ctrl_port = ctrl_listen(NULL, cp->opts->control_port, ai,
                                            cp->opts, cp->cb);
                LOG_INFO(cp->cb, "opened control port");
        }
}

void control_plane_wait_until_done(struct control_plane *cp)
{
        if (cp->opts->client) {
                sleep(cp->opts->test_length);
                LOG_INFO(cp->cb, "finished sleep");
        } else {
                const int n = cp->opts->num_clients;
                int* client_fds = calloc(n, sizeof(int));
                int i;

                if (!client_fds)
                        PLOG_FATAL(cp->cb, "calloc client_fds");
                LOG_INFO(cp->cb, "expecting %d clients", n);
                for (i = 0; i < n; i++) {
                        client_fds[i] = ctrl_accept(cp->ctrl_port,
                                                    &cp->num_incidents, cp->cb,
                                                    cp->opts->magic);
                        LOG_INFO(cp->cb, "client %d connected", i);
                }
                do_close(cp->ctrl_port);  /* disallow further connections */
                if (cp->opts->nonblocking) {
                        for (i = 0; i < n; i++)
                                set_nonblocking(client_fds[i], cp->cb);
                }
                LOG_INFO(cp->cb, "expecting %d notifications", n);
                for (i = 0; i < n; i++) {
                        ctrl_wait_client(client_fds[i], cp->opts->magic,
                                         cp->cb);
                        LOG_INFO(cp->cb, "received notification %d", i);
                }
                for (i = 0; i < n; i++)
                        do_close(client_fds[i]);
                free(client_fds);
        }
}

void control_plane_stop(struct control_plane *cp)
{
        if (cp->opts->client) {
                ctrl_notify_server(cp->ctrl_conn, cp->opts->magic, cp->cb);
                LOG_INFO(cp->cb, "notified server to exit");
                do_close(cp->ctrl_conn);
        }
}

int control_plane_incidents(struct control_plane *cp)
{
        return cp->num_incidents;
}

void control_plane_destroy(struct control_plane *cp)
{
        free(cp);
}
