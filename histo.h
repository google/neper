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

/*
 * A simple histogram API for tracking a series of latency measurements:
 *
 * An 'event' is a single measurement.
 * An 'epoch' is all events collected within some time interval.
 *
 * Typical usage is to call neper_histo_event() many times and
 * neper_histo_epoch() perhaps every second or so.
 */

/* Internally the collector allows 64-bit values in buckets with k_bits
 * significant bits. 6 gives 1.5% error and about 4K buckets.
 */
#define DEFAULT_K_BITS 4

struct thread;
struct neper_histo;

/* Create a new collector */
struct neper_histo *neper_histo_new(const struct thread *t, uint8_t k_bits);

/* Returns the min of the previous sampling epoch. */
double neper_histo_min(const struct neper_histo *);

/* Returns the max of the previous sampling epoch. */
double neper_histo_max(const struct neper_histo *);

/* Returns the mean of the previous sampling epoch. */
double neper_histo_mean(const struct neper_histo *);

/* Returns the stddev of the previous sampling epoch. */
double neper_histo_stddev(const struct neper_histo *);

/* Returns the percent of the previous sampling epoch. */
double neper_histo_percent(const struct neper_histo *, int percentage);

/* Adds one histogram to the current epoch of another. */
void neper_histo_add(struct neper_histo *des, const struct neper_histo *src);

/* Adds a new event to the current sampling epoch. */
void neper_histo_event(struct neper_histo *, double delta_s);

/* Returns the event total across all sampling epochs. */
uint64_t neper_histo_samples(const struct neper_histo *);

/* Commits the current sample set and begins a new one. */
void neper_histo_epoch(struct neper_histo *);

/* Prints the results */
void neper_histo_print(struct neper_histo *);

/* Destroy the object */
void neper_histo_delete(struct neper_histo *);

#endif
