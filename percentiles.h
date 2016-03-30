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

#ifndef NEPER_PERCENTILES_H
#define NEPER_PERCENTILES_H

#include <stdbool.h>

struct callbacks;

struct percentiles {
        bool chosen[101];
};

void parse_percentiles(char *arg, void *out, struct callbacks *cb);
void print_percentiles(const char *name, const void *var, struct callbacks *cb);

#endif
