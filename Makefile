# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
#
# Makefile.

all: binaries

CFLAGS = -std=c99 -Wall -Werror -O3 -g -D_GNU_SOURCE

lib := \
	common.o \
	control_plane.o \
	cpuinfo.o \
	flags.o \
	flow.o \
	hexdump.o \
	interval.o \
	logging.o \
	numlist.o \
	percentiles.o \
	sample.o \
	thread.o \
	version.o

tcp_rr-objs := tcp_rr_main.o tcp_rr.o $(lib)

tcp_stream-objs := tcp_stream_main.o tcp_stream.o $(lib)

ext-libs := -lm -lpthread -lrt

tcp_rr: $(tcp_rr-objs)
	$(CC) -o $@ $^ $(ext-libs)

tcp_stream: $(tcp_stream-objs)
	$(CC) -o $@ $^ $(ext-libs)

binaries: tcp_rr tcp_stream

clean:
	rm -f *.o tcp_rr tcp_stream
