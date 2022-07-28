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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpuinfo.h"
#include "logging.h"

static const char *ltrim(const char *str)
{
        while (isspace(*str))
                str++;
        return str;
}

static void rtrim(char *str)
{
        char *p = str + strlen(str) - 1;
        while (p >= str && isspace(*p))
                *p-- = 0;
}

int get_cpuinfo(struct cpuinfo *cpus, int max_cpus, struct callbacks *cb)
{
        FILE *f;
        int n = 0;
        char *key = NULL;
        char *value = NULL;
        int m = 0;

        f = fopen("/proc/cpuinfo", "r");
        if (!f || ferror(f))
                return -1;
        while (!feof(f) && n < max_cpus) {
                m = fscanf(f, "%m[^:]:%m[^\n]\n", &key, &value);
                /* Only parse out info that are useful to us
                 * specified in struct cpuinfo
                 */
                if (m == 2) {
                        rtrim(key);
#if defined(__powerpc__) || defined(__aarch64__) || defined(__riscv)
                        if (strcmp(ltrim(key), "processor") == 0) {
                                sscanf(value, "%d", &cpus[n].processor);
                                n++;
                        }
#else
                        /* this is x86 processor */
                        if (strcmp(ltrim(key), "processor") == 0)
                                sscanf(value, "%d", &cpus[n].processor);
                        else if (strcmp(ltrim(key), "physical id") == 0)
                                sscanf(value, "%d", &cpus[n].physical_id);
                        else if (strcmp(ltrim(key), "siblings") == 0)
                                sscanf(value, "%d", &cpus[n].siblings);
                        else if (strcmp(ltrim(key), "core id") == 0)
                                sscanf(value, "%d", &cpus[n].core_id);
                        else if (strcmp(ltrim(key), "cpu cores") == 0) {
                                sscanf(value, "%d", &cpus[n].cpu_cores);
                                n++;
                        }
#endif

                }
                if (key)
                        free(key);
                if (value)
                        free(value);

        }
        if (!feof(f) && n >= max_cpus)
                LOG_FATAL(cb, "max_cpus less than actual CPUs on machine");

        fclose(f);
        return n;
}
