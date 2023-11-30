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

CFLAGS := -std=c99 -Wall -O3 -g -D_GNU_SOURCE -DNO_LIBNUMA

ifdef WITH_TCPDEVMEM_CUDA
	CFLAGS += -DWITH_TCPDEVMEM_CUDA
endif
ifdef WITH_TCPDEVMEM_UDMA
	CFLAGS += -DWITH_TCPDEVMEM_UDMA -DNDEBUG=1 -static -I ~/cos-kernel/usr/include
	LDFLAGS += -static
endif

ifndef_any_of = $(filter undefined,$(foreach v,$(1),$(origin $(v))))
ifdef_any_of = $(filter-out undefined,$(foreach v,$(1),$(origin $(v))))

lib := \
	check_all_options.o \
	coef.o \
	common.o \
	control_plane.o \
	cpuinfo.o \
	define_all_flags.o \
	flags.o \
	flow.o \
	hexdump.o \
	histo.o \
	logging.o \
	loop.o \
	numlist.o \
	or_die.o \
	parse.o \
	percentiles.o \
	pq.o \
	print.o \
	rusage.o \
	snaps.o \
	socket.o \
	stats.o \
	thread.o \
	version.o

tcp_rr-objs := tcp_rr_main.o tcp_rr.o rr.o $(lib)

tcp_stream-objs := tcp_stream_main.o tcp_stream.o stream.o $(lib)
ifdef WITH_TCPDEVMEM_CUDA
	tcp_stream-objs += tcpdevmem_cuda.o
endif
ifdef WITH_TCPDEVMEM_UDMA
	tcp_stream-objs += tcpdevmem_udma.o
endif
ifneq ($(call ifdef_any_of,WITH_TCPDEVMEM_CUDA WITH_TCPDEVMEM_UDMA),)
	tcp_stream-objs += tcpdevmem.o
endif


tcp_crr-objs := tcp_crr_main.o tcp_crr.o rr.o $(lib)

udp_rr-objs := udp_rr_main.o udp_rr.o rr.o $(lib)

udp_stream-objs := udp_stream_main.o udp_stream.o stream.o $(lib)

psp_stream-objs := psp_stream_main.o psp_stream.o stream.o psp_lib.o $(lib)

psp_crr-objs := psp_crr_main.o psp_crr.o rr.o psp_lib.o $(lib)

psp_rr-objs := psp_rr_main.o psp_rr.o rr.o psp_lib.o $(lib)

ext-libs := -lm -lrt -lpthread

tcpdevmem_cuda.o: tcpdevmem_cuda.cu
	nvcc -arch=sm_90 -O3 -g -D_GNU_SOURCE -DNO_LIBNUMA -DWITH_TCPDEVMEM_CUDA -c -o $@ $^

tcp_rr: $(tcp_rr-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

tcp_stream: $(tcp_stream-objs)
ifdef WITH_TCPDEVMEM_CUDA
	g++ $(LDFLAGS) -o $@ $^ $(ext-libs) -lc -L/usr/local/cuda/lib64 -lcudart -lcuda
else
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)
endif

tcp_crr: $(tcp_crr-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

udp_rr: $(udp_rr-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

udp_stream: $(udp_stream-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

psp_stream: $(psp_stream-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

psp_crr: $(psp_crr-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

psp_rr: $(psp_rr-objs)
	$(CC) $(LDFLAGS) -o $@ $^ $(ext-libs)

binaries: tcp_rr tcp_stream tcp_crr udp_rr udp_stream psp_stream psp_crr psp_rr

clean:
	rm -f *.o tcp_rr tcp_stream tcp_crr udp_rr udp_stream psp_stream psp_crr psp_rr
