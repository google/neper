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

#ifndef THIRD_PARTY_NEPER_FLAGS_H
#define THIRD_PARTY_NEPER_FLAGS_H

struct options;
struct callbacks;
struct flags_parser;

struct flags_parser* flags_parser_create(struct options *opts,
                                         struct callbacks *cb);
struct options* flags_parser_opts(struct flags_parser *fp);
void flags_parser_add(struct flags_parser *fp, char short_name,
                      const char *long_name, const char *usage,
                      const char *type, void *variable);
void flags_parser_set_parser(struct flags_parser *fp, void *variable,
                             void (*parser)(const char *, void *,
                                            struct callbacks *));
void flags_parser_set_printer(struct flags_parser *fp, void *variable,
                              void (*printer)(const char *, const void *,
                                              struct callbacks *));
void flags_parser_set_no_argument(struct flags_parser *fp, void *variable);
void flags_parser_set_optional(struct flags_parser *fp, void *variable);
void flags_parser_run(struct flags_parser *fp, int argc, char **argv);
void flags_parser_dump(struct flags_parser *fp);
void flags_parser_destroy(struct flags_parser *fp);

/* You probably want DEFINE_FLAG or DEFINE_FLAG_NAMED instead of this. This
 * macro takes extra arguments -- separating TYPE and TYPESTR -- because the
 * preprocessor will convert a TYPE of "bool" too "_Bool" *except* in the
 * specific case where the macro contains #TYPE. This was not fun to debug.
 */
#define DEFINE_FLAG_NAMED_TYPED(FP, TYPE, TYPESTR, VAR, DEFAULT_VALUE, NAME,  \
                SHORT_NAME, USE)                                              \
        do {                                                                  \
                TYPE default_value = DEFAULT_VALUE;                           \
                struct options *opts = flags_parser_opts(FP);                 \
                opts->VAR = default_value;                                    \
                flags_parser_add(FP, SHORT_NAME, NAME,                        \
                                USE " (default " #DEFAULT_VALUE ")", TYPESTR, \
                                &opts->VAR);                                  \
        } while (0);

/* DEFINE_FLAG_NAMED is like DEFINE_FLAG, but uses a custom flag name. */
#define DEFINE_FLAG_NAMED(FP, TYPE, VAR, DEFAULT_VALUE, NAME, SHORT_NAME, USE) \
        DEFINE_FLAG_NAMED_TYPED(FP, TYPE, #TYPE, VAR, DEFAULT_VALUE, NAME,     \
                        SHORT_NAME, USE)

#define DEFINE_FLAG(FP, TYPE, VAR, DEFAULT_VALUE, SHORT_NAME, USAGE)       \
        DEFINE_FLAG_NAMED_TYPED(FP, TYPE, #TYPE, VAR, DEFAULT_VALUE, #VAR, \
                        SHORT_NAME, USAGE)

#define DEFINE_FLAG_PARSER(FP, VAR, PARSER) do { \
        struct options *opts = flags_parser_opts(FP); \
        flags_parser_set_parser(FP, &opts->VAR, PARSER); \
} while (0)

#define DEFINE_FLAG_PRINTER(FP, VAR, PRINTER) do { \
        struct options *opts = flags_parser_opts(FP); \
        flags_parser_set_printer(FP, &opts->VAR, PRINTER); \
} while (0)

#define DEFINE_FLAG_HAS_NO_ARGUMENT(FP, VAR) do { \
        struct options *opts = flags_parser_opts(FP); \
        flags_parser_set_no_argument(FP, &opts->VAR); \
} while (0)

#define DEFINE_FLAG_HAS_OPTIONAL_ARGUMENT(FP, VAR) do { \
        struct options *opts = flags_parser_opts(FP); \
        flags_parser_set_optional(FP, &opts->VAR); \
} while (0)

#endif
