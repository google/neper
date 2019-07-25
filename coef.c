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

#include "coef.h"
#include "common.h"

struct coef_impl {
        struct neper_coef coef;

        const struct timespec *time_0;
        const struct timespec *time_N;
        uint64_t val_0;
        uint64_t val_N;
        double sum_xx;
        double sum_xy;
        double sum_yy;
};

static void coef_event(struct neper_coef *coef, const struct timespec *time,
                       uint64_t val)
{
        struct coef_impl *impl = (void *)coef;

        if (impl->time_0) {
                impl->time_N = time;
                impl->val_N = val;

                double seconds = seconds_between(impl->time_0, impl->time_N);
                double events = impl->val_N - impl->val_0;

                impl->sum_xx += seconds * seconds;
                impl->sum_xy += seconds * events;
                impl->sum_yy += events * events;
        } else {
                impl->time_0 = time;
                impl->val_0 = val;
        }
}

static double coef_correlation(const struct neper_coef *coef)
{
        const struct coef_impl *impl = (void *)coef;

        return impl->sum_xy / sqrt(impl->sum_xx * impl->sum_yy);
}

static double coef_thruput(const struct neper_coef *coef)
{
        const struct coef_impl *impl = (void *)coef;

        double seconds = seconds_between(impl->time_0, impl->time_N);
        double events = impl->val_N - impl->val_0;
        return events / seconds;
}

static const struct timespec *coef_end(const struct neper_coef *coef)
{
        const struct coef_impl *impl = (void *)coef;

        return impl->time_N;
}

static void coef_fini(struct neper_coef *coef)
{
        struct coef_impl *impl = (void *)coef;

        if (impl)
                free(impl);
}

struct neper_coef *neper_coef(void)
{
        struct coef_impl *impl = calloc(1, sizeof(struct coef_impl));
        struct neper_coef *coef = &impl->coef;

        coef->event       = coef_event;
        coef->correlation = coef_correlation;
        coef->thruput     = coef_thruput;
        coef->end         = coef_end;
        coef->fini        = coef_fini;

        return coef;
}
