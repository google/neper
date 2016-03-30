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

#include "hexdump.h"
#include <ctype.h>
#include <string.h>

#define WIDTH 16

char *hexdump(const char *in, size_t in_len, char *out, size_t out_len)
{
        static const char chr[] = "0123456789abcdef";

        const int num_rows = (in_len + WIDTH - 1) / WIDTH;
        char row_buf[WIDTH * 3 + WIDTH + 1];
        int row, i, c;

        for (row = 0; row < num_rows; row++) {
                for (i = 0; i < WIDTH; i++) {
                        if (row * WIDTH + i < in_len) {
                                c = in[row * WIDTH + i];
                                row_buf[i * 3] = chr[c / 16];
                                row_buf[i * 3 + 1] = chr[c % 16];
                                row_buf[i * 3 + 2] = ' ';
                                row_buf[WIDTH * 3 + i] = isprint(c) ? c : '.';
                        } else {
                                row_buf[i * 3] = ' ';
                                row_buf[i * 3 + 1] = ' ';
                                row_buf[i * 3 + 2] = ' ';
                                row_buf[WIDTH * 3 + i] = ' ';
                        }
                }
                row_buf[WIDTH * 3 + WIDTH] = '\n';
                if (sizeof(row_buf) * (row + 1) >= out_len)
                        return NULL;
                memcpy(out + sizeof(row_buf) * row, row_buf, sizeof(row_buf));
        }
        out[sizeof(row_buf) * num_rows] = '\0';
        return out;
}
