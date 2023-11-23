#define __iovec_defined 1

#include <linux/memfd.h>
#include <linux/if.h>
#include <linux/dma-buf.h>
#include <linux/socket.h>
#include <linux/udmabuf.h>
#include <linux/uio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <stddef.h>
#include <fcntl.h>

#include "flow.h"
#include "lib.h"
#include "logging.h"
#include "tcpdevmem.h"
#include "tcpdevmem_udma.h"
#include "thread.h"

#define TEST_PREFIX "ncdevmem_udma"

#ifndef MSG_SOCK_DEVMEM
#define MSG_SOCK_DEVMEM 0x2000000	/* don't copy devmem pages but return
					 * them as cmsg instead */
#endif

int udma_setup_alloc(const struct options *opts, void **f_mbuf, struct thread *t)
{
        bool is_client = opts->client;
        int devfd;
        int memfd;
        int buf;
        int buf_pages;
        int ret;
        size_t size = opts->tcpd_phys_len;

        struct tcpdevmem_udma_mbuf *tmbuf;
        struct dma_buf_create_pages_info pages_create_info;
        struct udmabuf_create create;

        if (f_mbuf == NULL)
                return ENOMEM;

        if (*f_mbuf)
                return 0;

        tmbuf = (struct tcpdevmem_udma_mbuf *)calloc(1, sizeof(struct tcpdevmem_udma_mbuf));
        if (!tmbuf)
        {
                exit(EXIT_FAILURE);
        }

        devfd = open("/dev/udmabuf", O_RDWR);
        if (devfd < 0)
        {
                printf("%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
                       TEST_PREFIX);
                exit(70);
        }

        memfd = memfd_create("udmabuf-test", MFD_ALLOW_SEALING);
        if (memfd < 0)
        {
                printf("%s: [skip,no-memfd]\n", TEST_PREFIX);
                exit(72);
        }

        ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
        if (ret < 0)
        {
                printf("%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);
                exit(73);
        }

        ret = ftruncate(memfd, size);
        if (ret == -1)
        {
                printf("%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);
                exit(74);
        }

        memset(&create, 0, sizeof(create));

        create.memfd = memfd;
        create.offset = 0;
        create.size = size;
        printf("size=%lu\n", size);
        buf = ioctl(devfd, UDMABUF_CREATE, &create);
        if (buf < 0)
        {
                printf("%s: [FAIL, create udmabuf] %i\n", TEST_PREFIX, buf);
                exit(75);
        }

        pages_create_info.dma_buf_fd = buf;
        pages_create_info.create_page_pool = is_client ? 0 : 1;

        ret = sscanf(opts->tcpd_nic_pci_addr, "0000:%llx:%llx.%llx",
                     &pages_create_info.pci_bdf[0],
                     &pages_create_info.pci_bdf[1],
                     &pages_create_info.pci_bdf[2]);

        if (ret != 3)
        {
                printf("%s: [FAIL, parse fail]\n", TEST_PREFIX);
                exit(76);
        }

        buf_pages = ioctl(buf, DMA_BUF_CREATE_PAGES, &pages_create_info);
        if (buf_pages < 0)
        {
                perror("ioctl DMA_BUF_CREATE_PAGES: [FAIL, create pages fail]\n");
                exit(77);
        }

        if (!is_client)
                install_flow_steering(opts, buf_pages, t);

        struct dma_buf_sync sync = {0};
        sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_START;
        ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);

        *f_mbuf = tmbuf;

        tmbuf->devfd = devfd;
        tmbuf->memfd = memfd;
        tmbuf->buf = buf;
        tmbuf->buf_pages = buf_pages;
        tmbuf->bytes_sent = 0;
        return 0;
}

int udma_send(int socket, void *f_mbuf, size_t n, int flags)
{
        int buf_pages, buf;
        struct iovec iov;
        struct msghdr *msg;
        struct cmsghdr *cmsg;
        char buf_dummy[n];
        char offsetbuf[CMSG_SPACE(sizeof(uint32_t) * 2)];
        struct tcpdevmem_udma_mbuf *tmbuf;

        if (!f_mbuf)
                return -1;

        tmbuf = (struct tcpdevmem_udma_mbuf *)f_mbuf;
        buf_pages = tmbuf->buf_pages;
        buf = tmbuf->buf;
        msg = &tmbuf->msg;

        struct dma_buf_sync sync = {0};
        sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_START;
        ioctl(buf, DMA_BUF_IOCTL_SYNC, &sync);

        char *buf_mem = NULL;
        buf_mem = (char *)mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, buf, 0);
        if (buf_mem == MAP_FAILED)
        {
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
        iov.iov_len = n - tmbuf->bytes_sent;

        msg->msg_iov = &iov;
        msg->msg_iovlen = 1;

        msg->msg_control = offsetbuf;
        msg->msg_controllen = sizeof(offsetbuf);

        cmsg = CMSG_FIRSTHDR(msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_DEVMEM_OFFSET;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 2);
        *((int *)CMSG_DATA(cmsg)) = buf_pages;
        ((int *)CMSG_DATA(cmsg))[1] = (int)tmbuf->bytes_sent;

        ssize_t bytes_sent = sendmsg(socket, msg, MSG_ZEROCOPY);
        if (bytes_sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
        {
                perror("sendmsg() error: ");
                exit(EXIT_FAILURE);
        }

        if (bytes_sent == 0)
        {
                perror("sendmsg() sent 0 bytes. Something is wrong.\n");
                exit(EXIT_FAILURE);
        }

        tmbuf->bytes_sent += bytes_sent;
        if (tmbuf->bytes_sent == n)
                tmbuf->bytes_sent = 0;

        return bytes_sent;
}

int udma_recv(int socket, void *f_mbuf, size_t n, struct thread *t)
{
        struct tcpdevmem_udma_mbuf *tmbuf = (struct tcpdevmem_udma_mbuf *)f_mbuf;
        bool is_devmem = false;
        size_t total_received = 0;
        size_t page_aligned_frags = 0;
        size_t non_page_aligned_frags = 0;
        unsigned long flow_steering_flakes = 0;

        char iobuf[819200];
        char ctrl_data[sizeof(int) * 20000];

        struct msghdr msg = {0};
        struct iovec iov = {.iov_base = iobuf,
                            .iov_len = sizeof(iobuf)};

        if (!f_mbuf)
                return -1;

        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl_data;
        msg.msg_controllen = sizeof(ctrl_data);
        ssize_t ret = recvmsg(socket, &msg, MSG_SOCK_DEVMEM);
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
                return -1;
        }
        if (ret < 0)
        {
                PLOG_FATAL(t->cb, "recvmsg:");
                exit(1);
        }
        if (ret == 0)
        {
                LOG_ERROR(t->cb, "client exited");
                return -1;
        }

        struct cmsghdr *cm = NULL;
        struct devmemvec *devmemvec = NULL;
        for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm))
        {
                if (cm->cmsg_level != SOL_SOCKET ||
                    (cm->cmsg_type != SCM_DEVMEM_OFFSET &&
                     cm->cmsg_type != SCM_DEVMEM_HEADER))
                {
                        LOG_ERROR(t->cb, "found weird cmsg");
                        continue;
                }
                is_devmem = true;

                devmemvec = (struct devmemvec *)CMSG_DATA(cm);

                if (cm->cmsg_type == SCM_DEVMEM_HEADER)
                {
                        // TODO: process data copied from skb's linear
                        // buffer.
                        LOG_FATAL(t->cb,
                                  "SCM_DEVMEM_HEADER. devmemvec->frag_size=%u",
                                  devmemvec->frag_size);
                        exit(1);
                        continue;
                }

                struct devmemtoken token = {devmemvec->frag_token, 1};

                total_received += devmemvec->frag_size;

                if (devmemvec->frag_size % PAGE_SIZE)
                        non_page_aligned_frags++;
                else
                        page_aligned_frags++;

                struct dma_buf_sync sync = {0};
                sync.flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_START;
                ioctl(tmbuf->buf, DMA_BUF_IOCTL_SYNC, &sync);

                sync.flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_END;
                ioctl(tmbuf->buf, DMA_BUF_IOCTL_SYNC, &sync);

                ret = setsockopt(socket, SOL_SOCKET,
                                 SO_DEVMEM_DONTNEED, &token,
                                 sizeof(token));
                if (ret)
                {
                        PLOG_FATAL(t->cb, "DONTNEED failed");
                        exit(1);
                }
        }

        if (!is_devmem)
        {
                flow_steering_flakes++;
                is_devmem = false;
                total_received += ret;
        }
        if (flow_steering_flakes) {
                LOG_WARN(t->cb, "total_received=%lu flow_steering_flakes=%lu",
                         total_received, flow_steering_flakes);
        }

        return total_received;
}
