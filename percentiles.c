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

#include "percentiles.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib.h"
#include "logging.h"

void percentiles_parse(const char *arg, void *out, struct callbacks *cb)
{
        struct percentiles *p = out;
        char *endptr;
        long val;

        while (true) {
                errno = 0;
                val = strtol(arg, &endptr, 10);
                if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) ||
                    (errno != 0 && val == 0))
                        PLOG_FATAL(cb, "strtol");
                if (endptr == arg)
                        break;
                if ((val < 0 || val > 100) && (val != 999) && (val != 9999))
                        LOG_FATAL(cb, "%ld percentile doesn't exist", val);
                switch (val) {
                case 999:
                        p->chosen[PER_INDEX_99_9] = true;
                        break;
                case 9999:
                        p->chosen[PER_INDEX_99_99] = true;
                        break;
                default:
                        p->chosen[val] = true;
                        break;
                }
                LOG_INFO(cb, "%ld percentile is chosen", val);
                if (*endptr == '\0')
                        break;
                arg = endptr + 1;
        }
}

void percentiles_print(const char *name, const void *var, struct callbacks *cb)
{
        const struct percentiles *p = var;
        char buf[10], s[400] = "";
        int i;

        for (i = 0; i <= 100; i++) {
                if (p->chosen[i]) {
                        sprintf(buf, "%d,", i);
                        strcat(s, buf);
                }
        }
        if (p->chosen[PER_INDEX_99_9])
                strcat(s, "99.9,");
        if (p->chosen[PER_INDEX_99_99])
                strcat(s, "99.99,");
        if (strlen(s) > 0)
                s[strlen(s) - 1] = '\0'; /* remove trailing comma */
        PRINT(cb, name, "%s", s);
}

bool percentiles_chosen(const struct percentiles *p, int percent)
{
        if (p)
                return p->chosen[percent];

        return false;
}

int percentiles_count(const struct percentiles *p)
{
        if (p) {
                int i, sum = 0;
                for (i = 0; i < PER_INDEX_COUNT; i++)
                        sum += p->chosen[i] ? 1 : 0;
                return sum;
        }

        return 0;
}
