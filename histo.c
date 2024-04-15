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

#include "histo.h"
#include "common.h"
#include "thread.h"

#include <float.h>

// use 0.01 us time resolution
static const int TIME_RESOLUTION = 100 * 1000000;

struct neper_histo {
        const struct thread *thread;

        int num_buckets;  /* # of buckets allocated */
        int *ceil;        /* Max value that can be hashed into each bucket */

        uint64_t *all_buckets;
        uint64_t *cur_buckets;

        uint64_t all_count;
        uint64_t one_count;
        uint64_t cur_count;

        double all_sum;
        double one_sum;
        double cur_sum;

        long double all_sum2;
        long double one_sum2;
        long double cur_sum2;

        double all_min;
        double one_min;
        double cur_min;

        double all_max;
        double one_max;
        double cur_max;

        int all_percent[PER_INDEX_COUNT];  /* % across all completed epochs */
        int one_percent[PER_INDEX_COUNT];  /* % of the last completed epoch */

        bool first_all;   /* Is this the first call to all_percent() */
};

struct neper_histo_factory {
        const struct thread *thread;

        int num_buckets;  /* # of buckets allocated */
        int *ceil;        /* Max value that can be hashed into each bucket */
};

double neper_histo_min(const struct neper_histo *histo)
{
        return histo->one_min;
}

double neper_histo_max(const struct neper_histo *histo)
{
        return histo->one_max;
}

double neper_histo_mean(const struct neper_histo *histo)
{
        return histo->one_sum / histo->one_count;
}

static double histo_stddev(long double N, long double S, long double Q)
{
        return sqrt(N*Q - S*S) / N;
}

double neper_histo_stddev(const struct neper_histo *histo)
{
        return histo_stddev(histo->one_count, histo->one_sum, histo->one_sum2);
}

static void histo_all_finalize(struct neper_histo *impl)
{
        double cent = impl->all_count / 100.0;
        double nnn  = (impl->all_count * 99.9) / 100.0;
        double nnnn = (impl->all_count * 99.99) / 100.0;
        int sub = 0;
        int p = 1;
        int i;

        if (!impl->first_all)
                return;
        impl->first_all = false;

        for (i = 0; i < impl->num_buckets; i++) {
                sub += impl->all_buckets[i];
                while (p < 100 && p * cent <= sub)
                        impl->all_percent[p++] = impl->ceil[i];
                if (p == 100) {
                        if (nnn <= sub) {
                                int c = impl->ceil[i];
                                impl->all_percent[PER_INDEX_99_9] = c;
                                p++;
                        }
                }
                if (p == 101) {
                        if (nnnn <= sub) {
                                int c = impl->ceil[i];
                                impl->all_percent[PER_INDEX_99_99] = c;
                                p++;
                        }
                }
        }
}

static void histo_one_finalize(struct neper_histo *impl)
{
        double cent = impl->one_count / 100.0;
        double nnn  = (impl->one_count * 99.9) / 100.0;
        double nnnn = (impl->one_count * 99.99) / 100.0;
        int sub = 0;
        int p = 1;
        int i;

        for (i = 0; i < impl->num_buckets; i++) {
                int n = impl->cur_buckets[i];
                sub += n;
                while (p < 100 && p * cent <= sub)
                        impl->one_percent[p++] = impl->ceil[i];
                if (p == 100) {
                        if (nnn <= sub) {
                                int c = impl->ceil[i];
                                impl->one_percent[PER_INDEX_99_9] = c;
                                p++;
                        }
                }
                if (p == 101) {
                        if (nnnn <= sub) {
                                int c = impl->ceil[i];
                                impl->one_percent[PER_INDEX_99_99] = c;
                                p++;
                        }
                }
                impl->all_buckets[i] += n;
                impl->cur_buckets[i] = 0;
        }
}

static double histo_all_percent(struct neper_histo *impl, int percentage)
{
        histo_all_finalize(impl);

        switch (percentage) {
        case 0:
                return impl->all_min;
        case 100:
                return impl->all_max;
        case 999:
                return (double)impl->all_percent[PER_INDEX_99_9] /
                       TIME_RESOLUTION;
        case 9999:
                return (double)impl->all_percent[PER_INDEX_99_99] /
                       TIME_RESOLUTION;
        default:
                return (double)impl->all_percent[percentage] /
                       TIME_RESOLUTION;
        }
}

double neper_histo_percent(const struct neper_histo *impl, int percentage)
{
        switch (percentage) {
        case 0:
                return impl->one_min;
        case 100:
                return impl->one_max;
        case 999:
                return (double)impl->one_percent[PER_INDEX_99_9] /
                       TIME_RESOLUTION;
        case 9999:
                return (double)impl->one_percent[PER_INDEX_99_99] /
                       TIME_RESOLUTION;
        default:
                return (double)impl->one_percent[percentage] /
                       TIME_RESOLUTION;
        }
}

uint64_t neper_histo_samples(const struct neper_histo *histo)
{
        return histo->all_count;
}

void neper_histo_add(struct neper_histo *desi, const struct neper_histo *srci)
{
        desi->cur_count += srci->all_count;
        desi->cur_sum   += srci->all_sum;
        desi->cur_sum2  += srci->all_sum2;

        desi->cur_min = MIN(desi->cur_min, srci->all_min);
        desi->cur_max = MAX(desi->cur_max, srci->all_max);

        int i;
        for (i = 0; i < desi->num_buckets; i++)
                desi->cur_buckets[i] += srci->all_buckets[i];
}

// binary search for the correct bucket index
static int histo_find_bucket_idx(struct neper_histo *impl, int ticks)
{
        int l_idx = 0;
        int r_idx = impl->num_buckets - 1;

        if (ticks > impl->ceil[r_idx])
                return r_idx;

        while (l_idx <= r_idx) {
                int idx = (l_idx + r_idx) / 2;
                if (impl->ceil[idx] < ticks) {
                        l_idx = idx + 1;
                } else {
                        if (idx == 0)
                                return idx;
                        else if (impl->ceil[idx -1] < ticks)
                                return idx;
                        else
                                r_idx = idx - 1;
                }
        }

        return -1;
}

void neper_histo_event(struct neper_histo *impl, double delta_s)
{
        int ticks = delta_s * TIME_RESOLUTION;
        int i;

        impl->cur_count++;
        impl->cur_sum  += delta_s;
        impl->cur_sum2 += delta_s * delta_s;

        impl->cur_min = MIN(impl->cur_min, delta_s);
        impl->cur_max = MAX(impl->cur_max, delta_s);

        i = histo_find_bucket_idx(impl, ticks);
        if (i == -1) {
                LOG_ERROR(impl->thread->cb,
                          "%s(): not able to find bucket for ticks %d",
                          __func__, ticks);
                return;
        }
        impl->cur_buckets[i]++;
}

void neper_histo_epoch(struct neper_histo *impl)
{
        impl->all_count += impl->cur_count;
        impl->one_count  = impl->cur_count;
        impl->cur_count  = 0;

        impl->all_sum += impl->cur_sum;
        impl->one_sum  = impl->cur_sum;
        impl->cur_sum  = 0;

        impl->all_sum2 += impl->cur_sum2;
        impl->one_sum2  = impl->cur_sum2;
        impl->cur_sum2  = 0;

        impl->all_min = MIN(impl->all_min, impl->cur_min);
        impl->one_min = impl->cur_min;
        impl->cur_min = DBL_MAX;

        impl->all_max = MAX(impl->all_max, impl->cur_max);
        impl->one_max = impl->cur_max;
        impl->cur_max = 0;

        histo_one_finalize(impl);
}

/*
 * Returns the size of the hash table needed for the given parameters.
 * If 'table' and 'ceil' are non-null then populate them as well.
 *
 * 'table' maps an incoming value to a bucket so we can do an O(1) lookup.
 * 'ceils' tracks the maximum value stored in each bucket.
 *
 * The delta between each bucket increases exponentially and is stored as a
 * double. However, it is rounded down to the nearest integer when used. So
 * for example, with a growth rate of 1.02, the delta between the first and
 * second buckets will be 1.02, rounded down to 1. The delta between the
 * second and third buckets will be 1.02^2 ~= 1.04, which also rounds down to 1.
 * Eventually the delta will climb above 2 and that will become the new value.
 */

static void histo_hash(int num_buckets, double growth, int *ceils)
{
        double delta = 1.0;
        int ceil = 1;
        int hash = 0;

        while (hash < num_buckets) {
                ceils[hash] = ceil;
                delta *= growth;
                ceil += (int)delta;
                hash++;
        }
}

void neper_histo_print(struct neper_histo *histo)
{
        const struct thread *t = histo->thread;
        const struct options *opts = t->opts;

        PRINT(t->cb, "latency_min", "%.9f", histo->all_min);
        PRINT(t->cb, "latency_max", "%.9f", histo->all_max);
        PRINT(t->cb, "latency_mean", "%.9f", histo->all_sum / histo->all_count);
        PRINT(t->cb, "latency_stddev", "%.9f",
              histo_stddev(histo->all_count, histo->all_sum, histo->all_sum2));

        int i;
        for (i = 0; i < 100; i++)
                if (percentiles_chosen(&opts->percentiles, i)) {
                        char key[13];
                        sprintf(key, "latency_p%d", i);
                        PRINT(t->cb, key, "%.9f", histo_all_percent(histo, i));
                }
        if (percentiles_chosen(&opts->percentiles, PER_INDEX_99_9))
          PRINT(t->cb, "latency_p99.9", "%.9f",
                histo_all_percent(histo, PER_INDEX_99_9));
        if (percentiles_chosen(&opts->percentiles, PER_INDEX_99_99))
          PRINT(t->cb, "latency_p99.99", "%.9f",
                histo_all_percent(histo, PER_INDEX_99_99));
}

void neper_histo_delete(struct neper_histo *impl)
{
        if (impl) {
                free(impl->all_buckets);
                free(impl->cur_buckets);
                free(impl->ceil);
                free(impl);
        }
}

struct neper_histo *neper_histo_new( const struct neper_histo_factory *fimpl)
{

        struct neper_histo *impl = calloc(1, sizeof(*impl));

        impl->thread      = fimpl->thread;
        impl->num_buckets = fimpl->num_buckets;
        impl->ceil        = fimpl->ceil;

        impl->all_buckets = calloc(fimpl->num_buckets, sizeof(uint64_t));
        impl->cur_buckets = calloc(fimpl->num_buckets, sizeof(uint64_t));

        impl->all_min = DBL_MAX;
        impl->cur_min = DBL_MAX;

        impl->first_all = true;

        return impl;
}

void neper_histo_factory_delete(struct neper_histo_factory *impl)
{
        if (impl) {
                free(impl->ceil);
                free(impl);
        }
}

struct neper_histo_factory *neper_histo_factory_new(const struct thread *t,
                                                int num_buckets, double growth)
{
        struct neper_histo_factory *impl = calloc(1, sizeof(*impl));

        impl->thread      = t;
        impl->num_buckets = num_buckets;
        impl->ceil        = calloc(impl->num_buckets, sizeof(int));

        histo_hash(num_buckets, growth, impl->ceil);

        return impl;
}
