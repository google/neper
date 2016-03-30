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

#include "interval.h"
#include <math.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "common.h"
#include "flow.h"
#include "sample.h"
#include "thread.h"

struct interval {
        double seconds;
        struct timespec *time_start;
        pthread_mutex_t *time_start_mutex;
        struct rusage *rusage_start;
        struct timespec last_time;
};

static inline void set_uninitialized(struct timespec *ts)
{
        ts->tv_sec = ts->tv_nsec = 0;
}

static inline int is_uninitialized(struct timespec *ts)
{
        return ts->tv_sec == 0 && ts->tv_nsec == 0;
}

static void ensure_initialized(struct interval *itv, struct timespec now)
{
        if (is_uninitialized(&itv->last_time)) {
                pthread_mutex_lock(itv->time_start_mutex);
                if (is_uninitialized(itv->time_start)) {
                        getrusage(RUSAGE_SELF, itv->rusage_start);
                        *itv->time_start = now;
                }
                pthread_mutex_unlock(itv->time_start_mutex);
                itv->last_time = *itv->time_start;
        }
}

struct interval *interval_create(double interval_in_seconds, struct thread *t)
{
        struct interval *itv;

        itv = malloc(sizeof(*itv));
        if (!itv)
                PLOG_FATAL(t->cb, "malloc");
        itv->seconds = interval_in_seconds;
        itv->time_start = t->time_start;
        itv->time_start_mutex = t->time_start_mutex;
        itv->rusage_start = t->rusage_start;
        set_uninitialized(&itv->last_time);
        return itv;
}

static inline double to_seconds(struct timespec a)
{
        return a.tv_sec + a.tv_nsec * 1e-9;
}

static void get_next_time(struct interval *itv, double duration)
{
        double new_time, int_part, frac_part;

        new_time = to_seconds(itv->last_time);
        new_time += floor(duration / itv->seconds) * itv->seconds;
        frac_part = modf(new_time, &int_part);
        itv->last_time.tv_sec = int_part;
        itv->last_time.tv_nsec = frac_part * 1e9;
}

void interval_collect(struct flow *flow, struct thread *t)
{
        struct interval *itv = flow->itv;
        struct timespec now;
        double duration;

        clock_gettime(CLOCK_MONOTONIC, &now);
        ensure_initialized(itv, now);
        duration = seconds_between(&itv->last_time, &now);
        if (duration < itv->seconds)
                return;
        add_sample(t->index, flow, &now, &t->samples, t->cb);
        get_next_time(itv, duration);
}

void interval_destroy(struct interval *itv)
{
        if (itv)
                free(itv);
}
