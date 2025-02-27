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

#include "logging.h"
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include "lib.h"

/* TODO remove global variables */
static int stdout_lines;
static FILE *log_file;
static bool g_logtostderr;

static void print(void *logger, const char *key, const char *value_fmt, ...)
{
        va_list argp;

        printf("%s=", key);
        va_start(argp, value_fmt);
        vprintf(value_fmt, argp);
        va_end(argp);
        printf("\n");
        fflush(stdout);
        ++stdout_lines;
}

/* Open a file for logging. Must be called before LOG(). Not thread-safe.
 * Logs will be written to the filename
 *     <program name>.<hostname>.<user name>.<date>-<time>.<pid>.log
 * in the current working directory.
 */
static void open_log(void)
{
        char *hostname, *user_name, path[1024];
        struct utsname un;
        struct timespec ts;
        struct tm tm;

        uname(&un);
        hostname = un.nodename;
        if (!hostname)
                hostname = "_";
        user_name = getlogin();
        if (!user_name)
                user_name = "_";
        clock_gettime(CLOCK_REALTIME, &ts);
        localtime_r(&ts.tv_sec, &tm);
        snprintf(path, sizeof(path),
                 "%s.%s.%s.%04d%02d%02d-%02d%02d%02d.%d.log",
                 program_invocation_short_name, hostname, user_name,
                 1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday, tm.tm_hour,
                 tm.tm_min, tm.tm_sec, getpid());
        if (log_file)
                fclose(log_file);
        log_file = fopen(path, "w");
}

static void close_log(void)
{
        if (log_file) {
                fclose(log_file);
                log_file = NULL;
        }
}

enum LOG_LEVEL { FATAL, ERROR, WARNING, INFO };

/* Generic logging function. Thread-safe.
 *
 * Log lines have this form:
 *     Lmmdd hh:mm:ss.uuuuuu nnn thrdid file:line] func: msg...
 * where the fields are defined as follows:
 *   L                A single character, representing the log level
 *   mm               The month (zero padded)
 *   dd               The day (zero padded)
 *   hh:mm:ss.uuuuuu  Time in hours, minutes and fractional seconds
 *   nnn              The number of lines in stdout (space padded)
 *   thrdid           The space-padded thread ID as returned by gettid()
 *   file             The file name
 *   line             The line number
 *   func             The calling function name
 *   msg              The user-supplied message
 */
static void logging(const char *file, int line, const char *func,
                    enum LOG_LEVEL level, const char *fmt, va_list argp)
{
        char buf[4096], *msg, level_char, *path;
        int size, thread_id;
        struct timespec ts;
        struct tm tm;

        if (!log_file)
                return;
        /* vsnprintf returns # of chars excluding the terminating NULL byte */
        size = vsnprintf(buf, sizeof(buf), fmt, argp) + 1;
        if (size > sizeof(buf)) {
                msg = malloc(size);
                vsnprintf(msg, size, fmt, argp);
        } else
                msg = buf;
        if (level == FATAL)
                level_char = 'F';
        else if (level == ERROR)
                level_char = 'E';
        else if (level == WARNING)
                level_char = 'W';
        else if (level == INFO)
                level_char = 'I';
        else
                level_char = ' ';
        clock_gettime(CLOCK_REALTIME, &ts);
        localtime_r(&ts.tv_sec, &tm);
        thread_id = syscall(SYS_gettid);
        if (thread_id == -1)
                thread_id = getpid();
        path = strdup(file);
        fprintf(g_logtostderr ? stderr : log_file,
                "%c%02d%02d %02d:%02d:%02d.%06ld %3d %6d %s:%d] %s: %s\n",
                level_char, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                tm.tm_sec, ts.tv_nsec / 1000, stdout_lines, thread_id,
                basename(path), line, func, msg);
        free(path);
        /* TODO dump stack trace if FATAL */
        if (level == FATAL || level == ERROR)
                fprintf(stderr, "%s\n", msg);
        if (size > sizeof(buf))
                free(msg);
        if (level == FATAL || level == ERROR || level == WARNING)
                fflush(g_logtostderr ? stderr : log_file);
        if (level == FATAL) {
                fclose(log_file);
                fflush(stdout);
                fflush(stderr);
                exit(1);
        }
}

static void log_fatal(void *logger, const char *file, int line,
                      const char *function, const char *format, ...)
{
        va_list argp;

        va_start(argp, format);
        logging(file, line, function, FATAL, format, argp);
        va_end(argp);
}

static void log_error(void *logger, const char *file, int line,
                      const char *function, const char *format, ...)
{
        va_list argp;

        va_start(argp, format);
        logging(file, line, function, ERROR, format, argp);
        va_end(argp);
}

static void log_warn(void *logger, const char *file, int line,
                     const char *function, const char *format, ...)
{
        va_list argp;

        va_start(argp, format);
        logging(file, line, function, WARNING, format, argp);
        va_end(argp);
}

static void log_info(void *logger, const char *file, int line,
                     const char *function, const char *format, ...)
{
        va_list argp;

        va_start(argp, format);
        logging(file, line, function, INFO, format, argp);
        va_end(argp);
}

static void logtostderr(void *logger)
{
        g_logtostderr = true;
}

static void logtonull()
{
        log_file = fopen("/dev/null", "w");
        if (!log_file) {
                log_file = stderr;
                log_error(NULL, __FILE__, __LINE__, __func__,
                          "Can't open /dev/null for logging, "
                          "log to stderr instead: %s", strerror(errno));
        }
}

void logging_init(struct callbacks *cb, int argc, char **argv)
{
        /*
         * Quickly scan options, if we have --logtostderr or --nolog,
         * no need to create a logfile.
         */
        int i;
        for (i = 1; i < argc; i++) {
                if (!strcmp(argv[i], "--logtostderr")) {
                        log_file = stderr;
                        break;
                }
                if (!strcmp(argv[i], "--nolog")) {
                        logtonull();
                        break;
                }
        }
        if (!log_file)
                open_log();
        cb->logger = NULL;
        cb->print = print;
        cb->log_fatal = log_fatal;
        cb->log_error = log_error;
        cb->log_warn = log_warn;
        cb->log_info = log_info;
        cb->logtostderr = logtostderr;
}

void logging_exit(struct callbacks *cb)
{
        close_log();
}
