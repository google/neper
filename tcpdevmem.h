#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/if.h>
#include <sys/ioctl.h>

#include "thread.h"
#include "lib.h"

#define PAGE_SHIFT (12)
#define PAGE_SIZE (1 << PAGE_SHIFT)

#define MSG_SOCK_DEVMEM 0x2000000
#define SO_DEVMEM_DONTNEED 97
#define SO_DEVMEM_HEADER 98
#define SCM_DEVMEM_HEADER SO_DEVMEM_HEADER
#define SO_DEVMEM_OFFSET 99
#define SCM_DEVMEM_OFFSET SO_DEVMEM_OFFSET

#define DMA_BUF_CREATE_PAGES \
	_IOW(DMA_BUF_BASE, 2, struct dma_buf_create_pages_info)

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

struct dma_buf_create_pages_info
{
	__u64 pci_bdf[3];
	__s32 dma_buf_fd;
	__s32 create_page_pool;
};

struct dma_buf_pages_bind_rx_queue
{
	char ifname[IFNAMSIZ];
	__u32 rxq_idx;
};

#define DMA_BUF_PAGES_BIND_RX \
	_IOW(DMA_BUF_BASE, 3, struct dma_buf_pages_bind_rx_queue)

// devmemvec represents a fragment of payload that is received on the socket.
struct devmemvec
{
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
struct devmemtoken
{
	__u32 token_start;
	__u32 token_count;
};

int install_flow_steering(const struct options *opts, intptr_t buf,
			  struct thread *t);
int tcpd_setup_socket(int socket);
