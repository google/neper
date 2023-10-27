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

#if __cplusplus
extern "C" {
#endif

#include "common.h"
#include "tcpdevmem_cuda.h"
#include "tcpdevmem.h"
#include "logging.h"
#include "flow.h"
#include "thread.h"

#if __cplusplus
}
#endif

#define LAST_PRIME 111

#define MIN_RX_BUFFER_TOTAL_SIZE (1 << 28)
#define GPUMEM_ALIGNMENT (1UL << 21)
#define GPUMEM_MINSZ 0x400000

#define multiplier (1 << 16)

#define TEST_PREFIX "ncdevmem"
#define NUM_PAGES 16000

struct TcpdRxBlock {
  uint64_t gpu_offset;
  size_t size;
  uint64_t paddr;
};

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

__global__ void scatter_copy_kernel(long3* scatter_list, uint8_t* dst,
                                    uint8_t* src) {
  int block_idx = blockIdx.x;
  long3 blk = scatter_list[block_idx];
  long dst_off = blk.x;
  long src_off = blk.y;
  long sz = blk.z;

  int thread_sz = sz / blockDim.x;
  int rem = sz % blockDim.x;
  bool extra = (threadIdx.x < rem);
  int thread_offset = sz / blockDim.x * threadIdx.x;
  thread_offset += (extra) ? threadIdx.x : rem;

  for (int i = 0; i < thread_sz; i++) {
    dst[dst_off + thread_offset + i] = src[src_off + thread_offset + i];
  }
  if (extra) {
    dst[dst_off + thread_offset + thread_sz] =
        src[src_off + thread_offset + thread_sz];
  }
}

void gather_rx_data(struct tcpdevmem_cuda_mbuf *tmbuf) {
  int ret;
  void *gpu_scatter_list_ = tmbuf->gpu_scatter_list_;
  std::vector<long3> *scattered_data_ = (std::vector<long3> *)tmbuf->scattered_data_;
  void *gpu_rx_mem_ = tmbuf->gpu_rx_mem_;
  void *rx_buff_ = tmbuf->gpu_gen_mem_;

  ret = cudaMemcpyAsync(gpu_scatter_list_,
                        scattered_data_->data(),
                        scattered_data_->size() * sizeof(long3),
                        cudaMemcpyHostToDevice);
  if (ret)
    return;

  scatter_copy_kernel<<<scattered_data_->size(), 256, 0>>>(
      (long3*)gpu_scatter_list_, (uint8_t*)gpu_rx_mem_, (uint8_t*)rx_buff_);
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

int tcpd_cuda_setup_alloc(const struct options *opts, void **f_mbuf, struct thread *t)
{
  bool is_client = opts->client;
  int ret;
  void *gpu_gen_mem_;
  int gpu_mem_fd_;
  int dma_buf_fd_;
  // int q_start = opts->queue_start;
  // int q_num = opts->queue_num;
  struct tcpdevmem_cuda_mbuf *tmbuf;
  const char *gpu_pci_addr = opts->tcpd_gpu_pci_addr;
  const char *nic_pci_addr = opts->tcpd_nic_pci_addr;
  size_t alloc_size = opts->tcpd_phys_len;

  tmbuf =
    (struct tcpdevmem_cuda_mbuf *)calloc(1, sizeof(struct tcpdevmem_cuda_mbuf));
  if (!tmbuf) {
    exit(EXIT_FAILURE);
  }

  if (alloc_size % GPUMEM_ALIGNMENT != 0) {
    alloc_size += GPUMEM_ALIGNMENT - (alloc_size % GPUMEM_ALIGNMENT);
  }

  cudaMalloc(&gpu_gen_mem_, alloc_size);
  if (is_client && opts->tcpd_validate) {
          fill_tx_buffer(gpu_gen_mem_, alloc_size);
          cudaDeviceSynchronize();
  }
  unsigned int flag = 1;
  cuPointerSetAttribute(&flag,
                        CU_POINTER_ATTRIBUTE_SYNC_MEMOPS,
                        (CUdeviceptr)gpu_gen_mem_);

  gpu_mem_fd_ = get_gpumem_dmabuf_pages_fd(gpu_pci_addr, nic_pci_addr,
                                           gpu_gen_mem_, alloc_size,
                                           &dma_buf_fd_, is_client);

  if (gpu_mem_fd_ < 0) {
    printf("get_gpumem_dmabuf_pages_fd() failed!: ");
    exit(71);
  }

  if (!is_client)
    install_flow_steering(opts, gpu_mem_fd_, t);

  *f_mbuf = tmbuf;
  tmbuf->gpu_mem_fd_ = gpu_mem_fd_;
  tmbuf->dma_buf_fd_ = dma_buf_fd_;
  tmbuf->gpu_gen_mem_ = gpu_gen_mem_;
  tmbuf->cpy_buffer = malloc(opts->buffer_size);
  tmbuf->vectors = new std::vector<devmemvec>();
  tmbuf->tokens = new std::vector<devmemtoken>();
  tmbuf->bytes_received = 0;
  tmbuf->bytes_sent = 0;

  cudaMalloc(&tmbuf->gpu_rx_mem_, opts->buffer_size);
  cudaMalloc(&tmbuf->gpu_scatter_list_, opts->buffer_size);
  tmbuf->rx_blks_ = new std::vector<TcpdRxBlock>();
  tmbuf->scattered_data_ = new std::vector<long3>();
  return 0;
}

int tcpd_send(int socket, void *buf, size_t n, int flags) {
  int gpu_mem_fd_;
  struct iovec iov;
  struct msghdr msg;
  struct cmsghdr *cmsg;
  char offsetbuf[CMSG_SPACE(sizeof(uint32_t) * 2)];
  struct tcpdevmem_cuda_mbuf *tmbuf;

  if (!buf) return -1;

  tmbuf = (struct tcpdevmem_cuda_mbuf *)buf;
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

int tcpd_recv(int socket, void *f_mbuf, size_t n, int flags, struct thread *t) {
  struct iovec iov;
  struct msghdr msg_local;
  struct msghdr *msg;
  struct tcpdevmem_cuda_mbuf *tmbuf;
  int ret, client_fd;
  int buffer_size = n;
  size_t total_received = 0;
  unsigned char *cpy_buffer;
  const struct options *opts = t->opts;
  std::vector<devmemvec> *vectors;
  std::vector<devmemtoken> *tokens;
  std::vector<TcpdRxBlock> *rx_blks_;
  std::vector<long3> *scattered_data_;

  if (!f_mbuf) return -1;

  tmbuf = (struct tcpdevmem_cuda_mbuf *)f_mbuf;
  cpy_buffer = (unsigned char *)tmbuf->cpy_buffer;
  vectors = (std::vector<devmemvec> *)tmbuf->vectors;
  tokens = (std::vector<devmemtoken> *)tmbuf->tokens;
  rx_blks_ = (std::vector<TcpdRxBlock> *)tmbuf->rx_blks_;
  scattered_data_ = (std::vector<long3> *)tmbuf->scattered_data_;

  client_fd = socket;

  char buf_dummy[n];
  char offsetbuf[CMSG_SPACE(sizeof(int) * 1000)];
  msg = &msg_local;

  memset(msg, 0, sizeof(struct msghdr));

  iov.iov_base = buf_dummy;
  iov.iov_len = n - tmbuf->bytes_received;
  msg->msg_iov = &iov;
  msg->msg_iovlen = 1;

  msg->msg_control = offsetbuf;
  msg->msg_controllen = sizeof(offsetbuf);

  rx_blks_->clear();

  ssize_t received = recvmsg(socket, msg, MSG_SOCK_DEVMEM | MSG_DONTWAIT);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    printf("%s %d: recvmsg returned < 0\n", __func__, __LINE__);
    return -1;
  } else if (received < 0) {
    printf("%s %d\n", __func__, __LINE__);
    return -1;
  } else if (received == 0) {
    printf("Client exited\n");
    return -1;
  }

  if (msg->msg_flags & MSG_CTRUNC) {
    LOG_ERROR(t->cb, "fatal, cmsg truncated, current msg_controllen");
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
    struct TcpdRxBlock blk;

    if (devmemvec->frag_size > PAGE_SIZE)
      continue;

    blk.gpu_offset = (uint64_t)devmemvec->frag_offset;
    blk.size = devmemvec->frag_size;
    rx_blks_->emplace_back(blk);

    total_received += devmemvec->frag_size;

    vectors->emplace_back(*devmemvec);
    tokens->push_back(token);
  }

  size_t dst_offset = tmbuf->bytes_received;
  for (int i = 0; i < rx_blks_->size(); i++) {
    struct TcpdRxBlock blk = rx_blks_->at(i);
    size_t off = (size_t)blk.gpu_offset;
    scattered_data_->emplace_back(
        make_long3((long)dst_offset, (long)off, (long)blk.size));

    dst_offset += blk.size;
  }
  tmbuf->bytes_received += received;

  /* Once we've received fragments totaling buffer_size, we can copy from the
   * CUDA buffer to a user-space buffer, and free the fragments in the CUDA
   * buffer.
  */
  if (tmbuf->bytes_received == buffer_size) {
    if (opts->tcpd_rx_cpy) {
      gather_rx_data(tmbuf);
      cudaDeviceSynchronize();
    }
    /* There is a performance impact when we cudaMemcpy from the CUDA buffer to
     * the userspace buffer, so it's gated by a flag
     */
    if (opts->tcpd_validate) {
      for (int idx = 0; idx < vectors->size(); idx++) {
        struct devmemvec vec = (*vectors)[idx];
        struct devmemtoken token = (*tokens)[idx];

        /* copy each fragment to the cpy_buffer in order, i.e.
         * 1st fragment will occuply bytes [0-4095], 2nd fragment will
         * occupy bytes [4096-8191], etc.
         */
        cudaMemcpy(cpy_buffer + (vec.frag_token - 1) * PAGE_SIZE,
                   (char *)tmbuf->gpu_gen_mem_ + vec.frag_offset,
                   vec.frag_size,
                   cudaMemcpyDeviceToHost);
      }

      /* Ensure the sequence is what we expect:
       * a repeating sequence of 1 to LAST_PRIME inclusive
       */
      cudaDeviceSynchronize();
      int i = 0;
      int expected_val;
      while (i < buffer_size) {
        expected_val = (i % LAST_PRIME) + 1;
        if (cpy_buffer[i] != expected_val) {
          LOG_WARN(t->cb,
                   "Thread %i - incorrect byte %i, expected %i, got %i",
                   t->index,
                   i,
                   expected_val,
                   cpy_buffer[i]);
          break;
        }
        i++;
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
    rx_blks_->clear();
    scattered_data_->clear();
    tmbuf->bytes_received = 0;
  }
  return total_received;
}

int cuda_flow_cleanup(void *f_mbuf) {
  struct tcpdevmem_cuda_mbuf *t_mbuf = (struct tcpdevmem_cuda_mbuf *)f_mbuf;
  close(t_mbuf->gpu_mem_fd_);
  close(t_mbuf->dma_buf_fd_);
  cudaFree(t_mbuf->gpu_gen_mem_);
  free(t_mbuf->cpy_buffer);
  free(t_mbuf->tokens);
  free(t_mbuf->vectors);

  cudaFree(t_mbuf->gpu_rx_mem_);
  cudaFree(t_mbuf->gpu_scatter_list_);
  free(t_mbuf->rx_blks_);
  free(t_mbuf->scattered_data_);
  return 0;
}
