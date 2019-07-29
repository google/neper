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

#ifndef THIRD_PARTY_NEPER_DEFINE_ALL_FLAGS_H
#define THIRD_PARTY_NEPER_DEFINE_ALL_FLAGS_H

struct flags_parser *add_flags_common(struct flags_parser *fp);

struct flags_parser *add_flags_tcp(struct flags_parser *fp);
struct flags_parser *add_flags_udp(struct flags_parser *fp);

struct flags_parser *add_flags_rr(struct flags_parser *fp);
struct flags_parser *add_flags_stream(struct flags_parser *fp);

struct flags_parser *add_flags_tcp_rr(struct flags_parser *fp);
struct flags_parser *add_flags_tcp_crr(struct flags_parser *fp);
struct flags_parser *add_flags_tcp_stream(struct flags_parser *fp);
struct flags_parser *add_flags_udp_rr(struct flags_parser *fp);
struct flags_parser *add_flags_udp_stream(struct flags_parser *fp);

#endif
