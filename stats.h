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

#ifndef THIRD_PARTY_NEPER_STATS_H
#define THIRD_PARTY_NEPER_STATS_H

#include <stdbool.h>
#include <stdio.h>

struct callbacks;
struct flow;
struct neper_coef;
struct neper_histo;
struct neper_snap;
struct thread;

/*
 * The neper_stat struct is a glue object used for latency measurements.
 * It associates a neper_snaps struct with a neper_histo struct.
 *
 * flow()  Returns the flow object.
 * histo() Returns the neper_histo object.
 * snaps() Returns the neper_snaps object.
 * event() Appends a new sampling event.
 */

struct neper_stat {
        struct neper_histo *(*histo)(const struct neper_stat *);
        const struct neper_snaps *(*snaps)(const struct neper_stat *);
        void (*event)(struct thread *, struct neper_stat *, int things,
                bool force,
                void (*fn)(struct thread *, struct neper_stat *,
                           struct neper_snap *));
};

double neper_stat_cmp(void *, void *);

struct neper_coef *neper_stat_print(struct thread *, FILE *,
        void (*)(struct thread *, int flow_index, const struct neper_snap *,
                 FILE *));

struct neper_stat *neper_stat_init(struct flow *, struct neper_histo *,
                                   int extra);

/*
 * The neper_stats (note: plural) struct is a container for the neper_stat
 * (note: singluar) struct:
 *
 * insert() Adds a neper_stat struct to the collection.
 * flows()  Returns the number of neper_stat structs added for this thread.
 * snaps()  Returns the total number of neper_snap structs across all flows for
 *          this thread.
 */

struct neper_stats {
        void (*insert)(struct neper_stats *, struct neper_stat *);
        int (*flows)(struct neper_stats *);
        int (*snaps)(struct neper_stats *);
        int (*sumforeach)(struct neper_stats *,
                          int (*fn)(struct neper_stat *, void *), void *);
        void (*fini)(struct neper_stats *);
};

struct neper_stats *neper_stats_init(struct callbacks *);

#endif
