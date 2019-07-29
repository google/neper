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

#ifndef THIRD_PARTY_NEPER_PQ_H
#define THIRD_PARTY_NEPER_PQ_H

#include <stdint.h>

struct callbacks;

/*
 * An simple priority queue API which just happens to have exactly the semantics
 * needed by neper:
 *
 * enq()  Adds an object to the priority queue.
 * deq()  Removes and returns the next object in the priority queue.
 * fini() Deallocates the object.
 *
 * int (*cmp)(void *, void *) is the standard kind of comparison function used
 * to order elements in the queue. For this implementation, the object with the
 * smallest value will be the next one removed.
 */

struct neper_pq {
        void (*enq)(struct neper_pq *, void *);
        void *(*deq)(struct neper_pq *);
        void (*fini)(struct neper_pq *);
};

struct neper_pq *neper_pq(double (*cmp)(void *, void *), uint32_t maxlen,
                          struct callbacks *);

#endif
