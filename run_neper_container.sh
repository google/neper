#!/bin/bash
set -e

NEPER_IMG="gcr.io/a3-tcpd-staging-hostpool/stable/neper"

usage() {
  echo "Starts Neper in a container"
  echo "Usage: $0 -c cuda_lib_dir        default: /var/lib/nvidia/lib64"
  echo "       $0 -i neper_docker_image  default: ${NEPER_IMG}"
  echo "       $0 -h                     print usage guide"
  echo ""
}

while getopts "c:i:h" option; do
  case $option in
    c)
      CUDA_LIB_DIR="${OPTARG}"
      ;;
    i)
      NEPER_IMG="${OPTARG}"
      ;;
    h)
      usage
      exit 0
      ;;
    *)
      usage
      exit 1
      ;;
  esac
done
shift $((OPTIND-1))

: ${CUDA_LIB_DIR:="/var/lib/nvidia/lib64"}

function run_neper_container() {
  docker run \
    --name neper_c \
    --rm \
    -u 0 --network=host \
    --cap-add=IPC_LOCK \
    --userns=host \
    --volume /run/tcpx:/tmp \
    --volume ${CUDA_LIB_DIR}:/usr/local/nvidia/lib64 \
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

sudo iptables -I INPUT -p tcp -m tcp -j ACCEPT

run_neper_container -it "${NEPER_IMG}" $@
