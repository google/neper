#include <linux/if.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include "flow.h"
#include "lib.h"
#include "logging.h"
#include "tcpdevmem_cuda.h"
#include "tcpdevmem.h"
#include "thread.h"

#define TEST_PREFIX "ncdevmem"
#define RETURN_IF_NON_ZERO(cmd)	\
	ret = (cmd);		\
	if (ret) return ret

int driver_reset(const struct options *opts) {
	char driver_reset_cmd[512];
	int ret = 0;

	sprintf(driver_reset_cmd, "ethtool --set-priv-flags %s enable-strict-header-split on", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool --set-priv-flags %s enable-strict-header-split off", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool --set-priv-flags %s enable-header-split off", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool --set-rxfh-indir %s equal 16", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool -K %s ntuple off", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool --set-priv-flags %s enable-strict-header-split off", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool --set-priv-flags %s enable-header-split off", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool -K %s ntuple off", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool --set-priv-flags %s enable-max-rx-buffer-size on", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	sprintf(driver_reset_cmd, "ethtool -K %s ntuple on", opts->tcpd_link_name);
	RETURN_IF_NON_ZERO(system(driver_reset_cmd));

	return ret;
}

int install_flow_steering(const struct options *opts, intptr_t buf,
			  struct thread *t)
{
	int q_start = opts->queue_start;
	int q_num = opts->queue_num;
	int ret;

	int num_queues = q_start + (t->index % q_num);
	LOG_INFO(t->cb, "Bind to queue %i\n", num_queues);
	struct dma_buf_pages_bind_rx_queue bind_cmd;

	strcpy(bind_cmd.ifname, opts->tcpd_link_name);
	bind_cmd.rxq_idx = num_queues;

	ret = ioctl(buf, DMA_BUF_PAGES_BIND_RX, &bind_cmd);
	if (ret < 0)
		LOG_FATAL(t->cb, "FAIL, bind fail queue=%d", num_queues);

	/* using t->index below requires 1 thread listening to 1 port
	 * (see relevant comments in socket.c)
	 */
	int src_port = t->index + opts->source_port;
	int dst_port = t->index + atoi(opts->port);

	char flow_steer_cmd[512];
	sprintf(flow_steer_cmd,
		"ethtool -N %s flow-type tcp4 src-ip %s dst-ip %s src-port %i dst-port %i queue %i",
		opts->tcpd_link_name, opts->tcpd_src_ip, opts->tcpd_dst_ip,
		src_port, dst_port, num_queues);
	RETURN_IF_NON_ZERO(system(flow_steer_cmd));

	// only running the below ethtool commands after last thread/flow is setup
	if (t->index == opts->num_flows - 1)
	{
		char ethtool_cmd[512];
		sprintf(ethtool_cmd, "ethtool --set-priv-flags %s enable-strict-header-split on", opts->tcpd_link_name);
		RETURN_IF_NON_ZERO(system(ethtool_cmd));

		sprintf(ethtool_cmd, "ethtool --set-priv-flags %s enable-header-split on", opts->tcpd_link_name);
		RETURN_IF_NON_ZERO(system(ethtool_cmd));

		sprintf(ethtool_cmd, "ethtool --set-rxfh-indir %s equal 8", opts->tcpd_link_name);
		RETURN_IF_NON_ZERO(system(ethtool_cmd));

		LOG_INFO(t->cb, "ethtool cmds returned %i, sleeping 1...\n", ret);
		sleep(1);
	}
	return ret;
}

int tcpd_setup_socket(struct thread *t, int socket)
{
	const int one = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
	    setsockopt(socket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) ||
	    setsockopt(socket, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one)))
		PLOG_FATAL(t->cb, "tcpd_setup_socket");

	return 0;
}
