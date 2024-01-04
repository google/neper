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

#include "print.h"

#include "common.h"
#include "percentiles.h"
#include "rusage.h"
#include "thread.h"

FILE *print_header(const char *path, const char *things, const char *nl,
                   struct callbacks *cb)
{
        FILE *csv = NULL;

        if (path) {
                csv = fopen(path, "w");
                if (csv)
                        LOG_INFO(cb, "successfully opened %s", path);
                else
                        LOG_ERROR(cb, "fopen(%s): %s", path, strerror(errno));
        }

        if (csv) {
                fprintf(csv, "tid,flow_id,time,%s", things);
                fprintf(csv, ",utime,stime,maxrss,minflt,majflt,nvcsw,nivcsw");
                fprintf(csv, "%s", nl);
        }

        return csv;
}

void print_latency_header(FILE *csv, const struct percentiles *pc)
{
        fprintf(csv, ",latency_min,latency_mean,latency_max,latency_stddev");

        for (int i = 0; i < pc->p_count; i++)
                fprintf(csv, ",latency_p%g", pc->p_th[i]);
        fprintf(csv, "\n");
}

void print_rusage(FILE *csv, const struct rusage *rusage, const char *nl)
{
        fprintf(csv, ",%ld.%06ld,%ld.%06ld,%ld,%ld,%ld,%ld,%ld",
                rusage->ru_utime.tv_sec, rusage->ru_utime.tv_usec,
                rusage->ru_stime.tv_sec, rusage->ru_stime.tv_usec,
                rusage->ru_maxrss,
                rusage->ru_minflt, rusage->ru_majflt,
                rusage->ru_nvcsw, rusage->ru_nivcsw);
        fprintf(csv, "%s", nl);
}
