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

#ifndef THIRD_PARTY_NEPER_PRINT_H
#define THIRD_PARTY_NEPER_PRINT_H

#include <stdio.h>
#include <sys/resource.h>
#include <sys/time.h>

struct callbacks;
struct flow;
struct percentiles;

FILE *print_header(const char *path, const char *things, const char *nl,
                   struct callbacks *);
void print_latency_header(FILE *csv, const struct percentiles *);
void print_rusage(FILE *csv, const struct rusage *, const char *nl);

#endif
