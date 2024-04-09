FROM nvidia/cuda:12.0.0-devel-ubuntu20.04

ENV DEBIAN_FRONTEND='noninteractive'

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
        git openssh-server wget iproute2 vim libopenmpi-dev build-essential cmake gdb \
  protobuf-compiler libprotobuf-dev rsync libssl-dev \
  && rm -rf /var/lib/apt/lists/*

ARG CUDA12_GENCODE='-gencode=arch=compute_90,code=sm_90'
ARG CUDA12_PTX='-gencode=arch=compute_90,code=compute_90'

WORKDIR /third_party

# this assumes that kernel hdr files have been copied into ${neper_dir}/usr/,
# which will then be copied into the container
COPY . /third_party
RUN make tcp_stream WITH_TCPDEVMEM_CUDA=1

RUN chmod +777 /tmp
RUN apt-get update
RUN apt-get install -y python3 sysstat ethtool
