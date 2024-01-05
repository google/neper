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

#ifndef THIRD_PARTY_NEPER_SNAPS_H
#define THIRD_PARTY_NEPER_SNAPS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

struct neper_rusage;
struct rusage;

/*
 * The neper_snap (note: singular) struct is the basic unit of event sampling.
 * It contains a timestamp, a pointer to the most recent rusage results,
 * an event count (which could be seconds or bytes or whatever) and room for
 * additional caller-specific opaque state.
 */

struct neper_snap {
        struct timespec timespec;
        struct rusage *rusage;
        uint64_t things;
        char opaque[0];
};

void neper_snap_print(const struct neper_snap *, FILE *, double raw_thruput,
                      const char *nl);

/*
 * The neper_snaps (note: plural) struct is a container for all of the
 * neper_snap structs for one flow:
 *
 * add()       Appends a new neper_snap struct to the list.
 * count()     Returns the number of neper_snap structs added.
 * iter_next() Returns the next neper_snap as an iterator.
 * iter_done() Returns true once the end of the iterator has been reached.
 */

struct neper_snaps;

struct neper_snap *neper_snaps_add(struct neper_snaps *, const struct timespec *, uint64_t);
int neper_snaps_count(const struct neper_snaps *);
const struct neper_snap *neper_snaps_iter_next(struct neper_snaps *);
bool neper_snaps_iter_done(const struct neper_snaps *);

double neper_snaps_cmp(const struct neper_snaps *, const struct neper_snaps *);

/*
 * 'total' is the total number of neper_snap structs to allocate.
 * 'extra' is the additional space (in bytes) to reserve in each 'opaque' array.
 */

struct neper_snaps *neper_snaps_init(struct neper_rusage *,
                                     int total, int extra);

#endif
