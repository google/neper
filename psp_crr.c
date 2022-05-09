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
#include "rr.h"
#include "socket.h"
#include "thread.h"

static const struct neper_fn client_fn = {
        .fn_loop_init = socket_connect_all,
        .fn_flow_init = crr_flow_init,
        .fn_report    = rr_report_stats,
        .fn_type      = SOCK_STREAM,
        .fn_ctrl_client = psp_ctrl_client,
        .fn_pre_connect = psp_pre_connect,
};

static const struct neper_fn server_fn = {
        .fn_loop_init = socket_listen,
        .fn_flow_init = crr_flow_init,
        .fn_report    = rr_report_stats,
        .fn_type      = SOCK_STREAM,
        .fn_ctrl_server = psp_ctrl_server,
        .fn_post_listen = psp_post_listen,
};

int psp_crr(struct options *opts, struct callbacks *cb)
{
        const struct neper_fn *fn = opts->client ? &client_fn : &server_fn;
        /* psp_crr server doesn't collect stats, as it uses too much memory. */
        if (!opts->client)
                opts->nostats = true;
        return run_main_thread(opts, cb, fn);
}
