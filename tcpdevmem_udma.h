#ifndef THIRD_PARTY_NEPER_DEVMEM_UDMA_H_
#define THIRD_PARTY_NEPER_DEVMEM_UDMA_H_

#if __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "common.h"
#include "flags.h"
#include "lib.h"

#define UDMABUF_CREATE _IOW('u', 0x42, struct udmabuf_create)

struct tcpdevmem_udma_mbuf {
        struct msghdr msg;
        int dmabuf_fd;
        int pages_fd;

        int devfd;
        int memfd;
        int buf;
        int buf_pages;
        size_t bytes_sent;
};

int udma_setup_alloc(const struct options *opts, void **f_mbuf,
                     struct thread *t);
int udma_send(int socket, void *f_mbuf, size_t n, int flags);
int udma_recv(int socket, void *f_mbuf, size_t n, struct thread *t);

#if __cplusplus
}
#endif

#endif  // THIRD_PARTY_NEPER_DEVMEM_UDMA_H_
