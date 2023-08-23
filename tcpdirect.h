#ifndef THIRD_PARTY_NEPER_TCPDIRECT_H_
#define THIRD_PARTY_NEPER_TCPDIRECT_H_

#if __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "common.h"
#include "flags.h"
#include "lib.h"

int tcpdirect_setup_socket(int socket);
int tcpdirect_cuda_setup_alloc(const struct options *opts, void **f_mbuf, struct thread *t);
int cuda_flow_cleanup(void *f_mbuf);
int udmabuf_setup_alloc(const struct options *opts, void **f_mbuf);
int tcpdirect_send(int socket, void *buf, size_t n, int flags);
int tcpdirect_udma_send(int fd, void *buf, size_t n, int flags);
int tcpdirect_recv(int fd, void *f_mbuf, size_t n, int flags);

#if __cplusplus
}
#endif

#endif  // THIRD_PARTY_NEPER_TCPDIRECT_H_
