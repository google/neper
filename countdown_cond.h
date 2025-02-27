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

#ifndef _COUNTDOWN_COND_H
#define _COUNTDOWN_COND_H

#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#define futex(...) syscall(__NR_futex, __VA_ARGS__)

/*
 * countdown condition variable, example usage:
 *
 * countdown_cond cc;
 *
 * main thread:
 *
 * init(&cc, N); // initialize countdown value to N, note not atomic so
 *               // needs to be called prior to any thread calling other
 *               // functions.
 *
 *
 * waiting thread:
 *
 * wait(&cc); // wait synchronization barrier
 *
 *
 * worker thread(s):
 *
 * if (dec(&cc) < 0) {
 *     return false; // nothing to do
 * } else {
 *         .
 *         . // do work
 *         .
 *     if (!commit(&cc))
 *         return fakse; // last commit
 *     return true // maybe more work to do
 * )
 * // note, dec()/commit() are order independent
 */

struct countdown_cond {
        int value;
        int wait;
};

static inline void countdown_cond_init(struct countdown_cond *cc, int v)
{
        cc->value = v;
        cc->wait = v;
}

static inline int countdown_cond_dec(const struct countdown_cond *cc)
{
        return __sync_add_and_fetch((int *)&cc->value, -1);
}

static inline int countdown_cond_commit(//struct callbacks *cb,
                                    const struct countdown_cond *cc)
{
        int value = __sync_add_and_fetch((int *)&cc->wait, -1);

        if (value == 0)
                futex(&cc->wait, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
        if (value >= 0)
                return value;

        //PLOG_FATAL(cb, "countdown_inc() value underflow %d", value);
        return -1;
}

static inline void countdown_cond_wait(struct countdown_cond *cc)
{
        while (cc->wait > 0)
                futex(&cc->wait, FUTEX_WAIT, cc->value, NULL, NULL, 0);
}

#endif
