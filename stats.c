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

#include "stats.h"
#include "coef.h"
#include "common.h"
#include "flow.h"
#include "histo.h"
#include "pq.h"
#include "print.h"
#include "rusage.h"
#include "snaps.h"
#include "thread.h"

/*
 * Quick taxonomy:
 *
 * stat_impl implements (and overlays with) the neper_stat struct.
 *
 * neper_snaps is the container for neper_snap structs.
 *
 * neper_snap is a single performance event, containing an event count plus
 * timing and system information.
 *
 * neper_histo implements latency analysis, which is used (only) by rr tests.
 */

struct stat_impl {
        struct neper_stat stat;  /* This must be the first field. */

        struct neper_snaps *snaps;  /* snapshot container */
        struct neper_histo *histo;  /* histogram stats (optional) */
        int thread_index;           /* which thread */
        int flow_index;             /* which flow within the thread */
        struct stat_impl *next;

        uint64_t things;     /* The total of whatever we are measuring */
        uint64_t scratch;    /* per-flow scratch space for final stats */
};

double neper_stat_cmp(void *aptr, void *bptr)
{
        const struct stat_impl *a = aptr;
        const struct stat_impl *b = bptr;
        return neper_snaps_cmp(a->snaps, b->snaps);
}

static const struct neper_snaps *stat_snaps(const struct neper_stat *stat)
{
        const struct stat_impl *impl = (void *)stat;
        return impl->snaps;
}

static struct neper_histo *stat_histo(const struct neper_stat *stat)
{
        const struct stat_impl *impl = (void *)stat;
        return impl->histo;
}

/*
 * Logs a new statistics event for the current interval and determines
 * whether it is time to create a new snapshot.
 */

static void stat_event(struct thread *t, struct neper_stat *stat, int things,
                       bool force, void (*fn)(struct thread *, 
                                              struct neper_stat *,
                                              struct neper_snap *))
{
        struct stat_impl *impl = (void *)stat;
        struct neper_snaps *snaps = impl->snaps;

        struct timespec now;
        common_gettime(&now);

        if (!impl->things && !t->stop)
                thread_time_start(t, &now);

        double elapsed = seconds_between(t->time_start, &now);

        impl->things += things;

        int i = neper_snaps_count(snaps);

        double threshold = t->opts->interval * (i + 1);

        /* Always record the first event, to capture the start time. */
        if (elapsed >= threshold || !i || force) {
                struct neper_snap *snap = neper_snaps_add(snaps, &now, impl->things);

                if (fn)
                        fn(t, stat, snap);

                if (!i)
                        t->stats->insert(t->stats, stat);
        }
}

struct neper_coef *neper_stat_print(struct thread *ts, FILE *csv,
        void (*fn)(struct thread *t, int, const struct neper_snap *, FILE *))
{
        struct neper_pq *pq = thread_stats_pq(ts);
        if (!pq)
                return NULL;

        struct neper_coef *coef = neper_coef();
        uint64_t current_total = 0;
        struct neper_stat *stat;
        uint64_t prev_total = 0;
        const struct timespec *prev_ts;

        while ((stat = pq->deq(pq))) {
                struct stat_impl *impl = (void *)stat;
                struct neper_snaps *snaps = impl->snaps;
                const struct neper_snap *snap = neper_snaps_iter_next(snaps);

                current_total += snap->things - impl->scratch;
                impl->scratch = snap->things;
                if (csv) {
                        struct thread *t = &ts[impl->thread_index];
                        double raw_thruput = 0;

                        fprintf(csv, "%d,%d,", t->index, impl->flow_index);

                        if (prev_total) {
                                double delta = current_total - prev_total;
                                double seconds = seconds_between(prev_ts, &snap->timespec);

                                if (seconds)
                                        raw_thruput = delta / seconds;
                                else
                                        raw_thruput = 0;
                        }

                        prev_total = current_total;
                        prev_ts = &snap->timespec;
                        if (fn) {
                                neper_snap_print(snap, csv, raw_thruput, "");
                                fn(t, impl->flow_index, snap, csv);
                        } else {
                                neper_snap_print(snap, csv, raw_thruput, "\n");
                        }
                }

                coef->event(coef, &snap->timespec, current_total);

                if (!neper_snaps_iter_done(snaps))
                        pq->enq(pq, stat);
        }

        pq->fini(pq);

        const struct timespec *time = coef->end(coef);
        struct callbacks *cb = ts[0].cb;

        PRINT(cb, "time_end", "%ld.%09ld", time->tv_sec, time->tv_nsec);
        PRINT(cb, "correlation_coefficient", "%.2f", coef->correlation(coef));

        return coef;
}

struct neper_stat *neper_stat_init(struct flow *f, struct neper_histo *histo,
                                   int extra)
{
        struct thread *t = flow_thread(f);
        const struct options *opts = t->opts;
        int n;
        // test_length, >0 seconds, <0 transactions.
        if (opts->test_length < 0)
                n = -opts->test_length + 1;
        else
                n = (opts->test_length / opts->interval) + 1;
        struct stat_impl *impl =
                calloc_or_die(1, sizeof(struct stat_impl), t->cb);
        struct neper_stat *stat = &impl->stat;

        stat->histo = stat_histo;
        stat->snaps = stat_snaps;
        stat->event = stat_event;

        impl->snaps = neper_snaps_init(t->rusage, n, extra);
        impl->histo = histo;
        impl->flow_index  = flow_id(f);
        impl->thread_index = t->index;

        return stat;
}

static void stat_delete(struct neper_stat *stat)
{
        struct stat_impl *impl = (void *)stat;

        if (impl) {
                if (impl->histo)
                        neper_histo_delete(impl->histo);
                free(impl->snaps);   /* TODO: Add a destructor */
                free(impl);
        }
}

/******************************************************************************/

struct stats_impl {
        struct neper_stats si_stats;

        struct neper_stat *si_stat;
        int si_count;
};

static int fn_snaps(struct neper_stat *stat, void *unused)
{
        struct stat_impl *impl = (void *)stat;
        const struct neper_snaps *snaps = impl->snaps;

        return neper_snaps_count(snaps);
}

static void stats_container_insert(struct neper_stats *stats,
                                   struct neper_stat *stat)
{
        struct stats_impl *stats_impl = (void *)stats;
        struct stat_impl *stat_impl = (void *)stat;

        stat_impl->next = (void *)stats_impl->si_stat;
        stats_impl->si_stat = stat;
        stats_impl->si_count++;
}

static int stats_container_flows(struct neper_stats *stats)
{
        struct stats_impl *stats_impl = (void *)stats;

        return stats_impl->si_count;
}

static int stats_container_sumforeach(struct neper_stats *stats,
                                      int (*fn)(struct neper_stat *, void *),
                                      void *ptr)
{
        struct stats_impl *stats_impl = (void *)stats;
        struct stat_impl *stat_impl;
        int sum = 0;

        for (stat_impl = (void *)stats_impl->si_stat;
                         stat_impl;
                         stat_impl = stat_impl->next)
                sum += fn(&stat_impl->stat, ptr);

        return sum;
}

static int stats_container_snaps(struct neper_stats *stats)
{
        if (stats)
                return stats_container_sumforeach(stats, fn_snaps, NULL);
        return 0;
}

static void stats_container_fini(struct neper_stats *stats)
{
        if (stats)
                stats_container_sumforeach(stats, (void *)stat_delete, NULL);
}

struct neper_stats *neper_stats_init(struct callbacks *cb)
{
        struct stats_impl *stats_impl =
                calloc_or_die(1, sizeof(struct stats_impl), cb);
        struct neper_stats *stats = &stats_impl->si_stats;

        stats->insert     = stats_container_insert;
        stats->flows      = stats_container_flows;
        stats->snaps      = stats_container_snaps;
        stats->sumforeach = stats_container_sumforeach;
        stats->fini       = stats_container_fini;

        return stats;
}
