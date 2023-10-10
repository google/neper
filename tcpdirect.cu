#ifdef WITH_TCPDIRECT
#include <cuda.h>
#include <cuda_runtime.h>

#include <asm-generic/errno-base.h>
#include <asm-generic/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/dma-buf.h>

#include <memory>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "tcpdirect.h"
#include "logging.h"
#include "flow.h"
#include "thread.h"

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY	0x4000000
#endif

#define LAST_PRIME 111

#define MIN_RX_BUFFER_TOTAL_SIZE (1 << 28)
#define GPUMEM_ALIGNMENT (1UL << 21)
#define GPUMEM_MINSZ 0x400000
#define PAGE_SHIFT (12)
#define PAGE_SIZE (1 << PAGE_SHIFT)

#define multiplier (1 << 16)

#define TEST_PREFIX "ncdevmem"
#define NUM_PAGES 16000

/* missing definitions in mman-linux.h */
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 2U
#endif

/* GRTE libraries from google3 already define the following */
#ifndef F_SEAL_SHRINK
#define F_SEAL_SHRINK 2U
#endif
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033U
#endif
#ifndef F_GET_SEALS
#define F_GET_SEALS 1034U
#endif

#define MSG_SOCK_DEVMEM 0x2000000
#define SO_DEVMEM_DONTNEED 97
#define SO_DEVMEM_HEADER 98
#define SCM_DEVMEM_HEADER SO_DEVMEM_HEADER
#define SO_DEVMEM_OFFSET 99
#define SCM_DEVMEM_OFFSET SO_DEVMEM_OFFSET

struct dma_buf_create_pages_info {
  __u64 pci_bdf[3];
  __s32 dma_buf_fd;
  __s32 create_page_pool;
};

struct dma_buf_pages_bind_rx_queue {
  char ifname[IFNAMSIZ];
  __u32 rxq_idx;
};

#define DMA_BUF_CREATE_PAGES \
  _IOW(DMA_BUF_BASE, 2, struct dma_buf_create_pages_info)

#define DMA_BUF_PAGES_BIND_RX \
  _IOW(DMA_BUF_BASE, 3, struct dma_buf_pages_bind_rx_queue)

// devmemvec represents a fragment of payload that is received on the socket.
struct devmemvec {
  // frag_offset is the offset in the registered memory.
  __u32 frag_offset;
  // frag size is the size of the payload.
  __u32 frag_size;
  // frag_token is an identifier for this fragment and it can be used to return
  // the memory back to kernel.
  __u32 frag_token;
};

// devmemtoken represents a range of tokens. It is used to return the fragment
// memory back to the kernel.
struct devmemtoken {
  __u32 token_start;
  __u32 token_count;
};

struct udmabuf_create {
  uint32_t memfd;
  uint32_t flags;
  uint64_t offset;
  uint64_t size;
};
#define UDMABUF_CREATE _IOW('u', 0x42, struct udmabuf_create)

int memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

/* Fills buf of size n with a repeating sequence of 1 to 111 inclusive
 */
void fill_tx_buffer(void *buf, size_t n) {
#define BUFSIZE 3996
  unsigned char src_buf[BUFSIZE];
  int ptr = 0, i = 0;

  while (i < BUFSIZE) {
    src_buf[i] = (i % LAST_PRIME) + 1;
    i++;
  }

  while (ptr*BUFSIZE + BUFSIZE < n) {
    cudaMemcpy((char *)buf + ptr*BUFSIZE, &src_buf, BUFSIZE, cudaMemcpyHostToDevice);
    ptr++;
  }

  i = ptr*BUFSIZE;
  while (i < n) {
    cudaMemset((char *)buf + i, (i % LAST_PRIME) + 1, 1);
    i++;
  }
}

int tcpdirect_setup_socket(int socket) {
  const int one = 1;
  if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))
      || setsockopt(socket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one))
      || setsockopt(socket, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one))
     ) {
    perror("tcpdirect_setup_socket");
    exit(EXIT_FAILURE);
  }

  return 0;
}

int get_gpumem_dmabuf_pages_fd(const std::string& gpu_pci_addr,
                               const std::string& nic_pci_addr, void* gpu_mem,
                               size_t gpu_mem_sz, int* dma_buf_fd, bool is_client) {
  int err, ret;

  cuMemGetHandleForAddressRange((void*)dma_buf_fd, (CUdeviceptr)gpu_mem,
                                gpu_mem_sz, CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD,
                                0);

  if (*dma_buf_fd < 0) {
    perror("cuMemGetHandleForAddressRange() failed!: ");
    exit(EXIT_FAILURE);
  }

  printf("Registered dmabuf region 0x%p of %lu Bytes\n",
      gpu_mem, gpu_mem_sz);

  struct dma_buf_create_pages_info frags_create_info;
  frags_create_info.dma_buf_fd = *dma_buf_fd;
  frags_create_info.create_page_pool = is_client ? 0 : 1;

  uint16_t pci_bdf[3];
  ret = sscanf(nic_pci_addr.c_str(), "0000:%hx:%hx.%hx", &pci_bdf[0],
               &pci_bdf[1], &pci_bdf[2]);
  frags_create_info.pci_bdf[0] = pci_bdf[0];
  frags_create_info.pci_bdf[1] = pci_bdf[1];
  frags_create_info.pci_bdf[2] = pci_bdf[2];
  if (ret != 3) {
    err = -EINVAL;
    goto err_close_dmabuf;
  }

  ret = ioctl(*dma_buf_fd, DMA_BUF_CREATE_PAGES, &frags_create_info);
  if (ret < 0) {
    perror("Error getting dma_buf frags: ");
    err = -EIO;
    goto err_close_dmabuf;
  }
  return ret;

err_close_dmabuf:
  close(*dma_buf_fd);
  return err;
}

int tcpdirect_cuda_setup_alloc(const struct options *opts, void **f_mbuf, struct thread *t)
{
  bool is_client = opts->client;
  int ret;
  void *gpu_tx_mem_;
  int gpu_mem_fd_;
  int dma_buf_fd_;
  int q_start = opts->queue_start;
  int q_num = opts->queue_num;
  struct tcpdirect_cuda_mbuf *tmbuf;
  const char *gpu_pci_addr = opts->tcpd_gpu_pci_addr;  // "0000:04:00.0"
  const char *nic_pci_addr = opts->tcpd_nic_pci_addr;  // "0000:06:00.0"
  size_t alloc_size = opts->tcpdirect_phys_len;

  tmbuf =
    (struct tcpdirect_cuda_mbuf *)calloc(1, sizeof(struct tcpdirect_cuda_mbuf));
  if (!tmbuf) {
    exit(EXIT_FAILURE);
  }

  if (alloc_size % GPUMEM_ALIGNMENT != 0) {
    alloc_size += GPUMEM_ALIGNMENT - (alloc_size % GPUMEM_ALIGNMENT);
  }

  // unnecessary if CUDA_VISIBLE_DEVICES env var is set
  // ret = cudaSetDevice(opts->tcpdirect_gpu_idx);
  // if (ret != 0) {
  //   printf("cudaSetDevice failed: index %i", opts->tcpdirect_gpu_idx);
  //   exit(70);
  // }

  cudaMalloc(&gpu_tx_mem_, alloc_size);
  if (is_client && opts->tcpd_validate) {
          fill_tx_buffer(gpu_tx_mem_, alloc_size);
          cudaDeviceSynchronize();
  }
  unsigned int flag = 1;
  cuPointerSetAttribute(&flag,
                        CU_POINTER_ATTRIBUTE_SYNC_MEMOPS,
                        (CUdeviceptr)gpu_tx_mem_);

  gpu_mem_fd_ = get_gpumem_dmabuf_pages_fd(gpu_pci_addr, nic_pci_addr,
                                           gpu_tx_mem_, alloc_size,
                                           &dma_buf_fd_, is_client);

  if (gpu_mem_fd_ < 0) {
    printf("get_gpumem_dmabuf_pages_fd() failed!: ");
    exit(71);
  }

  if (!is_client) {
    int num_queues = q_start + (t->index % q_num);
    printf("Bind to queue %i\n", num_queues);
    struct dma_buf_pages_bind_rx_queue bind_cmd;

    strcpy(bind_cmd.ifname, opts->tcpdirect_link_name);
    bind_cmd.rxq_idx = num_queues;

    ret = ioctl(gpu_mem_fd_, DMA_BUF_PAGES_BIND_RX, &bind_cmd);
    if (ret < 0) {
      printf("%s: [FAIL, bind fail queue=%d]\n", TEST_PREFIX,
            num_queues);
      exit(78);
    }

    // copied from socket.c#socket_connect_one()
    int flow_idx = (t->flow_first + t->flow_count);
    int src_port = flow_idx + opts->source_port;
    int dst_port = flow_idx + atoi(opts->port);

    char flow_steer_cmd[512];
    sprintf(flow_steer_cmd,
            "ethtool -N %s flow-type tcp4 src-ip %s dst-ip %s src-port %i dst-port %i queue %i",
            opts->tcpdirect_link_name, opts->tcpdirect_src_ip, opts->tcpdirect_dst_ip, src_port, dst_port, num_queues);
    ret = system(flow_steer_cmd);

    // only running the below ethtool commands after last thread/flow is setup
    if (flow_idx + t->flow_limit >= opts->num_flows) {
      char ethtool_cmd[512];
      sprintf(ethtool_cmd, "ethtool --set-priv-flags %s enable-strict-header-split on", opts->tcpdirect_link_name);
      ret = ret | system(ethtool_cmd);
      sprintf(ethtool_cmd, "ethtool --set-priv-flags %s enable-header-split on", opts->tcpdirect_link_name);
      ret = ret | system(ethtool_cmd);
      sprintf(ethtool_cmd, "ethtool --set-rxfh-indir %s equal 8", opts->tcpdirect_link_name);
      ret = ret | system(ethtool_cmd);
      printf("ethtool cmds returned %i, sleeping 1...\n", ret);
      sleep(1);
    }
  }

  *f_mbuf = tmbuf;
  tmbuf->gpu_mem_fd_ = gpu_mem_fd_;
  tmbuf->dma_buf_fd_ = dma_buf_fd_;
  tmbuf->gpu_tx_mem_ = gpu_tx_mem_;
  tmbuf->cpy_buffer = malloc(opts->buffer_size);
  tmbuf->vectors = new std::vector<devmemvec>();
  tmbuf->tokens = new std::vector<devmemtoken>();
  tmbuf->bytes_received = 0;
  tmbuf->bytes_sent = 0;
  return 0;
}

int udmabuf_setup_alloc(const struct options *opts, void **f_mbuf) {
  bool is_client = opts->client;
  int devfd;
  int memfd;
  int buf;
  int buf_pages;
  int ret;
  size_t size = opts->tcpdirect_phys_len;

  struct tcpdirect_udma_mbuf *tmbuf;
  struct dma_buf_create_pages_info pages_create_info;
  struct udmabuf_create create;

  if (f_mbuf == NULL) return ENOMEM;

  if (*f_mbuf) return 0;

  tmbuf = (struct tcpdirect_udma_mbuf *)calloc(1, sizeof(struct tcpdirect_udma_mbuf));
  if (!tmbuf) {
    exit(EXIT_FAILURE);
  }

  devfd = open("/dev/udmabuf", O_RDWR);
  if (devfd < 0) {
    printf("%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
           TEST_PREFIX);
    exit(70);
  }

  memfd = memfd_create("udmabuf-test", MFD_ALLOW_SEALING);
  if (memfd < 0) {
    printf("%s: [skip,no-memfd]\n", TEST_PREFIX);
    exit(72);
  }

  ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
  if (ret < 0) {
    printf("%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);
    exit(73);
  }

  ret = ftruncate(memfd, size);
  if (ret == -1) {
    printf("%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);
    exit(74);
  }

  memset(&create, 0, sizeof(create));

  create.memfd = memfd;
  create.offset = 0;
  create.size = size;
  printf("size=%lu\n", size);
  buf = ioctl(devfd, UDMABUF_CREATE, &create);
  if (buf < 0) {
    printf("%s: [FAIL, create udmabuf]\n", TEST_PREFIX);
    exit(75);
  }

  pages_create_info.dma_buf_fd = buf;
  pages_create_info.create_page_pool = is_client ? 0 : 1;

  /* TODO: hardcoded NIC pci address */
  // "0000:06:00.0"
  ret = sscanf(opts->tcpd_nic_pci_addr, "0000:%llx:%llx.%llx",
         &pages_create_info.pci_bdf[0],
         &pages_create_info.pci_bdf[1],
         &pages_create_info.pci_bdf[2]);

  if (ret != 3) {
    printf("%s: [FAIL, parse fail]\n", TEST_PREFIX);
    exit(76);
  }

  buf_pages = ioctl(buf, DMA_BUF_CREATE_PAGES, &pages_create_info);
  if (buf_pages < 0) {
    perror("ioctl DMA_BUF_CREATE_PAGES: [FAIL, create pages fail]\n");
    exit(77);
  }

  if (!is_client) {
    /* TODO hardcoded num_queues */
    int num_queues = 15;
    struct dma_buf_pages_bind_rx_queue bind_cmd;

    strcpy(bind_cmd.ifname, "eth1");
    bind_cmd.rxq_idx = num_queues;

    ret = ioctl(buf_pages, DMA_BUF_PAGES_BIND_RX, &bind_cmd);
    if (ret < 0) {
      printf("%s: [FAIL, bind fail queue=%d]\n", TEST_PREFIX,
            num_queues);
      exit(78);
    }

    system("ethtool --set-priv-flags eth1 enable-header-split on");
    system("ethtool --set-priv-flags eth1 enable-header-split off");
	  system("ethtool --set-priv-flags eth1 enable-header-split on");
    sleep(1);
    printf("toggled header-split\n");
  }

  struct dma_buf_sync sync = { 0 };
  sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_START;
  ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);

  *f_mbuf = tmbuf;

  tmbuf->devfd = devfd;
  tmbuf->memfd = memfd;
  tmbuf->buf = buf;
  tmbuf->buf_pages = buf_pages;
  return 0;
}

int tcpdirect_udma_send(int socket, void *f_mbuf, size_t n, int flags) {
  int buf_pages, buf;
  struct iovec iov;
  struct msghdr *msg;
  struct cmsghdr *cmsg;
  char buf_dummy[n];
  char offsetbuf[CMSG_SPACE(sizeof(uint32_t) * 2)];
  struct tcpdirect_udma_mbuf *tmbuf;

  if (!f_mbuf) return -1;

  tmbuf = (struct tcpdirect_udma_mbuf *)f_mbuf;
  buf_pages = tmbuf->buf_pages;
  buf = tmbuf->buf;
  msg = &tmbuf->msg;

  struct dma_buf_sync sync = { 0 };
  sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_START;
  ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);

  char *buf_mem = NULL;
  buf_mem = (char *)mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, buf, 0);
  if (buf_mem == MAP_FAILED) {
    perror("mmap()");
    exit(1);
  }

  memcpy(buf_mem, buf_dummy, n);

  sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_END;
  ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);

  munmap(buf_mem, n);

  memset(msg, 0, sizeof(struct msghdr));
  // memset(cmsg, 0, sizeof(struct cmsghdr));

  iov.iov_base = buf_dummy;
  iov.iov_len = n;

  msg->msg_iov = &iov;
  msg->msg_iovlen = 1;

  msg->msg_control = offsetbuf;
  msg->msg_controllen = sizeof(offsetbuf);

  cmsg = CMSG_FIRSTHDR(msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_DEVMEM_OFFSET;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 2);
  *((int*)CMSG_DATA(cmsg)) = buf_pages;
  ((int*)CMSG_DATA(cmsg))[1] = 0;

  ssize_t bytes_sent = sendmsg(socket, msg, MSG_ZEROCOPY);
  if (bytes_sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
    perror("sendmsg() error: ");
    exit(EXIT_FAILURE);
  }

  if (bytes_sent == 0) {
    perror("sendmsg() sent 0 bytes. Something is wrong.\n");
    exit(EXIT_FAILURE);
  }

  return bytes_sent;
}

int tcpdirect_send(int socket, void *buf, size_t n, int flags) {
  int gpu_mem_fd_;
  struct iovec iov;
  struct msghdr msg;
  struct cmsghdr *cmsg;
  char offsetbuf[CMSG_SPACE(sizeof(uint32_t) * 2)];
  struct tcpdirect_cuda_mbuf *tmbuf;

  if (!buf) return -1;

  tmbuf = (struct tcpdirect_cuda_mbuf *)buf;
  gpu_mem_fd_ = tmbuf->gpu_mem_fd_;

  memset(&msg, 0, sizeof(msg));
  // memset(cmsg, 0, sizeof(struct cmsghdr));

  iov.iov_base = NULL;
  iov.iov_len = n - tmbuf->bytes_sent;

  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  msg.msg_control = offsetbuf;
  msg.msg_controllen = sizeof(offsetbuf);

  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_DEVMEM_OFFSET;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 2);
  *((int*)CMSG_DATA(cmsg)) = gpu_mem_fd_;
  ((int *)CMSG_DATA(cmsg))[1] = (int)tmbuf->bytes_sent;

  ssize_t bytes_sent = sendmsg(socket, &msg, MSG_ZEROCOPY | MSG_DONTWAIT);
  if (bytes_sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
    perror("sendmsg() error: ");
    exit(EXIT_FAILURE);
  }

  if (bytes_sent == 0) {
    perror("sendmsg() sent 0 bytes. Something is wrong.\n");
    exit(EXIT_FAILURE);
  }

  tmbuf->bytes_sent += bytes_sent;
  if (tmbuf->bytes_sent == n)
    tmbuf->bytes_sent = 0;

  return bytes_sent;
}

int tcpdirect_recv(int socket, void *f_mbuf, size_t n, int flags, struct thread *t) {
  struct iovec iov;
  struct msghdr msg_local;
  struct msghdr *msg;
  struct tcpdirect_cuda_mbuf *tmbuf;
  int ret, client_fd; // buf
  int buffer_size = n;
  size_t total_received = 0;
  unsigned char *cpy_buffer;
  const struct options *opts = t->opts;
  std::vector<devmemvec> *vectors;
  std::vector<devmemtoken> *tokens;

  if (!f_mbuf) return -1;

  tmbuf = (struct tcpdirect_cuda_mbuf *)f_mbuf;
  cpy_buffer = (unsigned char *)tmbuf->cpy_buffer;
  vectors = (std::vector<devmemvec> *)tmbuf->vectors;
  tokens = (std::vector<devmemtoken> *)tmbuf->tokens;

  client_fd = socket;

  char buf_dummy[n];
  // char offsetbuf[CMSG_SPACE(sizeof(uint32_t) * 128)];
  char offsetbuf[CMSG_SPACE(sizeof(int) * 1000)];
  msg = &msg_local;

  memset(msg, 0, sizeof(struct msghdr));

  iov.iov_base = buf_dummy;
  iov.iov_len = n - tmbuf->bytes_received;
  msg->msg_iov = &iov;
  msg->msg_iovlen = 1;

  msg->msg_control = offsetbuf;
  msg->msg_controllen = sizeof(offsetbuf);

  // char *buf_mem = NULL;

  if (msg->msg_flags & MSG_CTRUNC) {
    printf("fatal, cmsg truncated, current msg_controllen\n");
 }

  ssize_t received = recvmsg(socket, msg, MSG_SOCK_DEVMEM | MSG_DONTWAIT);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
  } else if (received < 0) {
    printf("%s %d\n", __func__, __LINE__);
    return -1;
  } else if (received == 0) {
    printf("Client exited\n");
  }

  struct cmsghdr *cm = NULL;
  struct devmemvec *devmemvec = NULL;
  for (cm = CMSG_FIRSTHDR(msg); cm; cm = CMSG_NXTHDR(msg, cm)) {
    if (cm->cmsg_level != SOL_SOCKET ||
        (cm->cmsg_type != SCM_DEVMEM_OFFSET &&
          cm->cmsg_type != SCM_DEVMEM_HEADER)) {
      continue;
    }

    devmemvec = (struct devmemvec *)CMSG_DATA(cm);

    if (cm->cmsg_type == SCM_DEVMEM_HEADER) {
      // TODO: process data copied from skb's linear
      // buffer.
      fprintf(stderr, "\n\nSCM_DEVMEM_HEADER. devmemvec->frag_size=%u\n",
              devmemvec->frag_size);
      exit(1);
    }

    struct devmemtoken token = { devmemvec->frag_token, 1 };

    // struct dma_buf_sync sync = { 0 };
    // sync.flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_START;
    // ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);

    // buf_mem = (char *)mmap(NULL, n, PROT_READ | PROT_WRITE,
    //                MAP_SHARED, buf, 0);
    // if (buf_mem == MAP_FAILED) {
    //   perror("mmap()");
    //   exit(1);
    // }
    total_received += devmemvec->frag_size;
    // printf("\n\nreceived frag_page=%u, in_page_offset=%u,"
    //         " frag_offset=%u, frag_size=%u, token=%u"
    //         " total_received=%lu\n",
    //         devmemvec->frag_offset >> PAGE_SHIFT,
    //         devmemvec->frag_offset % PAGE_SIZE,
    //         devmemvec->frag_offset, devmemvec->frag_size,
    //         devmemvec->frag_token,
    //         total_received);

    // sync.flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_END;
    // ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);
    vectors->emplace_back(*devmemvec);
    tokens->push_back(token);
    // munmap(buf_mem, n);
  }

  tmbuf->bytes_received += received;

  /* Once we've received fragments totaling buffer_size, we can copy from the
   * CUDA buffer to a user-space buffer, and free the fragments in the CUDA
   * buffer.
  */
  if (tmbuf->bytes_received == buffer_size) {
    /* There is a performance impact when we cudaMemcpy from the CUDA buffer to
     * the userspace buffer, so it's gated by a flag
     */
    if (opts->tcpd_rx_cpy || opts->tcpd_validate) {
      for (int idx = 0; idx < vectors->size(); idx++) {
        struct devmemvec vec = (*vectors)[idx];
        struct devmemtoken token = (*tokens)[idx];

        /* copy each fragment to the cpy_buffer in order, i.e.
         * 1st fragment will occuply bytes [0-4095], 2nd fragment will
         * occupy bytes [4096-8191], etc.
         */
        cudaMemcpy(cpy_buffer + (vec.frag_token - 1) * PAGE_SIZE,
                   (char *)tmbuf->gpu_tx_mem_ + vec.frag_offset,
                   vec.frag_size,
                   cudaMemcpyDeviceToHost);
      }

      /* Ensure the sequence is what we expect:
       * a repeating sequence of 1 to LAST_PRIME inclusive
       */
      if (opts->tcpd_validate) {
        cudaDeviceSynchronize();
        int i = 0;
        int expected_val;
        while (i < buffer_size) {
          expected_val = (i % LAST_PRIME) + 1;
          if (cpy_buffer[i] != expected_val) {
            printf("Thread %i - incorrect byte %i, expected %i, got %i\n",
                  t->index,
                  i,
                  expected_val,
                  cpy_buffer[i]);
            break;
          }
          i++;
        }
      }
    }

    ret = setsockopt(client_fd, SOL_SOCKET,
                      SO_DEVMEM_DONTNEED, tokens->data(),
                      tokens->size() * sizeof(devmemtoken));
    if (ret) {
      perror("DONTNEED failed");
      exit(1);
    }
    vectors->clear();
    tokens->clear();
    tmbuf->bytes_received = 0;
  }
  return total_received;
}

int cuda_flow_cleanup(void *f_mbuf) {
  struct tcpdirect_cuda_mbuf *t_mbuf = (struct tcpdirect_cuda_mbuf *)f_mbuf;
  close(t_mbuf->gpu_mem_fd_);
  close(t_mbuf->dma_buf_fd_);
  cudaFree(t_mbuf->gpu_tx_mem_);
  free(t_mbuf->cpy_buffer);
  free(t_mbuf->tokens);
  free(t_mbuf->vectors);
  return 0;
}
#endif /* #ifdef WITH_TCPDIRECT */
