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

#include "snaps.h"
#include "common.h"
#include "print.h"
#include "rusage.h"

struct neper_snaps {
        struct neper_rusage *rusage;
        int total;   /* # of snap structs allocated */
        int count;   /* # of populated snap structs */
        int iter;    /* iterator bookmark */
        int extent;  /* extended size of the snap struct */
        char *ptr;   /* storage */
};

/*
 * Compare two neper_snap structs.
 * The one with the earlier timestamp is considered to be smaller.
 */

static double neper_snap_cmp(const struct neper_snap *a,
                             const struct neper_snap *b)
{
        return seconds_between(&b->timespec, &a->timespec);
}

void neper_snap_print(const struct neper_snap *snap, FILE *csv,
                      double raw_thruput, const char *nl)
{
        fprintf(csv, "%ld.%09ld,%zd,%.2f",
                snap->timespec.tv_sec, snap->timespec.tv_nsec, snap->things,
                raw_thruput);
        print_rusage(csv, snap->rusage, nl);
}

static struct neper_snap *snaps_get(const struct neper_snaps *impl, int i)
{
        if (i < 0)
                return NULL;
        if (i >= impl->total) {
                /*
                 * Temporary bandaid to avoid crashes if the test lasts
                 * longer than expected. See neper_snaps_init().
                */
                static int reported = 0;

                if (!reported) {
                        fprintf(stderr, "Test longer than expected (%d), "
                                "use -l <duration> to extend\n",
                                i + reported++);
		}
                i = impl->total; /* point to spare element */
        }

        return (void *)(impl->ptr + i * impl->extent);
}

/* Compare two containers by comparing the current iterator objects for each. */

double neper_snaps_cmp(const struct neper_snaps *a,
                       const struct neper_snaps *b)
{
        const struct neper_snap *sa = snaps_get(a, a->iter);
        const struct neper_snap *sb = snaps_get(b, b->iter);

        return neper_snap_cmp(sa, sb);
}

struct neper_snap *neper_snaps_add(struct neper_snaps *snaps,
                                   const struct timespec *now, uint64_t things)
{
        struct neper_snap *snap = snaps_get(snaps, snaps->count++);

        snap->timespec = *now;
        snap->rusage   = snaps->rusage->get(snaps->rusage, now);
        snap->things   = things;

        return snap;
}

int neper_snaps_count(const struct neper_snaps *snaps)
{
        return snaps->count;
}

const struct neper_snap *neper_snaps_iter_next(struct neper_snaps *snaps)
{
        int i = snaps->iter++;
        return (i < snaps->count) ? snaps_get(snaps, i) : NULL;
}

bool neper_snaps_iter_done(const struct neper_snaps *snaps)
{
        return snaps->iter >= snaps->count;
}

struct neper_snaps *neper_snaps_init(struct neper_rusage *rusage,
                                     int total, int extra)
{
        /*
         * Allocate memory for all the samples at startup, based on the
         * known values for total_length and interval. The actual test
         * may be longer than what the server expects, so we create an
         * extra element that is used for all subsequent samples.
         * This is a temporary bandaid to avoid panics or out of memory
         * errors at runtime. It may hurt some statistics.
         * See snaps_get().
         * TODO(lrizzo) Design a proper mechanism for dynamic allocation.
         */
        const int extent = sizeof(struct neper_snap) + extra;
        const int size = sizeof(struct neper_snaps) + extent * (total + 1);
        struct neper_snaps *snaps = calloc(1, size);

        snaps->rusage = rusage;
        snaps->total  = total;
        snaps->extent = extent;
        snaps->ptr    = (char *)(snaps + 1);

        return snaps;
}
