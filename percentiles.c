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

static int my_dsort(const void *p1, const void *p2)
{
        const double a = *(double *)p1, b = *(double *)p2;

        return a < b ? -1 : (a > b ? 1 : 0);
}

void percentiles_parse(const char *arg, void *out, struct callbacks *cb)
{
        struct percentiles *p = out;
        char *endptr;
        int sz = 0;
        double d;

        while (arg) {
                errno = 0;
                d = strtod(arg, &endptr);
                /* backward compatibility */
                if (d == 999)
                        d = 99.9;
                else if (d == 9999)
                        d = 99.99;
                if (errno || d < 0 || d > 100 || endptr == arg)
                        LOG_FATAL(cb, "invalid -p argument %s", arg);

                if (p->p_count >= sz) {
                        sz = 2 * sz + 2;
                        p->p_th = realloc(p->p_th, sz * sizeof(double));
                        if (!p->p_th)
                                LOG_FATAL(cb, "cannot allocate %d entries", sz);
                }
                p->p_th[p->p_count++] = d;
                LOG_INFO(cb, "%g percentile is chosen", d);
                if (*endptr == '\0')
                        break;
                arg = endptr + 1;
        }
        if (!p->p_count)
                return;
        qsort(p->p_th, p->p_count, sizeof(double), my_dsort);
        /* remove duplicates */
        int i, cur = 0;
        for (i = 1; i < p->p_count; i++) {
                if (p->p_th[cur] == p->p_th[i])
                        LOG_INFO(cb, "remove duplicate percentile %g", p->p_th[i]);
                else
                        p->p_th[++cur] = p->p_th[i];
        }
        p->p_count = cur;
}

void percentiles_print(const char *name, const void *var, struct callbacks *cb)
{
        const struct percentiles *p = var;
        char buf, *s;
        int i, len = 0;

        /* first pass, compute length */
        for (i = 0; i < p->p_count; i++)
                len += snprintf(&buf, 0, "%g,", p->p_th[i]);

        /* second pass, create string */
        s = calloc(1, len + 1);
        len = 0;
        for (i = 0; i < p->p_count; i++)
                len += sprintf(s + len, "%g,", p->p_th[i]);
        PRINT(cb, name, "%s", s);
        free(s);
}
