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

#ifndef THIRD_PARTY_NEPER_PERCENTILES_H
#define THIRD_PARTY_NEPER_PERCENTILES_H

#include <stdbool.h>

#define PER_INDEX_99_9	101
#define PER_INDEX_99_99	102
#define PER_INDEX_COUNT	103

struct callbacks;

struct percentiles {
        bool chosen[PER_INDEX_COUNT];
};

void percentiles_parse(const char *arg, void *out, struct callbacks *);
void percentiles_print(const char *name, const void *var, struct callbacks *);

bool percentiles_chosen(const struct percentiles *, int percent);
int percentiles_count(const struct percentiles *);

#endif
