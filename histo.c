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
static const double TICKS_TO_SEC = 1.0 / TIME_RESOLUTION;

struct neper_histo {
        const struct thread *thread;

        uint32_t num_buckets;  /* # of buckets allocated */
        uint8_t k_bits; /* resolution */
        uint16_t p_count; /* percentiles, cached for convenience */
        uint64_t sample_max_value;

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

        uint32_t all_max_index;
        uint32_t one_max_index;
        uint32_t cur_max_index;

        uint32_t all_min_index;
        uint32_t one_min_index;
        uint32_t cur_min_index;

        double *all_p_values;  /* % across all completed epochs */
        double *one_p_values;  /* % of the last completed epoch */

        bool first_all;   /* Is this the first call to all_percent() */
};

/* Conversion of a 64-bit value to an approximately logarithmic index
 * with k bits of resolution.
 * lr_bucket(n, k) computes the log2, followed by the k next significant bits.
 * lr_bucket_lo(b, k) returns the lower bound of bucket b.
 * Translate the index into the starting value for the corresponding interval.
 * Each power of 2 is mapped into N = 2**k intervals, each of size
 * S = 1 << ((index >> k) - 1), and starting at S * N.
 * The last k bits of index indicate which interval we want.
 *
 * For example, if k = 2 and index = 0b11011 (27) we have:
 * - N = 2**2 = 4;
 * - interval size S is 1 << ((0b11011 >> 2) - 1) = 1 << (6 - 1) = 32
 * - starting value is S * N = 128
 * - the last 2 bits 11 indicate the third interval so the
 *   starting value is 128 + 32*3 = 224
 */

#define fls64(x) ((x) == 0? 0 : (64 - __builtin_clzl(x)))
static int lr_bucket(uint64_t val, int k)
{
        const uint64_t mask = (1ul << k) - 1;
        const int bucket = fls64(val >> k);
        int slot = bucket == 0 ? val : ((bucket << k) | ((val >> (bucket - 1)) & mask) );
        return slot;
}

static uint64_t lr_bucket_lo(int index, int k)
{
        const uint32_t n = (1 << k), interval = index & (n - 1);
        if (index < n)
                return index;
        const uint32_t power_of_2 = (index >> k) - 1;
        return (1ul << power_of_2) * (n + interval);
}

static uint64_t lr_bucket_hi(int index, int k)
{
        return lr_bucket_lo(index + 1, k) - 1;
}

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
        const struct percentiles *pc = &impl->thread->opts->percentiles;
        double cent = impl->all_count / 100.0;
        int sub = 0, v = 0, i;

        if (!impl->first_all)
                return;
        impl->first_all = false;

        for (i = impl->all_min_index; i <= impl->all_max_index; i++) {
                sub += impl->all_buckets[i];
                while (v < pc->p_count && sub >= pc->p_th[v] * cent)
                        impl->all_p_values[v++] = lr_bucket_hi(i, impl->k_bits) * TICKS_TO_SEC;
        }
}

static void histo_one_finalize(struct neper_histo *impl)
{
        const struct percentiles *pc = &impl->thread->opts->percentiles;
        double cent = impl->one_count / 100.0;
        int sub = 0, v = 0, i;

        for (i = impl->one_min_index; i <= impl->one_max_index; i++) {
                int n = impl->cur_buckets[i];
                sub += n;
                impl->all_buckets[i] += n;
                impl->cur_buckets[i] = 0;
                while (v < pc->p_count && sub >= pc->p_th[v] * cent)
                        impl->one_p_values[v++] = lr_bucket_hi(i, impl->k_bits) * TICKS_TO_SEC;
        }
}

double neper_histo_percent(const struct neper_histo *impl, int index)
{
        return impl->one_p_values[index];
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
        desi->cur_min_index = MIN(desi->cur_min_index, srci->all_min_index);
        desi->cur_max_index = MAX(desi->cur_max_index, srci->all_max_index);

        int i;
        for (i = srci->all_min_index; i <= srci->all_max_index; i++)
                desi->cur_buckets[i] += srci->all_buckets[i];
}

void neper_histo_event(struct neper_histo *impl, double delta_s)
{
        int i;

        impl->cur_count++;
        impl->cur_sum  += delta_s;
        impl->cur_sum2 += delta_s * delta_s;

        impl->cur_min = MIN(impl->cur_min, delta_s);
        impl->cur_max = MAX(impl->cur_max, delta_s);

        delta_s *= TIME_RESOLUTION; /* convert to ticks, potential overflow */
        if (delta_s < 0 || delta_s > impl->sample_max_value) {
                LOG_ERROR(impl->thread->cb,
                          "%s(): not able to find bucket for delta_s %g",
                          __func__, delta_s / TIME_RESOLUTION);
                /* TODO: This will also cause an error in reporting 100% and
                 * high percentiles, because the sum of buckets will never
                 * reach the total count.
                 */
                return;
        }
        if (!impl->p_count)
                return;
        i = lr_bucket((uint64_t)delta_s, impl->k_bits);
        impl->cur_min_index = MIN(impl->cur_min_index, i);
        impl->cur_max_index = MAX(impl->cur_max_index, i);
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
        impl->cur_min = impl->sample_max_value;

        impl->all_max = MAX(impl->all_max, impl->cur_max);
        impl->one_max = impl->cur_max;
        impl->cur_max = 0;

        impl->all_min_index = MIN(impl->all_min_index, impl->cur_min_index);
        impl->one_min_index = impl->cur_min_index;
        impl->cur_min_index = impl->num_buckets - 1;

        impl->all_max_index = MAX(impl->all_max_index, impl->cur_max_index);
        impl->one_max_index = impl->cur_max_index;
        impl->cur_max_index = 0;

        histo_one_finalize(impl);
}

void neper_histo_print(struct neper_histo *histo)
{
        const struct thread *t = histo->thread;
        const struct percentiles *pc = &t->opts->percentiles;

        histo_all_finalize(histo);
        PRINT(t->cb, "latency_min", "%.9f", histo->all_min);
        PRINT(t->cb, "latency_max", "%.9f", histo->all_max);
        PRINT(t->cb, "latency_mean", "%.9f", histo->all_sum / histo->all_count);
        PRINT(t->cb, "latency_stddev", "%.9f",
              histo_stddev(histo->all_count, histo->all_sum, histo->all_sum2));

        for (int i = 0; i < pc->p_count; i++) {
                char key[32];
                sprintf(key, "latency_p%g", pc->p_th[i]);
                PRINT(t->cb, key, "%.9f", histo->all_p_values[i]);
        }
}

void neper_histo_delete(struct neper_histo *impl)
{
        if (impl)
                free(impl);
}

struct neper_histo *neper_histo_new(const struct thread *t, uint8_t k_bits)
{
        struct neper_histo *ret, histo = {};
        size_t memsize = sizeof(histo);

        if (k_bits > 10)
                k_bits = 10;
        histo.thread      = t;
        histo.k_bits      = k_bits;
        histo.num_buckets = 65 * (1 << k_bits);
        histo.sample_max_value = ~0ul;
        histo.first_all = true;
        histo.p_count = t->opts->percentiles.p_count;

        /* Allocate memory in one chunk */
        memsize += histo.num_buckets * 2 * sizeof(histo.all_buckets[0]);
        memsize += histo.p_count * 2 * sizeof(histo.all_p_values[0]);

        ret = calloc(1, memsize);
        *ret = histo;
        ret->all_buckets = (void *)(ret + 1);
        ret->cur_buckets = ret->all_buckets + ret->num_buckets;
        ret->all_p_values = (void *)(ret->cur_buckets + ret->num_buckets);
        ret->one_p_values = ret->all_p_values + ret->p_count;

        ret->all_min = ret->sample_max_value;
        ret->cur_min = ret->sample_max_value;
        ret->all_min_index = ret->num_buckets - 1;
        ret->cur_min_index = ret->num_buckets - 1;

        return ret;
}
