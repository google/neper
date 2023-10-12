#ifndef THIRD_PARTY_NEPER_DEVMEM_H_
#define THIRD_PARTY_NEPER_DEVMEM_H_

#if __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "common.h"
#include "flags.h"
#include "lib.h"

struct tcpdevmem_udma_mbuf {
        struct msghdr msg;
        int dmabuf_fd;
        int pages_fd;

        int devfd;
        int memfd;
        int buf;
        int buf_pages;
};

struct tcpdevmem_cuda_mbuf {
        int gpu_mem_fd_;
        int dma_buf_fd_;
        void *gpu_gen_mem_;
        void *gpu_rx_mem_;
        void *gpu_scatter_list_;
        void *scattered_data_;
        void *rx_blks_;
        void *cpy_buffer;
        size_t bytes_received;
        size_t bytes_sent;
        void *tokens;
        void *vectors;
};

int tcpd_setup_socket(int socket);
int tcpd_cuda_setup_alloc(const struct options *opts, void **f_mbuf, struct thread *t);
int cuda_flow_cleanup(void *f_mbuf);
int udmabuf_setup_alloc(const struct options *opts, void **f_mbuf);
int tcpd_send(int socket, void *buf, size_t n, int flags);
int tcpd_udma_send(int fd, void *buf, size_t n, int flags);
int tcpd_recv(int fd, void *f_mbuf, size_t n, int flags, struct thread *t);

#if __cplusplus
}
#endif

#endif  // THIRD_PARTY_NEPER_DEVMEM_H_
