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

#ifndef NEPER_CPUINFO_H
#define NEPER_CPUINFO_H

struct cpuinfo {
        int processor;
        int physical_id;
        int siblings;
        int core_id;
        int cpu_cores;
};

/* Parse /proc/cpuinfo to get CPU topology.  cpus is a user-provided buffer to
 * be filled in.  max_cpus is the maximum number of items that can be filled in
 * cpus.  On success, the number of items filled in is returned.  Otherwise, -1
 * is returned and errno is set.
 */
int get_cpuinfo(struct cpuinfo *cpus, int max_cpus);

#endif
