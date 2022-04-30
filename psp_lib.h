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

#ifndef THIRD_PARTY_NEPER_PSP_LIB_H_
#define THIRD_PARTY_NEPER_PSP_LIB_H_

#include <netinet/in.h>
#include "psp_kernel.h"

// PSP listener.
struct listener {
        int listenfd;
        struct sockaddr_in6 listenaddr;
};
// PSP key request to kernel.
struct key_request {
        struct sockaddr_in6 addr;
        struct psp_spi_tuple client_tuple;
};
// PSP key response from kernel.
struct key_response {
        struct psp_spi_tuple server_tuple;
};

// PSP traffic client.
void psp_ctrl_client(int ctrl_conn, struct callbacks *cb);
// PSP traffic server.
void psp_ctrl_server(int ctrl_conn, struct callbacks *cb);
// PSP pre-connect routine.
void psp_pre_connect(struct thread *t, int s, struct addrinfo *ai);
// PSP post-listener routine.
void psp_post_listen(struct thread *t, int s, struct addrinfo *ai);

#endif  // THIRD_PARTY_NEPER_PSP_LIB_H_
