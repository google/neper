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

#ifndef THIRD_PARTY_NEPER_RUSAGE_H
#define THIRD_PARTY_NEPER_RUSAGE_H

#include <stdbool.h>
#include <stdint.h>

struct rusage;
struct timespec;

/*
 * A simple API for tracking rusage() stats efficiently over time:
 *
 * get()  Checks the timestamp. If sufficient time has elapsed, calls rusage()
 *        for another snapshot. Returns a pointer to the most recent snapshot.
 * fini() Deallocates the object, including all snapshots.
 */

struct neper_rusage {
        struct rusage *(*get)(struct neper_rusage *, const struct timespec *);
        void (*fini)(struct neper_rusage *);
};

struct neper_rusage *neper_rusage(double interval);

/* Enhanced versions of standard getrusage to improve system accuracy */
void set_getrusage_enhanced(bool stime_use_proc, int num_threads);
int getrusage_enhanced(int who, struct rusage *usage);

#endif
