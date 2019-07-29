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

#include "flags.h"

#include <ctype.h>
#include <getopt.h>

#include "common.h"
#include "version.h"

typedef void (*parser_t)(const char *, void *, struct callbacks *);
typedef void (*printer_t)(const char *, const void *, struct callbacks *);

struct flag {
        char short_name;
        char *long_name;
        char *usage;
        char *type;
        char *variable_name;
        void *variable;
        parser_t parser;
        printer_t printer;
        int has_arg;
        struct flag *next;
};

struct flags_parser {
        struct options *opts;
        struct callbacks *cb;
        struct flag *flags;
        bool help;
        bool version;
};

static void flag_destroy(struct flag *flag)
{
        free(flag->long_name);
        free(flag->usage);
        free(flag->type);
        free(flag->variable_name);
}

void flags_parser_destroy(struct flags_parser *fp)
{
        struct flag *flag;

        for (flag = fp->flags; flag; flag = flag->next)
                flag_destroy(flag);
        free(fp);
}

struct flags_parser* flags_parser_create(struct options *opts,
                                         struct callbacks *cb)
{
        struct flags_parser *fp;

        fp = calloc(1, sizeof(*fp));
        if (!fp)
                LOG_FATAL(cb, "calloc fp");
        fp->opts = opts;
        fp->cb = cb;
        flags_parser_add(fp, 'h', "help", "Show usage and exit", "bool",
                         &fp->help);
        flags_parser_add(fp, 'v', "version", "Show version and exit", "bool",
                         &fp->version);
        return fp;
}

struct options* flags_parser_opts(struct flags_parser *fp)
{
        return fp->opts;
}

static char* remap_long_name(char *remapped)
{
        size_t i;

        for (i = 0; i < strlen(remapped); i++) {
                if (remapped[i] == '_')
                        remapped[i] = '-';
        }
        return remapped;
}

void flags_parser_add(struct flags_parser *fp, char short_name,
                      const char *long_name, const char *usage,
                      const char *type, void *variable)
{
        struct flag *flag;

        flag = calloc(1, sizeof(*flag));
        if (!flag)
                LOG_FATAL(fp->cb, "calloc flag");
        flag->short_name = short_name;
        flag->long_name = remap_long_name(strdup(long_name));
        flag->usage = strdup(usage);
        flag->type = strdup(type);
        flag->variable_name = strdup(long_name);
        flag->variable = variable;
        if (strcmp("bool", flag->type) == 0)
                flag->has_arg = no_argument;
        else
                flag->has_arg = required_argument;
        flag->next = fp->flags;
        fp->flags = flag;
}

static void print_usage(FILE *f, const char *program, struct flag *flags)
{
        const struct flag *flag;
        size_t i, maxlen;

        maxlen = 0;
        for (flag = flags; flag; flag = flag->next) {
                if (strlen(flag->long_name) > maxlen)
                        maxlen = strlen(flag->long_name);
        }

        fprintf(f, "usage: %s [<options>]\n\n", program);
        for (flag = flags; flag; flag = flag->next) {
                if (isgraph(flag->short_name))
                        fprintf(f, "  -%c,", flag->short_name);
                else
                        fprintf(f, "     ");
                fprintf(f, " --%s", flag->long_name);
                for (i = 0; i < maxlen - strlen(flag->long_name); i++)
                        fprintf(f, " ");
                fprintf(f, "  %s\n", flag->usage);
        }
}

static void default_parser(const char *type, char *arg, void *out,
                           struct callbacks *cb)
{
        if (strcmp(type, "bool") == 0 && !arg)
                *(bool *)out = true;
        else if (strcmp(type, "int") == 0)
                *(int *)out = atoi(arg);
        else if (strcmp(type, "const char *") == 0)
                *(const char **)out = arg;
        else if (strcmp(type, "unsigned long") == 0)
                *(unsigned long *)out = strtoul(arg, NULL, 0);
        else if (strcmp(type, "double") == 0)
                *(double *)out = atof(arg);
        else
                LOG_ERROR(cb, "Unknown type `%s' for arg `%s'.", type, arg);
}

void flags_parser_set_parser(struct flags_parser *fp, void *variable,
                             parser_t parser)
{
        struct flag *flag;

        for (flag = fp->flags; flag; flag = flag->next) {
                if (flag->variable == variable)
                        flag->parser = parser;
        }
}

void flags_parser_set_no_argument(struct flags_parser *fp, void *variable)
{
        struct flag *flag;

        for (flag = fp->flags; flag; flag = flag->next) {
                if (flag->variable == variable)
                        flag->has_arg = no_argument;
        }
}

void flags_parser_set_optional(struct flags_parser *fp, void *variable)
{
        struct flag *flag;

        for (flag = fp->flags; flag; flag = flag->next) {
                if (flag->variable == variable)
                        flag->has_arg = optional_argument;
        }
}

/*
 * Any value greater than 255 is sufficient for the purpose of being
 * distinguishable from ASCII characters. Using 1000 may ease debugging.
 */
#define LONG_OPTION_START 1000

void flags_parser_run(struct flags_parser *fp, int argc, char **argv)
{
        struct callbacks *cb = fp->cb;
        const struct flag *flag;
        struct option *longopts;
        size_t i, j, num_flags;
        struct flag *flags;
        char *shortopts;
        parser_t parser;
        int c;

        num_flags = 0;
        for (flag = fp->flags; flag; flag = flag->next)
                num_flags++;
        if (num_flags == 0)
                return;

        flags = calloc(num_flags, sizeof(flags[0]));
        if (!flags)
                LOG_FATAL(cb, "calloc flags");
        i = 0;
        for (flag = fp->flags; flag; flag = flag->next)
                flags[i++] = *flag;

        /* Sort flags by alphabetical order. */
        for (i = 0; i < num_flags; i++) {
                for (j = i+1; j < num_flags; j++) {
                        if (flags[i].short_name > flags[j].short_name) {
                                struct flag tmp = flags[i];
                                flags[i] = flags[j];
                                flags[j] = tmp;
                        }
                }
        }

        /* Link the local copy of flags. */
        for (i = 0; i+1 < num_flags; i++)
                flags[i].next = &flags[i+1];
        flags[num_flags-1].next = NULL;

        /* Allocate one extra element for the end of array (all zeros). */
        longopts = calloc(num_flags + 1, sizeof(longopts[0]));
        if (!longopts)
                LOG_FATAL(cb, "calloc longopts");
        for (i = 0; i < num_flags; i++) {
                longopts[i].name = flags[i].long_name;
                longopts[i].has_arg = flags[i].has_arg;
                longopts[i].flag = NULL;
                longopts[i].val = LONG_OPTION_START + i;
        }

        shortopts = calloc(num_flags*3 + 1, sizeof(char));
        if (!shortopts)
                LOG_FATAL(cb, "calloc shortopts");
        j = 0;
        for (i = 0; i < num_flags; i++) {
                if (!isgraph(flags[i].short_name))
                        continue;
                shortopts[j++] = flags[i].short_name;
                if (longopts[i].has_arg == no_argument)
                        continue;
                shortopts[j++] = ':';
                if (longopts[i].has_arg == required_argument)
                        continue;
                shortopts[j++] = ':';
        }

        while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
                if (c == '?') {
                        /* error message already printed by getopt_long */
                        print_usage(stderr, argv[0], flags);
                        exit(1);
                }
                i = c - LONG_OPTION_START;
                if (isgraph(c)) {
                        for (i = 0; i < num_flags; i++) {
                                if (c == flags[i].short_name)
                                        break;
                        }
                }
                parser = flags[i].parser;
                if (parser) {
                        parser(optarg, flags[i].variable, cb);
                        continue;
                }
                default_parser(flags[i].type, optarg, flags[i].variable, cb);
        }
        if (optind < argc) {
                fprintf(stderr, "%s: non-option arguments:", argv[0]);
                while (optind < argc)
                        fprintf(stderr, " '%s'", argv[optind++]);
                fprintf(stderr, "\n");
                print_usage(stderr, argv[0], flags);
                exit(1);
        }
        if (fp->help) {
                print_usage(stdout, argv[0], flags);
                exit(0);
        }
        if (fp->version) {
                show_version();
                exit(0);
        }

        free(shortopts);
        free(longopts);
        free(flags);
}

void flags_parser_set_printer(struct flags_parser *fp, void *variable,
                              printer_t printer)
{
        struct flag *flag;

        for (flag = fp->flags; flag; flag = flag->next) {
                if (flag->variable == variable)
                        flag->printer = printer;
        }
}

static void print_flag(const struct flag *flag, struct callbacks *cb)
{
        const char *type = flag->type;
        const char *name = flag->variable_name;
        const void *var = flag->variable;
        printer_t printer = flag->printer;

        if (printer)
                printer(name, var, cb);
        else if (strcmp(type, "bool") == 0)
                PRINT(cb, name, "%d", *(bool *)var);
        else if (strcmp(type, "int") == 0)
                PRINT(cb, name, "%d", *(int *)var);
        else if (strcmp(type, "const char *") == 0)
                PRINT(cb, name, "%s", *(const char **)var ?: "");
        else if (strcmp(type, "unsigned long") == 0)
                PRINT(cb, name, "%lu", *(unsigned long *)var);
        else if (strcmp(type, "double") == 0)
                PRINT(cb, name, "%f", *(double *)var);
        else if (strcmp(type, "long long") == 0)
                PRINT(cb, name, "%lld", *(long long *)var);
        else
                LOG_ERROR(cb, "Unknown type `%s' for variable %s", type, name);
}

void flags_parser_dump(struct flags_parser *fp)
{
        const struct flag *flag;

        PRINT(fp->cb, "VERSION", "%s", get_version());
        for (flag = fp->flags; flag; flag = flag->next) {
                if (flag->variable == &fp->help)
                        continue;
                if (flag->variable == &fp->version)
                        continue;
                print_flag(flag, fp->cb);
        }
}
