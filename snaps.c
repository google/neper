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

struct snaps_impl {
        struct neper_snaps snaps;

        struct neper_rusage *rusage;
        int total;   /* # of snap structs allocated */
        int count;   /* # of populated snap structs */
        int iter;    /* iterator bookmark */
        int extent;  /* extended size of the snap struct */
        void *ptr;   /* storage */
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

void neper_snap_print(const struct neper_snap *snap, FILE *csv, const char *nl)
{
        fprintf(csv, "%ld.%09ld,%zd",
                snap->timespec.tv_sec, snap->timespec.tv_nsec, snap->things);
        print_rusage(csv, snap->rusage, nl);
}

static struct neper_snap *snaps_get(const struct neper_snaps *snaps, int i)
{
        struct snaps_impl *impl = (void *)snaps;
        char *ptr = impl->ptr;

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
                return (void *)(ptr + impl->total * impl->extent);
        }

        return (void *)(ptr + i * impl->extent);
}

/* Compare two containers by comparing the current iterator objects for each. */

double neper_snaps_cmp(const struct neper_snaps *aptr,
                       const struct neper_snaps *bptr)
{
        const struct snaps_impl *a = (void *)aptr;
        const struct snaps_impl *b = (void *)bptr;
        const struct neper_snap *sa = snaps_get(&a->snaps, a->iter);
        const struct neper_snap *sb = snaps_get(&b->snaps, b->iter);

        return neper_snap_cmp(sa, sb);
}

static struct neper_snap *snaps_add(struct neper_snaps *snaps,
                                    const struct timespec *now, uint64_t things)
{
        struct snaps_impl *impl = (void *)snaps;
        struct neper_snap *snap = snaps_get(snaps, impl->count++);

        snap->timespec = *now;
        snap->rusage   = impl->rusage->get(impl->rusage, now);
        snap->things   = things;

        return snap;
}

static int snaps_count(const struct neper_snaps *snaps)
{
        const struct snaps_impl *impl = (void *)snaps;

        return impl->count;
}

static const struct neper_snap *snaps_iter_next(struct neper_snaps *snaps)
{
        struct snaps_impl *impl = (void *)snaps;

        int i = impl->iter++;
        return (i < impl->count) ? snaps_get(snaps, i) : NULL;
}

static bool snaps_iter_done(const struct neper_snaps *snaps)
{
        const struct snaps_impl *impl = (void *)snaps;

        return (impl->iter >= impl->count);
}

struct neper_snaps *neper_snaps_init(struct neper_rusage *rusage,
                                     int total, int extra)
{
        struct snaps_impl *impl = calloc(1, sizeof(struct snaps_impl));
        struct neper_snaps *snaps = &impl->snaps;

        impl->rusage = rusage;
        impl->total  = total;
        impl->count  = 0;
        impl->iter   = 0;
        impl->extent = sizeof(struct neper_snap) + extra;
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
        impl->ptr    = calloc(total + 1, impl->extent);

        snaps->add   = snaps_add;
        snaps->count = snaps_count;
        snaps->iter_next = snaps_iter_next;
        snaps->iter_done = snaps_iter_done;

        return snaps;
}
