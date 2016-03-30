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

int get_cpuinfo(struct cpuinfo *cpus, int max_cpus)
{
        FILE *f;
        int n = 0;
        char *key, *value;

        f = fopen("/proc/cpuinfo", "r");
        if (!f)
                return -1;
        while (n < max_cpus) {
                while (fscanf(f, "%m[^:]:%m[^\n]\n", &key, &value) == 2) {
                        rtrim(key);
                        if (strcmp(ltrim(key), "processor") == 0)
                                sscanf(value, "%d", &cpus[n].processor);
                        else if (strcmp(ltrim(key), "physical id") == 0)
                                sscanf(value, "%d", &cpus[n].physical_id);
                        else if (strcmp(ltrim(key), "siblings") == 0)
                                sscanf(value, "%d", &cpus[n].siblings);
                        else if (strcmp(ltrim(key), "core id") == 0)
                                sscanf(value, "%d", &cpus[n].core_id);
                        else if (strcmp(ltrim(key), "cpu cores") == 0)
                                sscanf(value, "%d", &cpus[n].cpu_cores);
                        free(key);
                        free(value);
                }
                if (ferror(f))
                        return -1;
                if (feof(f))
                        break;
                n++;
        }
        fclose(f);
        return n;
}
