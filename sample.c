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

#include "sample.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flow.h"
#include "lib.h"
#include "logging.h"
#include "numlist.h"
#include "percentiles.h"

void add_sample(int tid, struct flow *flow, struct timespec *ts,
                struct sample **samples, struct callbacks *cb)
{
        struct sample *sample = calloc(1, sizeof(struct sample));
        sample->tid = tid;
        sample->flow_id = flow->id;
        sample->bytes_read = flow->bytes_read;
        sample->transactions = flow->transactions;
        sample->latency = flow->latency;
        flow->latency = numlist_create(cb);
        sample->timestamp = *ts;
        getrusage(RUSAGE_THREAD, &sample->rusage);
        sample->next = *samples;
        *samples = sample;
}

void print_sample(FILE *csv, struct percentiles *percentiles,
                  struct sample *sample)
{
        if (!sample) {
                fprintf(csv, "time,tid,flow_id,bytes_read,transactions");
                fprintf(csv, ",latency_min,latency_mean,latency_max");
                fprintf(csv, ",latency_stddev");
                if (percentiles) {
                        int i;
                        for (i = 0; i <= 100; i++) {
                                if (percentiles->chosen[i])
                                        fprintf(csv, ",latency_p%d", i);
                        }
                }
                fprintf(csv, ",utime,stime,maxrss,minflt,majflt,nvcsw,nivcsw");
                fprintf(csv, "\n");
                return;
        }
        fprintf(csv, "%ld.%09ld,%d,%d,%zd,%lu",
                sample->timestamp.tv_sec, sample->timestamp.tv_nsec,
                sample->tid, sample->flow_id, sample->bytes_read,
                sample->transactions);
        fprintf(csv, ",%f,%f,%f,%f",
                numlist_min(sample->latency), numlist_mean(sample->latency),
                numlist_max(sample->latency), numlist_stddev(sample->latency));
        if (percentiles) {
                int i;
                for (i = 0; i <= 100; i++) {
                        if (percentiles->chosen[i]) {
                                fprintf(csv, ",%f",
                                        numlist_percentile(sample->latency, i));
                        }
                }
        }
        fprintf(csv, ",%ld.%06ld,%ld.%06ld,%ld,%ld,%ld,%ld,%ld",
                sample->rusage.ru_utime.tv_sec, sample->rusage.ru_utime.tv_usec,
                sample->rusage.ru_stime.tv_sec, sample->rusage.ru_stime.tv_usec,
                sample->rusage.ru_maxrss,
                sample->rusage.ru_minflt, sample->rusage.ru_majflt,
                sample->rusage.ru_nvcsw, sample->rusage.ru_nivcsw);
        fprintf(csv, "\n");
}

void print_samples(struct percentiles *percentiles, struct sample *samples,
                   int num, const char *filename, struct callbacks *cb)
{
        FILE *csv = fopen(filename, "w");
        if (csv) {
                int i;
                LOG_INFO(cb, "successfully opened %s", filename);
                print_sample(csv, percentiles, NULL);
                for (i = 0; i < num; i++)
                        print_sample(csv, percentiles, &samples[i]);
                if (fclose(csv))
                        LOG_ERROR(cb, "fclose: %s", strerror(errno));
        } else {
                LOG_ERROR(cb, "fopen(%s): %s", filename, strerror(errno));
        }
}

int compare_samples(const void *a, const void *b)
{
        const struct sample *x = a, *y = b;
        if (x->timestamp.tv_sec < y->timestamp.tv_sec)
                return -1;
        if (x->timestamp.tv_sec > y->timestamp.tv_sec)
                return 1;
        if (x->timestamp.tv_nsec < y->timestamp.tv_nsec)
                return -1;
        if (x->timestamp.tv_nsec > y->timestamp.tv_nsec)
                return 1;
        return 0;
}

void free_samples(struct sample *samples)
{
        struct sample *sample, *next;

        sample = samples;
        while (sample) {
                numlist_destroy(sample->latency);
                next = sample->next;
                free(sample);
                sample = next;
        }
}
