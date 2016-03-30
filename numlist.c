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

#include "numlist.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lib.h"
#include "logging.h"

#define MEMBLOCK_SIZE 500

struct memblock {
        size_t size;
        struct memblock *next;
        double data[MEMBLOCK_SIZE];
};

struct numlist {
        struct callbacks *cb;
        struct memblock *head;
};

static void prepend_memblock(struct numlist *lst)
{
        struct memblock *blk;

        blk = malloc(sizeof(struct memblock));
        if (!blk)
                PLOG_FATAL(lst->cb, "unable to allocate memblock");
        blk->size = 0;
        blk->next = lst->head;
        lst->head = blk;
}

struct numlist *numlist_create(struct callbacks *cb)
{
        struct numlist *lst;

        lst = malloc(sizeof(struct numlist));
        if (!lst)
                PLOG_FATAL(cb, "unable to allocate numlist");
        lst->cb = cb;
        lst->head = NULL;
        prepend_memblock(lst);
        return lst;
}

void numlist_destroy(struct numlist *lst)
{
        struct memblock *block, *next;

        block = lst->head;
        while (block) {
                next = block->next;
                free(block);
                block = next;
        }
        free(lst);
}

void numlist_add(struct numlist *lst, double val)
{
        if (lst->head->size == MEMBLOCK_SIZE)
                prepend_memblock(lst);
        lst->head->data[lst->head->size++] = val;
}

void numlist_concat(struct numlist *lst, struct numlist *tail)
{
        struct memblock *blk = lst->head;

        while (blk->next)
                blk = blk->next;
        blk->next = tail->head;
        tail->head = NULL;
}

#define for_each_memblock(blk, lst) \
        for (blk = (lst)->head; blk; blk = blk->next)

#define for_each_number(n, blk) \
        for (n = (blk)->data; n < (blk)->data + blk->size; n++)

#define for_each(n, blk, lst) \
        for_each_memblock(blk, lst) for_each_number(n, blk)

size_t numlist_size(struct numlist *lst)
{
        struct memblock *blk;
        size_t size = 0;
        double *n;

        for_each(n, blk, lst)
                size++;
        return size;
}

double numlist_min(struct numlist *lst)
{
        double min = INFINITY, *n;
        struct memblock *blk;

        for_each(n, blk, lst) {
                if (*n < min)
                        min = *n;
        }
        return min;
}

double numlist_max(struct numlist *lst)
{
        double max = -INFINITY, *n;
        struct memblock *blk;

        for_each(n, blk, lst) {
                if (*n > max)
                        max = *n;
        }
        return max;
}

double numlist_mean(struct numlist *lst)
{
        double sum = 0, cnt = 0, *n;
        struct memblock *blk;

        for_each(n, blk, lst) {
                sum += *n;
                cnt++;
        }
        return sum / cnt;
}

double numlist_stddev(struct numlist *lst)
{
        double sum = 0, cnt = 0, mean, *n;
        struct memblock *blk;

        mean = numlist_mean(lst);
        for_each(n, blk, lst) {
                sum += (*n - mean) * (*n - mean);
                cnt++;
        }
        return sqrt(sum / cnt);
}

static int compare_doubles(const void *a, const void *b)
{
        const double x = *(const double *)a, y = *(const double *)b;

        if (x < y)
                return -1;
        if (x > y)
                return 1;
        return 0;
}

double numlist_percentile(struct numlist *lst, int percentile)
{
        double *values, *n, result;
        struct memblock *blk;
        size_t size, i = 0;

        size = numlist_size(lst);
        if (size == 0)
                return NAN;
        values = malloc(sizeof(double) * size);
        for_each(n, blk, lst)
                values[i++] = *n;
        qsort(values, size, sizeof(double), compare_doubles);
        result = values[(size - 1) * percentile / 100];
        free(values);
        return result;
}
