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

#ifndef THIRD_PARTY_NEPER_HISTO_H
#define THIRD_PARTY_NEPER_HISTO_H

#include <stdint.h>

struct thread;

/*
 * A simple histogram API for tracking a series of latency measurements:
 * 
 * min()         Returns the min of the previous sampling epoch.
 * max()         Returns the max of the previous sampling epoch.
 * mean()        Returns the mean of the previous sampling epoch.
 * stddev()      Returns the stddev of the previous sampling epoch.
 * percent()     Returns the percent of the previous sampling epoch.
 * add()         Adds one histogram to the current epoch of another.
 * event()       Adds a new event to the current sampling epoch.
 * events()      Returns the event total across all sampling epochs.
 * epoch()       Commits the current sample set and begins a new one.
 * print()       Prints the results.
 * fini()        Deallocates the object.
 *
 * An 'event' is a single measurement.
 * An 'epoch' is all events collected within some time interval.
 *
 * So, typical usage is to call histo->event() many times and histo->epoch()
 * perhaps every second or so.
 */

struct neper_histo {
        uint64_t (*events)(const struct neper_histo *);

        double (*min)(const struct neper_histo *);
        double (*max)(const struct neper_histo *);
        double (*mean)(const struct neper_histo *);
        double (*stddev)(const struct neper_histo *);
        double (*percent)(const struct neper_histo *, int percentage);

        void (*add)(struct neper_histo *des, const struct neper_histo *src);

        void (*event)(struct neper_histo *, double delta_s);
        void (*epoch)(struct neper_histo *);
        void (*print)(struct neper_histo *);
        void (*fini)(struct neper_histo *);
};

/*
 * We use a factory to create histo objects so they can all share one set of
 * common lookup tables, saving a great deal of memory.
 */

struct neper_histo_factory {
        struct neper_histo *(*create)(const struct neper_histo_factory *);
        void (*fini)(struct neper_histo_factory *);
};

struct neper_histo_factory *neper_histo_factory(const struct thread *,
                                                int size,
                                                double growth);

#endif
