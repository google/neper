#include "thread.h"
#include "lib.h"

#define PAGE_SHIFT (12)
#define PAGE_SIZE (1 << PAGE_SHIFT)

#ifndef MSG_SOCK_DEVMEM
#define MSG_SOCK_DEVMEM 0x2000000	/* don't copy devmem pages but return
					 * them as cmsg instead */
#endif

int install_flow_steering(const struct options *opts, intptr_t buf,
			  struct thread *t);
int tcpd_setup_socket(int socket);
