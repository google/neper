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

#include <stdlib.h>
#include <time.h>

#include "common.h"
#include "parse.h"

void parse_all_samples(const char *arg, void *out, struct callbacks *cb)
{
        if (arg)
                *(const char **)out = arg;
        else
                *(const char **)out = "samples.csv";
}

void parse_log_rtt(const char *arg, void *out, struct callbacks *cb)
{
        if (arg)
                *(const char **)out = arg;
        else
                *(const char **)out = "rtt_samples.txt";
}

static long long parse_rate(const char *str, struct callbacks *cb)
{
        const struct rate_conversion *conv;
        char *suffix;
        double val;

        errno = 0;
        val = strtod(str, &suffix);
        if ((errno == ERANGE && (val == HUGE_VAL || val == -HUGE_VAL)) ||
            (errno != 0 && val == 0))
                PLOG_FATAL(cb, "strtod");
        if (suffix == str)
                LOG_FATAL(cb, "no digits were found");
        if (suffix[0] == '\0')
                return val;
        for (conv = conversions; conv->unit; conv++) {
                if (strncmp(suffix, conv->str, strlen(conv->str)) == 0)
                        return val * conv->bytes_per_second;
        }
        LOG_FATAL(cb, "invalid rate suffix `%s'", suffix);
        return 0;  /* unreachable */
}

void parse_max_pacing_rate(const char *arg, void *out, struct callbacks *cb)
{
        *(long long *)out = parse_rate(arg, cb);
}

static const struct rate_conversion *parse_unit_internal(const char *str,
                                                         struct callbacks *cb)
{
        const struct rate_conversion *conv;

        for (conv = conversions; conv->str; conv++) {
                if (!strcmp(conv->str, str))
                        return conv;
        }
        LOG_FATAL(cb, "invalid rate unit `%s'", str);
        return NULL;  /* unreachable */
}

void parse_unit(const char *arg, void *out, struct callbacks *cb)
{
        *(const struct rate_conversion **)out = parse_unit_internal(arg, cb);
}

void parse_duration(const char *arg, void *out, struct callbacks *cb)
{
        char *suffix;
        unsigned long *ns = (unsigned long *)out;

        *ns = 0;

        /* Parse the number. */
        errno = 0;
        unsigned long val = strtol(arg, &suffix, 0);
        if (errno != 0)
                PLOG_FATAL(cb, "strtol");
        *ns = val;

        /* Parse the suffix. No suffix indicates nanoseconds. */
        if (suffix == arg)
                LOG_FATAL(cb, "no duration digits");
        if (suffix[0] == '\0')
                return;
        char *suffixes[] = {
                "ns",
                "us",
                "ms",
                "s",
        };
        int i;
        for (i = 0; i < sizeof(suffixes)/sizeof(char *); i++) {
                if (!strcmp(suffix, suffixes[i]))
                        return;
                *ns *= 1000;
        }

        /* Never found a matching suffix. */
        LOG_FATAL(cb, "invalid suffix %s", suffix);
}
