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

#ifndef NEPER_NUMLIST_H
#define NEPER_NUMLIST_H

#include <stddef.h>

struct callbacks;
struct numlist;

struct numlist *numlist_create(struct callbacks *cb);
void numlist_destroy(struct numlist *lst);
void numlist_add(struct numlist *lst, double val);
/**
 * The numbers in @tail are all moved to @lst.
 * @tail will become empty after this operation.
 */
void numlist_concat(struct numlist *lst, struct numlist *tail);
size_t numlist_size(struct numlist *lst);
double numlist_min(struct numlist *lst);
double numlist_max(struct numlist *lst);
double numlist_mean(struct numlist *lst);
double numlist_stddev(struct numlist *lst);
double numlist_percentile(struct numlist *lst, int percentile);

#endif
