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

#ifndef THIRD_PARTY_NEPER_CHECK_ALL_OPTIONS_H_
#define THIRD_PARTY_NEPER_CHECK_ALL_OPTIONS_H_

void check_options_common(    struct options *opts, struct callbacks *cb);

void check_options_tcp(       struct options *opts, struct callbacks *cb);
void check_options_udp(       struct options *opts, struct callbacks *cb);

void check_options_rr(        struct options *opts, struct callbacks *cb);
void check_options_stream(    struct options *opts, struct callbacks *cb);

void check_options_tcp_rr(    struct options *opts, struct callbacks *cb);
void check_options_tcp_crr(   struct options *opts, struct callbacks *cb);
void check_options_tcp_stream(struct options *opts, struct callbacks *cb);
void check_options_udp_rr(    struct options *opts, struct callbacks *cb);
void check_options_udp_stream(struct options *opts, struct callbacks *cb);

#endif  // THIRD_PARTY_NEPER_CHECK_ALL_OPTIONS_H_

