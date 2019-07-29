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

#ifndef THIRD_PARTY_NEPER_COEF_H
#define THIRD_PARTY_NEPER_COEF_H

#include <stdint.h>

struct timespec;

/*
 * A simple API for tracking the correlation coefficient in a set of samples:
 *
 * event()       Adds a new timestamped event (counter) to the sample set.
 * correlation() Returns the correlation coefficient for the sample set.
 * thruput()     Returns the average events per second for the sample set,
 *               which may be bytes or transactions or whatever.
 * end()         Returns a pointer to the timespec for the final event.
 * fini()        Deallocates the object.
 */

struct neper_coef {
        void (*event)(struct neper_coef *, const struct timespec *, uint64_t);
        double (*correlation)(const struct neper_coef *);
        double (*thruput)(const struct neper_coef *);
        const struct timespec *(*end)(const struct neper_coef *);
        void (*fini)(struct neper_coef *);
};

struct neper_coef *neper_coef(void);

#endif
