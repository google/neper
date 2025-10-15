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

#include "common.h"
#include "flags.h"
#include "lib.h"
#include "parse.h"
#include "define_all_flags.h"
#include "check_all_options.h"

int main(int argc, char **argv)
{
        struct options opts = {.secret = "neper tcp_rr 201703241250"};
        struct callbacks cb = {0};
        struct flags_parser *fp;
        int exit_code = 0;

        logging_init(&cb, argc, argv);

        fp = flags_parser_create(&opts, &cb);

        /* Build up the flags from the most general to the most specific */
        fp = add_flags_common(fp);
        fp = add_flags_tcp(fp);
        fp = add_flags_rr(fp);
        fp = add_flags_tcp_rr(fp);

        flags_parser_run(fp, argc, argv);
        flags_parser_dump(fp);
        flags_parser_destroy(fp);

        /* Check all the options from most general to most specific */
        check_options_common(&opts, &cb);
        check_options_tcp(   &opts, &cb);
        check_options_rr(    &opts, &cb);
        check_options_tcp_rr(&opts, &cb);

        adjust_interval(&opts.interval, opts.test_length);
        if (opts.suicide_length) {
                if (create_suicide_timeout(opts.suicide_length)) {
                        PLOG_FATAL(&cb, "create_suicide_timeout");
                        goto exit;
                }
        }

        /* Run the actual test */
        exit_code = tcp_rr(&opts, &cb);
exit:
        logging_exit(&cb);
        return exit_code;
}
