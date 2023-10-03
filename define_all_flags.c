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

struct flags_parser *add_flags_common(struct flags_parser *fp)
{
        /* Define flags common to all main programs */

        DEFINE_FLAG(fp, int,          magic,         42,       0,  "Magic number used by control connections");
        DEFINE_FLAG(fp, int,          maxevents,     1000,     0,  "Number of epoll events per epoll_wait() call");
        DEFINE_FLAG(fp, int,          num_flows,     1,       'F', "Total number of flows");
        DEFINE_FLAG(fp, int,          num_threads,   1,       'T', "Number of threads");
        DEFINE_FLAG(fp, int,          num_clients,   1,        0,  "Number of clients");
        DEFINE_FLAG(fp, int,          listen_backlog, 128,     0,  "Backlog size for listen()");
        DEFINE_FLAG(fp, int,          suicide_length, 0,      's', "Suicide length in seconds");
        DEFINE_FLAG(fp, int,          source_port,  -1,        0,  "Sender (source) data port. First data stream will use this port, each next stream will use port one larger than previous one. When not specified, kernel assigns free source ports.");
        DEFINE_FLAG(fp, bool,         stime_use_proc,false,   'S', "Use global system+IRQ+SoftIRQ time from /proc/stat in place of getrusage ru_stime value. Should only be used on otherwise idle systems or with high workloads!");
        DEFINE_FLAG(fp, bool,         ipv4,          false,   '4', "Set desired address family to AF_INET");
        DEFINE_FLAG(fp, bool,         ipv6,          false,   '6', "Set desired address family to AF_INET6");
        DEFINE_FLAG(fp, bool,         client,        false,   'c', "Is client?");
        DEFINE_FLAG(fp, bool,         debug,         false,   'd', "Set SO_DEBUG socket option");
        DEFINE_FLAG(fp, bool,         dry_run,       false,   'n', "Turn on dry-run mode");
        DEFINE_FLAG(fp, bool,         pin_cpu,       false,   'U', "Pin threads to CPU cores");
        DEFINE_FLAG(fp, bool,         logtostderr,   false,    0,  "Log to stderr");
        DEFINE_FLAG(fp, bool,         nolog,         false,    0,  "No logging");
        DEFINE_FLAG(fp, bool,         nonblocking,   false,    0,  "Make sure syscalls are all nonblocking");
        DEFINE_FLAG(fp, bool,         freebind,      false,    0,  "Set FREEBIND socket option");
        DEFINE_FLAG(fp, double,       interval,      1.0,     'I', "For how many seconds that a sample is generated");
        DEFINE_FLAG(fp, long long,    max_pacing_rate, 0,     'm', "SO_MAX_PACING_RATE value; use as 32-bit unsigned");
        DEFINE_FLAG_PARSER(fp,        max_pacing_rate, parse_max_pacing_rate);
        DEFINE_FLAG(fp, int,          mark,          0,       'M', "SO_MARK value; use as 32-bit unsigned");
        DEFINE_FLAG(fp, const char *, local_hosts,   NULL,    'L', "Local hostnames or IP addresses");
        DEFINE_FLAG(fp, const char *, host,          NULL,    'H', "Server hostname or IP address");
        DEFINE_FLAG(fp, const char *, control_port,  "12866", 'C', "Server control port");
        DEFINE_FLAG(fp, const char *, port,          "12867", 'P', "Server data port");
        DEFINE_FLAG(fp, const char *, all_samples,   NULL,    'A', "Print all samples? If yes, this is the output file name");
        DEFINE_FLAG_HAS_OPTIONAL_ARGUMENT(fp, all_samples);
        DEFINE_FLAG_PARSER(fp,        all_samples, parse_all_samples);
        DEFINE_FLAG(fp, bool,         time_wait,     false,    0,  "Do not set SO_LINGER 0. Close gracefully. Active peer will enter TIME_WAIT state");

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_tcp(struct flags_parser *fp)
{
        /* Define flags common to all TCP main programs */
        DEFINE_FLAG(fp, int,          num_ports,     1,        0,  "Number of server data ports");
        DEFINE_FLAG(fp, bool,         tcp_fastopen,  false,   'X', "Enable TCP fastopen");
#ifndef NO_LIBNUMA
        DEFINE_FLAG(fp, bool,         pin_numa,       false,  'N', "Pin threads to CPU cores");
#endif
        DEFINE_FLAG(fp, int,          tcp_tx_delay,   0,      't', "Force usec delay in TCP flows");

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_udp(struct flags_parser *fp)
{
        /* Define flags common to all UDP main programs */

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_rr(struct flags_parser *fp)
{
        /* Define flags common to all RR and CRR main programs */
        DEFINE_FLAG(fp, int,                 request_size,  1,                       'Q', "Number of bytes in a request from client to server");
        DEFINE_FLAG(fp, int,                 response_size, 1,                       'R', "Number of bytes in a response from server to client");
        DEFINE_FLAG(fp, struct percentiles,  percentiles,   { .chosen = { false } }, 'p', "Set reported latency percentiles (list)");
        DEFINE_FLAG_PARSER(fp,               percentiles, percentiles_parse);
        DEFINE_FLAG_PRINTER(fp,              percentiles, percentiles_print);
        DEFINE_FLAG(fp, int,                 test_length,   10,                      'l', "Test length, >0 seconds, <0 transactions");
        DEFINE_FLAG(fp, int,                 buffer_size,   65536,                   'B', "Number of bytes that each read()/send() can transfer at once");

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_stream(struct flags_parser *fp)
{
        /* Define flags common to all STREAM main programs */
        DEFINE_FLAG(fp, int,           test_length,     10,      'l', "Test length in seconds");
        DEFINE_FLAG(fp, bool,          edge_trigger,    false,   'E', "Edge-triggered epoll");
        DEFINE_FLAG(fp, bool,          reuseaddr,       false,   'R', "Use SO_REUSEADDR on sockets");
        DEFINE_FLAG(fp, bool,          zerocopy,        false,   'Z', "Set MSG_ZEROCOPY when sending");
        DEFINE_FLAG(fp, const struct rate_conversion *, throughput_opt, neper_units_mb_pointer_hack, 0, "Units to display for throughput");
        DEFINE_FLAG_PARSER(fp,                          throughput_opt, parse_unit);
        DEFINE_FLAG_PRINTER(fp,                         throughput_opt, print_unit);

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_tcp_rr(struct flags_parser *fp)
{
        /* Define flags specialized to only TCP_RR */
        DEFINE_FLAG(fp, unsigned long, delay,           0,       'D', "Nanosecond delay between each send()/write()");
        DEFINE_FLAG(fp, bool,          async_connect,   false,   0,  "use non blocking connect");

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_tcp_crr(struct flags_parser *fp)
{
        /* Define flags specialized to only TCP_CRR */
        DEFINE_FLAG(fp, bool,          async_connect,   true,   0,  "use non blocking connect (default true for historical backward compatibility)");

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_tcp_stream(struct flags_parser *fp)
{
        /* Define flags specialized to only TCP_STREAM */
        DEFINE_FLAG(fp, unsigned long, delay,           0,       'D', "Nanosecond delay between each send()/write()");
        DEFINE_FLAG(fp, int,           buffer_size,     16384,   'B', "Number of bytes that each read/write uses as the buffer");
        DEFINE_FLAG(fp, bool,          skip_rx_copy,    false,    0,  "Skip kernel->user payload copy on receives");
        DEFINE_FLAG(fp, bool,          enable_read,     false,   'r', "Read from flows? enabled by default for the server");
        DEFINE_FLAG(fp, bool,          enable_write,    false,   'w', "Write to flows? Enabled by default for the client");
        DEFINE_FLAG(fp, bool,          enable_tcp_maerts,    false,   'M', "Enables TCP_MAERTS test (server writes and client reads). It overrides enable_read, and enable_write");
        DEFINE_FLAG(fp, bool,          async_connect,   false,   0,  "use non blocking connect");
#ifdef WITH_TCPDIRECT
        DEFINE_FLAG(fp, bool,                        tcpd_validate, false, 0, "Validates that received data is a repeating sequence of 1 to 111 inclusive");
        DEFINE_FLAG(fp, bool,                        tcpd_rx_cpy, false, 0, "After the CUDA buffer is filled to buffer_size, calls cudaMemcpy to a userspace buffer");
        DEFINE_FLAG(fp, const char *,  tcpd_nic_pci_addr, 0,     0,   "NIC PCI addr, e.x. 0000:06:00.0");
        DEFINE_FLAG(fp, const char *,  tcpd_gpu_pci_addr, 0,     0,   "GPU PCI addr, e.x. 0000:04:00.0");
        DEFINE_FLAG(fp, unsigned long long,          tcpdirect_phys_addr, 0, 0, "Set the remote memory physical address for tcpdirect, e.x. 0000:06:00.0");
        DEFINE_FLAG(fp, unsigned long long,          tcpdirect_phys_len, 0, 0, "Set the remote memory length for tcpdirect");
        DEFINE_FLAG(fp, const char *,                tcpdirect_src_ip, 0, 0, "Set the src ip address for tcpdirect");
        DEFINE_FLAG(fp, const char *,                tcpdirect_dst_ip, 0, 0, "Set the dst ip address for tcpdirect");
        DEFINE_FLAG(fp, const char *,                tcpdirect_link_name, "eth1", 0, "Link name to bind DMA buffer_pages for Rx");
        DEFINE_FLAG(fp, int,                         queue_start, 8, 0, "Queue to start flow-steering at");
        DEFINE_FLAG(fp, int,                         queue_num, 4, 0, "Number of queues to flow-steer to");
#endif

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_udp_rr(struct flags_parser *fp)
{
        /* Define flags specialized to only UDP_RR */
        DEFINE_FLAG(fp, unsigned long, delay,           0,       'D', "Nanosecond delay between each send()/write()");

        /* Return the updated fp */
        return (fp);
}

struct flags_parser *add_flags_udp_stream(struct flags_parser *fp)
{
        /* Define flags specialized to only UDP_STREAM */
        DEFINE_FLAG(fp, unsigned long, delay,           0,       'D', "Nanosecond delay between each send()/write()");
        DEFINE_FLAG(fp, int,           buffer_size,     1400,    'B', "Number of bytes that each read/write uses as the buffer");

        /* Return the updated fp */
        return (fp);
}

