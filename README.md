# neper

neper is a Linux networking performance tool.

* Support multithreads and multi-flows out of the box.
* Generate workload with epoll.
* Collect statistics in a more accurate way.
* Export statistics to CSV for easier consumption by other tools.

neper currently supports eight workloads:

* `tcp_rr` generates request/response workload (similar to HTTP or RPC) over
  TCP
* `tcp_crr` is similar to tcp_rr but establishes a fresh connection for each
  request/response pair
* `tcp_stream` generates bulk data transfer workload (similar to FTP or `scp`)
  over TCP
* `udp_rr` generates request/response workload (similar to HTTP or RPC) over
  UDP
* `udp_stream` generates bulk data transfer workload (similar to FTP or `scp`)
  over UDP
* `psp_stream` generates bulk data transfer workload (similar to FTP or `scp`)
  over TCP-PSP
* `psp_rr` generates request/response workload (similar to HTTP or RPC) over
  TCP-PSP
* `psp_crr` is similar to psp_rr but establishes a fresh connection for each
  request/response pair

neper as a small code base with clean coding style and structure, and is
easy to extend with new workloads and/or new options.  It can also be embedded
as a library in a larger application to generate epoll-based workloads.

Disclaimer: This is not an official Google product.

neper was at one time internally known as gperfnet.  This documentation may use both names
interchangeably.

This file documents the usage of neper as a tool.

Table of contents:

[TOC]

## Install

Currently neper consists of eight binaries, `tcp_rr` and `tcp_stream` and
`tcp_crr` and `udp_rr` and `udp_stream` and `psp_stream` and `psp_crr` and
`psp_rr`, corresponding to eight workloads.

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
generate network traffic "-l, --test-length".  After that duration, the client
will notify the server to stop and exit.  To run the test again, we have to
restart both the client and the server.  This is different from netperf, and
hopefully should make individual tests more independent from each other.

Optionally, a negative value for "-l, --test-length", a la netperf, can be
specified where the absolute value is used for rr test to specify the number of
transactions instead of time.  The transactions are distributed across flows on
demand.  Note, the "-s, --suicide-length" option can be used to specify a time
limit.  Also, the negative value isn't currently supported for all tests, e.g.
stream test, support for tests other than rr maybe added in the future.

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
so that they don't collide with the port 12865 used by netperf. Note that for
UDP there is one data port per flow, allocated in a sequential range on the
server.

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

### `psp_crr` and `psp_rr`

`psp_crr` and `psp_rr` shares all the command-line options and output with
`tcp_crr` and `tcp_rr`.  They differ in the secure socket setup and the ensuing
encryption/decryption operations performed by the PSP-capable hardwares.  More
details on PSP are available in: [google/psp](https://github.com/google/psp).

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

To have netperf `TCP_MAERTS` test, pass `-M` which overrides `-r` and `-w`.

    server$ tcp_stream -M
    client$ tcp_stream -c -H server -M

### `psp_stream`

`psp_stream` shares all the command-line options and output with `tcp_stream`.
They differ in the secure socket setup and the ensuing encryption/decryption
operations performed by the PSP-capable hardwares.  More details on PSP are
available in: [google/psp](https://github.com/google/psp).

## Options

### Connectivity options

    client
    host
    local_hosts
    control_port
    port
    mark

### Workload options

    maxevents
    num_flows
    num_threads
    test_length
    pin_cpu
    dry_run
    logtostderr
    nonblocking
    buffer_size

### Statistics options

    all_samples
    interval

### TCP options

    max_pacing_rate
    listen_backlog
    tcp_tx_delay

### `tcp_rr` and `tcp_crr` options

    request_size
    response_size
    percentiles
    num_ports

### `psp_rr` and `psp_crr` options

    request_size
    response_size
    percentiles
    num_ports

### `udp_rr` options

    request_size
    response_size
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
    throughput_opt
    zerocopy

### `udp_stream` options

    reuseaddr
    delay
    throughput_opt
    zerocopy

### `psp_stream` options

    reuseaddr
    enable_read
    enable_write
    epoll_trigger
    delay
    throughput_opt
    zerocopy

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

#### `tcp_rr` and `tcp_crr`

    num_transactions
    num_ports
    throughput
    correlation_coefficient # for throughput

#### `psp_rr` and `psp_crr`

    num_transactions
    num_ports
    throughput
    correlation_coefficient # for throughput

#### `tcp_stream`

    num_transactions
    throughput_Mbps
    correlation_coefficient # for throughput
    throughput_units
    throughput

#### `udp_rr`

    num_transactions
    throughput
    correlation_coefficient # for throughput

#### `udp_stream`

    num_transactions
    throughput_Mbps
    correlation_coefficient # for throughput
    throughput_units
    throughput

#### `psp_stream`

    num_transactions
    throughput_Mbps
    correlation_coefficient # for throughput
    throughput_units
    throughput

## Compile Neper

To compile `neper`, run `make` or `make all` on your CLI. This will create all the eight binaries, `tcp_rr`, `tcp_stream`, `tcp_crr`, `udp_rr`, `udp_stream`, `psp_stream`, `psp_crr` and `psp_rr` for you.

You can enable the use of `epoll_pwait2()` by passing defination `-DHAVE_EPOLL_PWAIT2` under `CFLAGS` of `Makefile`. Before enabling it make sure that your libc has `epoll_pwait2()` support.

To build a docker image, run `make image` from your CLI.

## Running in Kubernetes

When running `neper` in Kubernetes for network performance testing, it is important to ensure that the client and server pods are running on different nodes. This provides a more realistic network path and avoids the test being skewed by the node's internal loopback traffic.

For quick, ad-hoc tests, you can use `kubectl run` to launch the server and client pods directly, using a `nodeSelector` to force them onto specific nodes.
For more permanent or complex testing scenarios, it is recommended to use more advanced Kubernetes capabilities by using `Deployments` and `Services`.

### Identify Your Nodes

First, list the nodes in your cluster to choose where to place the server and client pods.

    kubectl get nodes

You will see an output with your node names, like `node-1` and `node-2`.

### Launch the neper Server

Launch the server pod on one of your nodes (e.g., node-1). The --overrides flag is used here to inject the nodeSelector into the pod specification.

    kubectl run neper-server --image=ghcr.io/google/neper:stable \
    --command \
    --overrides='{"apiVersion": "v1", "spec": {"nodeSelector": { "kubernetes.io/hostname": "node-1" }}}' \
    -- tcp_rr

### Launch the neper Client

Next, get the IP address of the server pod you just created.

    SERVER_IP=$(kubectl get pod neper-server -o jsonpath='{.status.podIP}')

Now, launch the client pod on a different node (e.g., node-2), and pass the server's IP address to it using the -H flag.

    kubectl run neper-client --image=ghcr.io/google/neper:stable \
    --command \
    --overrides='{"apiVersion": "v1", "spec": {"nodeSelector": { "kubernetes.io/hostname": "node-2" }}}' \
    -- tcp_rr -c -H $SERVER_IP

### View the Results

The Pods will run the tests and finish once they are completed:

    kubectl get pods
    NAME              READY   STATUS      RESTARTS      AGE
    neper-client      0/1     Completed   1 (18s ago)   31s
    neper-server      0/1     Completed   1 (18s ago)   2m37s

You can now tail the logs of the client pod to see the performance test results in real-time. The client pod will automatically exit when the test is complete.

    kubectl logs -f neper-client

### Interacting with the Pods

You can also open an interactive shell for more exploratory or advanced testing. For example, to get a shell into a client pod:

    kubectl run -it neper-client --image=ghcr.io/google/neper:stable \
    --command \
    --overrides='{"apiVersion": "v1", "spec": {"nodeSelector": { "kubernetes.io/hostname": "node-2" }}}' \
    -- sh

When you are finished, you can delete the pods:

    kubectl delete pod neper-server neper-client

## Contribution guidelines

* C99, avoid compiler-specific extensions.
* No external dependency.
* Linux coding style, with tabs expanded to 8 spaces.

## Changelog (not comprehensive)

### 2024-02-21

* **Breaking**: changed histogram implementation