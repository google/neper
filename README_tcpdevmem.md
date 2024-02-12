# Neper with TCPDirect run instructions

Table of Contents
- [Neper with TCPDirect run instructions](#neper-with-tcpdirect-run-instructions)
  - [TCPDirect CUDA: tcp\_stream within Docker container](#tcpdirect-cuda-tcp_stream-within-docker-container)
      - [Note on accessing Neper logs or long-running container:](#note-on-accessing-neper-logs-or-long-running-container)
    - [Building your own image for testing](#building-your-own-image-for-testing)
      - [building the image on your workstation, within the repo directory](#building-the-image-on-your-workstation-within-the-repo-directory)
      - [creating a container on the VM](#creating-a-container-on-the-vm)
    - [Override CUDA library directory (DLVM)](#override-cuda-library-directory-dlvm)
  - [TCPDirect UDMA: Compiling tcp\_stream](#tcpdirect-udma-compiling-tcp_stream)
  - [Running tcp\_stream](#running-tcp_stream)
    - [Added flags](#added-flags)
    - [Running tcp\_stream via `multi_neper.py`](#running-tcp_stream-via-multi_neperpy)
      - [Example of successful output](#example-of-successful-output)
    - [Running tcp\_stream directly](#running-tcp_stream-directly)


## TCPDirect CUDA: tcp_stream within Docker container

```
# On COS VM, do:
FLOWS=2
BUF_SIZE=409600
DEVS=eth1,eth2,eth3,eth4
DSTS=192.168.1.26,192.168.2.26,192.168.3.26,192.168.4.26
SRCS=192.168.1.23,192.168.2.23,192.168.3.23,192.168.4.23

./run_neper_container.sh ./multi_neper.py \
  --hosts $DSTS \
  --devices $DEVS --buffer-size $BUF_SIZE \
  --flows $FLOWS --threads $FLOWS \
  --src-ips $SRCS --log DEBUG \
  --q-num $FLOWS --phys-len 2147483648 \
  --client \
  --mode cuda
```

#### Note on accessing Neper logs or long-running container:

If access to the log files are required (i.e. when using `tcpd-validate` flag and checking the logs for data integrity), you can start the container, then run `multi_neper.py` so that you don't exit out of the container after the Neper run completes.

```
./run_neper_container.sh bash

# within the container
FLOWS=2
BUF_SIZE=409600
DEVS=eth1,eth2,eth3,eth4
DSTS=192.168.1.26,192.168.2.26,192.168.3.26,192.168.4.26
SRCS=192.168.1.23,192.168.2.23,192.168.3.23,192.168.4.23
./multi_neper.py \
  --hosts $DSTS \
  --devices $DEVS --buffer-size $BUF_SIZE \
  --flows $FLOWS --threads $FLOWS \
  --src-ips $SRCS --log DEBUG \
  --q-num $FLOWS --phys-len 2147483648 \
  --client \
  --mode cuda

# grep log files
ls | grep log
```

### Building your own image for testing

#### building the image on your workstation, within the repo directory

```
git clone -b tcpd https://github.com/google/neper.git
cd neper

# copy kernel header files to Neper working directory
# (assumed to be found in ~/cos-kernel/usr/include)
mkdir usr
cp -r ~/cos-kernel/usr/include/ ./usr/

IMAGE_NAME='gcr.io/a3-tcpd-staging-hostpool/$USER/neper'
docker build -t $IMAGE_NAME .
docker push $IMAGE_NAME
```


#### creating a container on the VM

```
IMAGE_NAME='gcr.io/a3-tcpd-staging-hostpool/$USER/neper'

function run_neper_container() {
  docker run \
    --name neper_c \
    -u 0 --network=host \
    --cap-add=IPC_LOCK \
    --userns=host \
    --volume /run/tcpx:/tmp \
    --volume /var/lib/nvidia/lib64:/usr/local/nvidia/lib64 \
    --volume /var/lib/tcpx:/usr/local/tcpx \
    --shm-size=1g --ulimit memlock=-1 --ulimit stack=67108864 \
    --device /dev/nvidia0:/dev/nvidia0 \
    --device /dev/nvidia1:/dev/nvidia1 \
    --device /dev/nvidia2:/dev/nvidia2 \
    --device /dev/nvidia3:/dev/nvidia3 \
    --device /dev/nvidia4:/dev/nvidia4 \
    --device /dev/nvidia5:/dev/nvidia5 \
    --device /dev/nvidia6:/dev/nvidia6 \
    --device /dev/nvidia7:/dev/nvidia7 \
    --device /dev/nvidia-uvm:/dev/nvidia-uvm \
    --device /dev/nvidiactl:/dev/nvidiactl \
    --cap-add=NET_ADMIN \
    --env LD_LIBRARY_PATH=/usr/local/nvidia/lib64:/usr/local/tcpx/lib64 \
    "$@"
}

run_neper_container -it $IMAGE_NAME bash
```


### Override CUDA library directory (DLVM)

The script assumes that `libcuda.so*` files are found in `/var/lib/nvidia/lib64`. In case this isn’t true (like when on DLVM), you can override the default env var: `CUDA_LIB_DIR`:

```
CUDA_LIB_DIR=/usr/lib/x86_64-linux-gnu ./run_neper_container.sh bash
```


## TCPDirect UDMA: Compiling tcp_stream

**UDMA-capable tcp_stream can be built statically on a workstation.**

Neper can be built statically on a host with UDMA header files.

The Makefile assumes that the header files are found at `usr/include` within the Neper directory.

```
# clone the Neper repository and checkout the tcpd branch
git clone -b tcpd https://github.com/google/neper.git
cd neper

# copy kernel header files to Neper working directory
# (assumed to be found in ~/cos-kernel/usr/include)
mkdir usr
cp -r ~/cos-kernel/usr/include/ ./usr/

make tcp_steam WITH_TCPDEVMEM_UDMA=1

# copy the binary to your hosts
scp tcp_stream root@${HOST1}:~/
scp multi_neper.py root@${HOST1}:~/

scp tcp_stream root@${HOST2}:~/
scp multi_neper.py root@${HOST2}:~/
```


## Running tcp_stream


### Added flags

In general, these flags will be automatically populated by `multi_neper.py`.

```
--tcpd-validate     # payload validation - must pass to both Tx/Rx if enabled
--tcpd-tcpd-rx-cpy
--tcpd-nic-pci-addr
--tcpd-gpu-pci-addr
--tcpd-phys-len     # CUDA mode allows for a much larger value than UDMA mode
--tcpd-src-ip
--tcpd-dst-ip
--tcpd-link-name
--queue-start
--queue-num
```

Running TCPDirect requires the toggling of a handful of ethtool commands on the receiver (host). If running tcp_stream via `multi_neper.py`, this will automatically be done before each run.

Otherwise, it might be necessary to run these commands before each tcp_stream run.


### Running tcp_stream via `multi_neper.py`

`multi_neper.py` is a python script that runs in parallel multiple tcp_streams, which is useful when running tcp_stream across multiple pairs of NICs.

The script also calls ethtool commands on the receiver (host) before spawning tcp_streams, to set the receiver into a TCPDirect-capable state.

To view all of `multi_neper.py`’s accepted flags, run `multi_neper.py --help`.


```
# Rx (host)
FLOWS=2
BUF_SIZE=409600
DEVS=eth1,eth2,eth3,eth4
DSTS=192.168.1.26,192.168.2.26,192.168.3.26,192.168.4.26 # host IP addresses
SRCS=192.168.1.23,192.168.2.23,192.168.3.23,192.168.4.23 # client IP addresses
./multi_neper.py --hosts $DSTS \
  --devices $DEVS --buffer-size $BUF_SIZE \
  --flows $FLOWS --threads $FLOWS \
  --src-ips $SRCS --log DEBUG \
  --q-num $FLOWS --phys-len 2147483648 \
  --mode cuda


# Tx (client)
FLOWS=2
BUF_SIZE=409600
DEVS=eth1,eth2,eth3,eth4
DSTS=192.168.1.26,192.168.2.26,192.168.3.26,192.168.4.26
SRCS=192.168.1.23,192.168.2.23,192.168.3.23,192.168.4.23
./multi_neper.py --hosts $DSTS \
  --devices $DEVS --buffer-size $BUF_SIZE \
  --flows $FLOWS --threads $FLOWS \
  --src-ips $SRCS --log DEBUG \
  --q-num $FLOWS --phys-len 2147483648 \
  --client \
  --mode cuda
```

#### Example of successful output

```
DEBUG:root:minflt_end=6037
DEBUG:root:majflt_start=0
DEBUG:root:majflt_end=0
DEBUG:root:nvcsw_start=653
DEBUG:root:nvcsw_end=675141
DEBUG:root:nivcsw_start=2
DEBUG:root:nivcsw_end=1018
DEBUG:root:num_samples=155
DEBUG:root:time_end=613529.729042674
DEBUG:root:correlation_coefficient=1.00
DEBUG:root:throughput=193669.32
DEBUG:root:throughput_units=Mbit/s
DEBUG:root:local_throughput=193669323769
DEBUG:root:remote_throughput=0
DEBUG:root:
[eth1] Throughput (Mb/s): 193551.94
[eth2] Throughput (Mb/s): 193652.69
[eth3] Throughput (Mb/s): 193640.21
[eth4] Throughput (Mb/s): 193669.32
```



### Running tcp_stream directly

**If you’re running Neper outside of the container, make sure to run**

```
sudo -s
```

**before everything. `ethtool` commands and queue-binding is only available to superuser.**

Before running tcp_stream, the ethtool commands that `multi_neper.py` runs should also be run:

```
# run as superuser, if running Neper as root
sudo -s

res_link() {
ethtool --set-priv-flags $1 enable-strict-header-split on
ethtool --set-priv-flags $1 enable-strict-header-split off
ethtool --set-priv-flags $1 enable-header-split off
ethtool --set-rxfh-indir $1 equal 16
ethtool -K $1 ntuple off
ethtool --set-priv-flags $1 enable-strict-header-split off
ethtool --set-priv-flags $1 enable-header-split off
ethtool -K $1 ntuple off
ethtool --set-priv-flags $1 enable-max-rx-buffer-size on
ethtool -K $1 ntuple on
}

# call on each link you plan to run tcp_stream across
res_link eth1
```


You can then run `multi_neper.py` with the `--dry-run` flag, to see what tcp_stream commands the script would run:


```
$ FLOWS=1
$ BUF_SIZE=409600
$ DEVS=eth1
$ DSTS=192.168.1.26
$ SRCS=192.168.1.23
$ ./multi_neper.py --hosts $DSTS \
  --devices $DEVS --buffer-size $BUF_SIZE \
  --flows $FLOWS --threads $FLOWS \
  --src-ips $SRCS --log DEBUG \
  --q-num $FLOWS --phys-len 2147483648 \
  --client \
  --mode cuda \
  --dry-run

DEBUG:root:running on ['eth1']
DEBUG:root:('taskset --cpu-list 2-2 ./tcp_stream -T 1 -F 1 --port 12345 --source-port 12345 --control-port 12866 --buffer-size 409600  -l 10 --num-ports 1 --tcpd-phys-len 2147483648 --tcpd-nic-pci-addr 0000:06:00.0 --tcpd-gpu-pci-addr 0000:04:00.0 -c -H 192.168.1.26', {'CUDA_VISIBLE_DEVICES': '0', ...
```

The script will print the tcp_stream command, as well as the environment variables. The only environment variable that matters is `CUDA_VISIBLE_DEVICES` if running in `cuda` mode, which tells tcp_stream which GPU it should allocate memory on.

You can then reset the receiver, and copy/paste the command:

```
# on Rx (host)
res_link eth1
./multi_neper.py --dry-run ${other_rx_args}

CUDA_VISIBLE_DEVICES=0 ./tcp_stream # copy cmd from previous line


# on Tx (client)
./multi_neper.py --dry-run ${other_tx_args}

CUDA_VISIBLE_DEVICES=0 ./tcp_stream # copy cmd from previous line
```