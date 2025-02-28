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
#include "check_all_options.h"

void check_options_common(struct options *opts, struct callbacks *cb)
{
        FILE *f;

        /* Check the options shared by ALL main programs */
        CHECK(cb, !(opts->logtostderr && opts->nolog),
              "--logtostderr and --nolog are mutually exclusive.");
        CHECK(cb, opts->num_flows >= 1,
              "There must be at least 1 flow.");
        CHECK(cb, opts->num_threads >= 1,
              "There must be at least 1 thread.");
        if (opts->client) {
                CHECK(cb, opts->num_flows >= opts->num_threads,
                      "There should not be fewer flows than threads.");
                CHECK(cb, opts->num_flows >= count_local_hosts(opts),
                      "There should not be fewer flows than local hosts.");
        }
        CHECK(cb, opts->buffer_size > 0,
              "Buffer size must be positive.");
        CHECK(cb, opts->client || (opts->local_hosts == NULL),
              "local_hosts may only be set for clients.");
        CHECK(cb, opts->interval > 0,
              "Interval must be positive.");
        if ((f = fopen(PROCFILE_SOMAXCONN, "r")) != NULL) {
                int result = 0;
                if (fscanf(f, "%d", &result) != 1)
                        PLOG_FATAL(cb, "fscanf");
                fclose(f);
                CHECK(cb, opts->listen_backlog <= result,
                      "listen() backlog cannot exceed " PROCFILE_SOMAXCONN);
        }
        CHECK(cb, opts->maxevents >= 1,
              "Number of epoll events must be positive.");
        CHECK(cb, opts->max_pacing_rate >= 0,
              "Max pacing rate must be non-negative.");
        CHECK(cb, opts->max_pacing_rate <= UINT64_MAX,
              "Max pacing rate cannot exceed 64 bits.");
        CHECK(cb, opts->client || (opts->wait_start == 0),
              "The wait-start option is only valid for clients.");
}

void check_options_tcp(struct options *opts, struct callbacks *cb)
{
        /* Check the options shared by all TCP_* main programs */
        CHECK(cb, opts->source_port == -1 || opts->local_hosts == NULL,
              "localhosts may not be specified when specifying source_ports");
        if (opts->source_port != -1) {
                CHECK(cb, opts->source_port >= 1 && opts->source_port <= 0xFFFF,
                        "Source ports need to be in the range of 1..0xFFFF. "
                        "Best larger than 1024.");
        }
}

void check_options_udp(struct options *opts, struct callbacks *cb)
{
        /* Check the options shared by all UDP_* main programs */
}

void check_options_rr(struct options *opts, struct callbacks *cb)
{
        /* Check the options shared by all RR and CRR main programs */
        CHECK(cb, opts->test_length >= 1 || opts->test_length <= -1,
              "Test length must be at least 1 second or 1 transaction.");
        CHECK(cb, opts->request_size > 0,
              "Request size must be positive.");
        CHECK(cb, opts->response_size > 0,
              "Response size must be positive.");
}

void check_options_stream(struct options *opts, struct callbacks *cb)
{
        /* Check the options shared by all STREAM main programs */
        CHECK(cb, opts->test_length >= 1,
              "Test length must be at least 1 second.");
}

void check_options_tcp_crr(struct options *opts, struct callbacks *cb)
{
}

void check_options_tcp_rr(struct options *opts, struct callbacks *cb)
{
        /* Noburst-specific flags. */
        if (opts->noburst > 0) {
                CHECK(cb, opts->delay == 0,
                      "noburst cannot be set at the same time as delay");
        }
}

void check_options_tcp_stream(struct options *opts, struct callbacks *cb)
{
}

void check_options_udp_rr(struct options *opts, struct callbacks *cb)
{
}

void check_options_udp_stream(struct options *opts, struct callbacks *cb)
{
}

void check_options_psp_common(struct options *opts, struct callbacks *cb)
{
        if (opts->client) {
                CHECK(cb, opts->local_hosts != NULL,
                      "PSP client requires local IP (-L) for device lookup.");
        } else {
                CHECK(cb, opts->host != NULL,
                      "PSP server requires server IP (-H) for device lookup.");
        }
}
