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

#include "pq.h"
#include "common.h"

struct pq_impl {
        struct neper_pq pq;

        int pq_maxlen;     /* maximum # of elements */
        int pq_curlen;     /* current # of elements */
        void **pq_list;

        double (*pq_cmp)(void *, void *);

        struct callbacks *pq_cb;
};

/* Shift the element at index i up in the heap until it is greater than or equal
 * to its parent.
 */
static void pq_up(struct pq_impl *impl, int i)
{
        void **heap = impl->pq_list;

        for (;;) {
                int j = i / 2;  /* parent */
                if (!j)
                        break;
                if (impl->pq_cmp(heap[j], heap[i]) <= 0)
                        break;

                void *tmp = heap[i];
                heap[i] = heap[j];
                heap[j] = tmp;
                i = j;
        }
}

/* Shift the element at index i down in the heap until it is less than or equal
 * to its children.
 */
static void pq_down(struct pq_impl *impl, int i)
{
        void **heap = impl->pq_list;

        for (;;) {
                int j = i * 2;  /* left child */
                int k = j + 1;  /* right child */
                int top = i;

                if (j <= impl->pq_curlen && impl->pq_cmp(heap[j], heap[top]) < 0)
                        top = j;
                if (k <= impl->pq_curlen && impl->pq_cmp(heap[k], heap[top]) < 0)
                        top = k;
                if (top == i)
                        break;

                void *tmp = heap[i];
                heap[i] = heap[top];
                heap[top] = tmp;
                i = top;
        }
}

/* Append a new element to the bottom of the heap and then shift it upward into
 * its proper place. Scream and die if the caller exceeds the size limit.
 */
static void pq_enq(struct neper_pq *pq, void *elem)
{
        struct pq_impl *impl = (void *)pq;
        int i = ++impl->pq_curlen;

        if (i > impl->pq_maxlen)
                LOG_FATAL(impl->pq_cb, "pq_enq(), queue is full");

        impl->pq_list[i] = elem;
        pq_up(impl, i);
}

/* Remove (and return) the top element in the heap, move the element at the
 * bottom up to the top to replace the empty slot, and then shift it downward
 * into its proper place.
 */
static void *pq_deq(struct neper_pq *pq)
{
        struct pq_impl *impl = (void *)pq;
        void *elem = NULL;

        if (impl->pq_curlen) {
                elem = impl->pq_list[1];
                impl->pq_list[1] = impl->pq_list[impl->pq_curlen--];
                pq_down(impl, 1);
        }
        return elem;
}

static void pq_fini(struct neper_pq *pq)
{
        struct pq_impl *impl = (void *)pq;
        if (impl) {
                free(impl->pq_list);
                free(impl);
        }
}

/*
 * Creates a priority queue that can hold up to maxlen objects.
 * The heap storage array is 1-based, not 0-based, so we need to
 * allocate one extra pointer.
 */
struct neper_pq *neper_pq(double (*cmp)(void *, void *), uint32_t maxlen,
                          struct callbacks *cb)
{
        struct pq_impl *impl = calloc_or_die(1, sizeof(struct pq_impl), cb);
        struct neper_pq *pq = &impl->pq;

        pq->enq  = pq_enq;
        pq->deq  = pq_deq;
        pq->fini = pq_fini;

        impl->pq_maxlen = maxlen;
        impl->pq_list   = calloc_or_die(maxlen + 1, sizeof(void *), cb);
        impl->pq_cmp    = cmp;
        impl->pq_cb     = cb;

        return pq;
}
