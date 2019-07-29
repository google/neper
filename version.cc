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

#include "version.h"

#include <stdio.h>

#define MAJOR 1
#define MINOR 2
#define PATCH 0

#define AS_STRING2(x) #x
#define AS_STRING(x) AS_STRING2(x)

#ifdef BUILD_CHANGELIST
#define BUILD AS_STRING(BUILD_CHANGELIST)
#endif

static const char g_version[] =
        AS_STRING(MAJOR) "." AS_STRING(MINOR) "." AS_STRING(PATCH)
#ifdef BUILD
        "+" BUILD
#endif
;

extern "C" const char* get_version(void)
{
        return g_version;
}

extern "C" void show_version(void)
{
        printf("%s\n", get_version());
#ifdef BUILD_ID
        printf("http://sponge/%s\n", BUILD_ID);
#endif
}
