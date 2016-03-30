# neper

neper is a Linux networking performance tool.

* Support multithreads and multi-flows out of the box.
* Generate workload with epoll.
* Collect statistics in a more accurate way.
* Export statistics to CSV for easier consumption by other tools.

neper currently supports two workloads:

* `tcp_rr` generates request/response workload (similar to HTTP or RPC) over
  TCP
* `tcp_stream` generates bulk data transfer workload (similar to FTP or `scp`)
  over TCP

neper as a small code base with clean coding style and structure, and is
easy to extend with new workloads and/or new options.  It can also be embedded
as a library in a larger application to generate epoll-based workloads.

Disclaimer: This is not an official Google product.

## Basic usage

neper is intended to be used between two machines.  On each machine, the
neper process (e.g. `tcp_rr` or `tcp_stream`) spawns T threads (workers),
creates F flows (e.g. TCP connections), and multiplexes the F flows evenly over
the T threads.  Each thread has a epoll set to manage multiple flows.  Each
flow keeps its own state to make sure the expected workload is generated.

For ease of explanation, we refer to the two machines as the server and the
client.  The server is the process which binds to an endpoint, while the client
is the process which connects to the server.  The ordering of `bind()` and
`connect()` invocations is insignificant, as neper ensures the two processes
are synchronized properly.

When launching the client process, we can specify the number of seconds to
generate network traffic.  After that duration, the client will notify the
server to stop and exit.  To run the test again, we have to restart both the
client and the server.  This is different from netperf, and hopefully should
make individual tests more independent from each other.

### `tcp_rr`

Let's start the server process first:

    server$ tcp_rr
    percentiles=
    all_samples=
    port=12867
    control_port=12866
    host=
    max_pacing_rate=0
    interval=1.000000
    pin_cpu=0
    dry_run=0
    client=0
    buffer_size=65536
    response_size=1
    request_size=1
    test_length=10
    num_threads=1
    num_flows=1
    min_rto=0
    help=0
    total_run_time=10
    *(process waiting here)*

Immediately after the process starts, it prints several `key=value` pairs to
stdout.  They are the command-line option values perceived by neper.  In
this case, they are all default values.  We can use them to verify the options
are parsed correctly, or to reproduce the test procedure from persisted
outputs.

At this point, the server is waiting for a client to connect.  We can continue
by running

    client$ tcp_rr -c -H server
    *(options key-value pairs omitted)*

`-c` is short for `--client` which means "this is the client process".  If `-c`
is not specified, it will be started as a server and call `bind()`.  When `-c`
is specified, it will try to `connect()`, and `-H` (short for `--host`)
specifies the server hostname to connect to.  We can also use IP address
directly, to avoid resolving hostnames (e.g. through DNS).

For both `bind()` and `connect()`, we actually need the port number as well.
In the case of neper, two ports are being used, one for control plane, the
other one for data plane.  Default ports are 12866 for control plane and 12867
for data plane.  They can be overridden by `-C` (short for `--control-port`)
and `-P` (short for `--port`), respectively.  Default port numbers are chosen
so that they don't collide with the port 12865 used by netperf.

Immediately after the client process prints the options, it will connect to the
server and start sending packets.  After a period of time (by default 10
seconds), both processes print statistics summary to stdout and then exit:

    server$ tcp_rr
    *(previous output omitted)*
    invalid_secret_count=0
    time_start=1306173.666348235
    utime_start=0.062141
    utime_end=0.348902
    stime_start=0.003883
    stime_end=5.798208
    maxrss_start=7896
    maxrss_end=7896
    minflt_start=568
    minflt_end=571
    majflt_start=7
    majflt_end=7
    nvcsw_start=26
    nvcsw_end=329455
    nivcsw_start=46
    nivcsw_end=1028
    num_transactions=329605
    start_index=0
    end_index=9
    num_samples=10
    throughput=33009.84
    correlation_coefficient=1.00
    time_end=1306183.666374314

    client$ tcp_rr -c -H server
    *(previous output omitted)*
    *(new output lines are similar to the server)*

From the line `throughput=33009.84`, we know this test run finished 33009.84
request/response "ping-pong" transactions per second.  A transaction for the
client means sending a request and then receiving a response.  A transaction
for the server means receiving a request and then sending a response.  The
number in this example is very high because it was run on localhost.

To look closer, let's reexamine the test parameters (command-line options),
most importantly:

    response_size=1
    request_size=1
    test_length=10
    num_threads=1
    num_flows=1

That means we were using one thread (on each side) with one flow (TCP
connection between server and client) to send one-byte requests and responses
over 10 seconds.

To run the test with 10 flows and two threads, we can instead use

    server$ tcp_rr -F 10 -T 2
    client$ tcp_rr -c -H server -F 10 -T 2

where `-F` is short for `--num-flows` and `-T` is short for `--num-threads`.

That will be 10 flows multiplexed on top of two threads, so normally it's 5
flows per thread.  neper uses `SO_REUSEPORT` to load balance among the
threads, so it might not be exactly 5 flows per thread (e.g. may be 4 + 6).
This behavior might change in the future.

Server and client do not need to use the same number of threads.  For example,
we can create 2 threads on the server to serve requests from 4 threads from the
client.

    server$ tcp_rr -F 10 -T 2
    client$ tcp_rr -c -H server -F 10 -T 4

In this case, the four client-side threads may handle 3 + 3 + 2 + 2 (= 10)
flows respectively.

Also note that we have to specify the number of flows on the server side.  This
behavior might change in the future.

### `tcp_stream`

`tcp_stream` shares most of the command-line options with `tcp_rr`.  They
differ in the output since for a bulk data transfer test like `tcp_stream`, we
care about the throughput in Mbps rather than in number of transactions.

By default, it's the client sending data to the server.  We can enable the
other direction of data transfer (from server to client) by specifying
command-line options `-r` (short for `--enable-read`) and `-w` (short for
`--enable-write`).

    server$ tcp_stream -w
    client$ tcp_stream -c -H server -r

This is equivalent to

    server$ tcp_stream -rw
    client$ tcp_stream -c -H server -rw

since `-w` is auto-enabled for `-c`, and `-r` is auto-enabled when `-c` is
missing.

In both cases, the flows have bidirectional bulk data transfer.  Previously,
netperf users may emulate this behavior with `TCP_STREAM` and `TCP_MAERTS`, at
the cost of doubling the number of netperf processes.

Note that we don't have netperf `TCP_MAERTS` in neper, as you can always
choose where to specify the `-c` option.  The usage model is basically
different, as we don't have a daemon (like netserver) either.

## Options

### Connectivity options

    client
    host
    local_host
    control_port
    port

### Workload options

    maxevents
    num_flows
    num_threads
    test_length
    pin_cpu
    dry_run
    logtostderr
    nonblocking

### Statistics options

    all_samples
    interval

### TCP options

    max_pacing_rate
    min_rto
    listen_backlog

### `tcp_rr` options

    request_size
    response_size
    buffer_size
    percentiles

The output is only available in the detailed form (`samples.csv`) but not in
the stdout summary.

    server$ ./tcp_rr
    client$ ./tcp_rr -c -H server -A --percentiles=25,50,90,95,99
    client$ cat samples.csv
    time,tid,flow_id,bytes_read,transactions,latency_min,latency_mean,latency_max,latency_stddev,latency_p25,latency_p50,latency_p90,latency_p95,latency_p99,utime,stime,maxrss,minflt,majflt,nvcsw,nivcsw
    2766296.649115114,0,0,31726,31726,0.000019,0.000030,0.008010,0.000086,0.000023,0.000026,0.000032,0.000033,0.000068,0.005268,0.479424,5288,71,0,28490,3360
    2766297.649131797,0,0,62857,62857,0.000019,0.000031,0.007757,0.000078,0.000024,0.000027,0.000032,0.000034,0.000080,0.022667,0.933914,5288,133,0,57761,5692
    2766298.649119440,0,0,98525,98525,0.000015,0.000027,0.004187,0.000048,0.000023,0.000025,0.000032,0.000033,0.000048,0.063623,1.481519,5288,204,0,91853,7383
    2766299.649141269,0,0,138042,138042,0.000015,0.000024,0.009910,0.000091,0.000018,0.000018,0.000027,0.000030,0.000041,0.084147,1.984098,5288,283,0,129072,9754
    2766300.649148147,0,0,169698,169698,0.000019,0.000030,0.004938,0.000063,0.000024,0.000027,0.000034,0.000036,0.000057,0.119381,2.493741,5288,346,0,160027,10551
    2766301.649127545,0,0,202454,202454,0.000019,0.000029,0.006942,0.000060,0.000025,0.000027,0.000032,0.000032,0.000060,0.165496,2.920798,5288,411,0,186603,16817
    2766302.649152705,0,0,234954,234954,0.000018,0.000029,0.012611,0.000100,0.000025,0.000026,0.000031,0.000032,0.000059,0.205488,3.349022,5288,475,0,212910,23195
    2766303.649116145,0,0,269683,269683,0.000019,0.000027,0.004842,0.000038,0.000024,0.000026,0.000031,0.000032,0.000048,0.242531,3.806882,5288,544,0,240914,30076
    2766304.649131298,0,0,302011,302011,0.000019,0.000030,0.004476,0.000049,0.000025,0.000029,0.000032,0.000033,0.000044,0.253141,4.294832,5288,608,0,270468,32944
    2766305.649132278,0,0,340838,340838,0.000015,0.000025,0.000220,0.000006,0.000022,0.000025,0.000031,0.000033,0.000035,0.284624,4.808422,5288,685,0,308307,34005

### `tcp_stream` options

    reuseaddr
    enable_read
    enable_write
    epoll_trigger
    delay
    buffer_size

## Output format

When consuming the key-value pairs in the output, the order of the keys should
be insignificant.  However, the keys are case sensitive.

### Standard output keys

    total_run_time # expected time to finish, useful when combined with --dry-run
    invalid_secret_count
    time_start
    start_index
    end_index
    num_samples
    time_end
    rusage
    utime_start
    utime_end
    stime_start
    stime_end
    maxrss_start
    maxrss_end
    minflt_start
    minflt_end
    majflt_start
    majflt_end
    nvcsw_start
    nvcsw_end
    nivcsw_start
    nivcsw_end

#### `tcp_rr`

    num_transactions
    throughput
    correlation_coefficient # for throughput

#### `tcp_stream`

    num_transactions
    throughput_Mbps
    correlation_coefficient # for throughput_Mbps

## Contribution guidelines

* C99, avoid compiler-specific extensions.
* No external dependency.
* Linux coding style, with tabs expanded to 8 spaces.
