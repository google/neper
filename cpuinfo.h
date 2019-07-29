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
#include "lib.h"

#ifndef THIRD_PARTY_NEPER_CPUINFO_H
#define THIRD_PARTY_NEPER_CPUINFO_H

struct cpuinfo {
        int processor;
        int physical_id;
        int siblings;
        int core_id;
        int cpu_cores;
};

/* Parse /proc/cpuinfo output and fill in struct cpuinfo for each processor
 * input args: cpus: pointer to preallocated struct cpuinfo array
 *             max_cpus: max number of cpus in the array
 *             cb: general callback struct
 * return: number of filled struct cpuinfo on success and -1 on failure
 */
int get_cpuinfo(struct cpuinfo *cpus, int max_cpus, struct callbacks *cb);

#endif
