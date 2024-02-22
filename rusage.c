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

#include "rusage.h"
#include "common.h"

struct rusage_node {
        struct rusage_node *rn_next;
        struct rusage rn_rusage;
};

struct rusage_impl {
        struct neper_rusage ri_rusage;

        struct rusage_node *ri_node;
        struct timespec ri_timespec;
        double ri_interval;
};

/* The following routines are used to extend getrusage to include /proc/stat */

static double get_aggregate_proc_stat_system_time()
{
        /* Get the aggreagte system time by scraping /proc/stat */
        unsigned long dummy, system, irq, softirq;
        int nscan = 0;
        FILE *f = fopen("/proc/stat", "r");

        if (!f)
        {
                fprintf(stderr, "Unable to open /proc/stat!\n");
                exit(1);
        }

        /* Just keep the important values of the first seven */
        nscan = fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu",
                       &dummy, &dummy, &system, &dummy, &dummy, &irq, &softirq);
        fclose(f); // Ensure file is closed in all cases

        if (nscan != 7)
        {
                fprintf(stderr, "IO or parser error while reading /proc/stat!\n");
                exit(1);
        }
        /* Return the total system time in seconds */
        return ((double)(system + irq + softirq) / (double)sysconf(_SC_CLK_TCK));
}

static void secs_to_timeval(double secs, struct timeval *tv)
{
        /* Split into integer seconds and microseconds (with rounding). */
        tv->tv_sec  = secs;
        tv->tv_usec = (secs - (double)tv->tv_sec) * 1000000.0 + 0.5;
}

/* The following global values establish an environment from which the */
/* enhanced getrusage function and execute. By saving them once globally */
/* we avoid having to pass the extra getrusage options from all call sites */
static bool option_getrusage_stime_use_proc = 0;
static int  option_getrusage_num_threads    = 0;
static double getrusage_initial_sys_time    = 0.0;

void set_getrusage_enhanced(bool stime_use_proc, int num_threads)
{
        /* Set the options to use in later getrusage calls */
        /* Must be called once before main code starts timing */
        /* num_threads must be greater than 0 */

        option_getrusage_stime_use_proc = stime_use_proc;
        if (num_threads <= 0) {
                fprintf(stderr,
                        "set_getrusage_enhanced options 0 >= num_threads = %i\n",
                        num_threads);
                exit(1);
        }
        option_getrusage_num_threads = num_threads;

        /* Capture the initial system time to use to offset the later times */
        /* to start at zero. This attempts to duplicate a behavior that the */
        /* original neper codes relied on */
        getrusage_initial_sys_time = get_aggregate_proc_stat_system_time();
}

int getrusage_enhanced(int who, struct rusage *usage)
{
        /* Enhanced version of getrusage that optionally improves the */
        /* system time estimate using values scraped from /proc/stat  */

        /* Get the base rusage values for this thread */
        int retval = getrusage(who, usage);

        if(retval != 0) {
                /* TODO:(winget) Add appropriate error checking on all getrusage calls */
                fprintf(stderr, "getrusage returned %i\n", retval);
        }

        if(option_getrusage_stime_use_proc) {
                if (option_getrusage_num_threads == 0) {
                        /* The options were never initialized, can't continue */
                        fprintf(stderr, "getrusage_enhanced num_threads not set\n");
                        exit(1);
                }

                /* Get the /proc/stat aggreagte system time for all CPUs */
                double sys = get_aggregate_proc_stat_system_time() -
                             getrusage_initial_sys_time;

                if (who != RUSAGE_SELF) {
                        /* Allocate only "this" threads portion to this return value */
                        /* This assumes that the main process will call with "SELF"  */
                        /* while threads will consistently call with THREAD and then */
                        /* sum up the results                                        */
                        sys = sys / (double) option_getrusage_num_threads;
                }
                /* Update the system timeval to new aggreagte value */
                secs_to_timeval(sys, &usage->ru_stime);
        }
        return(retval);
}


static struct rusage *rusage_get(struct neper_rusage *rusage, const struct timespec *now)
{
        struct rusage_impl *impl = (void *)rusage;
        struct rusage_node *node = impl->ri_node;

        if (seconds_between(&impl->ri_timespec, now) >= impl->ri_interval) {
                node = malloc(sizeof(struct rusage_node));
                node->rn_next = impl->ri_node;
                getrusage_enhanced(RUSAGE_THREAD, &node->rn_rusage);
                impl->ri_node = node;
                impl->ri_timespec = *now;
        }

        return &node->rn_rusage;
}

static void rusage_fini(struct neper_rusage *rusage)
{
        struct rusage_impl *impl = (void *)rusage;
        if (impl) {
                struct rusage_node *node = impl->ri_node;
                while (node) {
                        struct rusage_node *temp = node->rn_next;
                        free(node);
                        node = temp;
                }
                free(impl);
        }
}

struct neper_rusage *neper_rusage(double interval)
{
        struct rusage_impl *impl = calloc(1, sizeof(struct rusage_impl));
        struct neper_rusage *rusage = &impl->ri_rusage;

        rusage->get  = rusage_get;
        rusage->fini = rusage_fini;

        impl->ri_interval = interval;

        return rusage;
}
