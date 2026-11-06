#include <net/icmp.h>
#include <linux/inetdevice.h>
#include <user/fs.h>
#include <user/user.h>
#include <linux/err.h>
#include <net/checksum.h>
#include "golp.h"

int golp_ping_up(struct net_device *dev)
{
	struct golp_priv *priv = netdev_priv(dev);
	int fd;

	priv->ping_emulated = false;
	priv->ping_sock = golp_socket_alloc(dev, IPPROTO_ICMP);
	if (priv->ping_sock == NULL)
		return -ENOMEM;
	fd = host_socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	priv->ping_sock->fd = fd;
	/* Darwin/macOS/iOS have no unprivileged ICMP datagram sockets. Rather
	 * than fail the whole interface bring-up, synthesize echo replies. */
	if (fd < 0 || host_getenv("GOLP_EMULATE_PING") != NULL) {
		pr_warn("golp: no host ICMP socket (%pe); synthesizing echo replies\n",
			fd < 0 ? ERR_PTR(fd) : ERR_PTR(-EOPNOTSUPP));
		priv->ping_emulated = true;
		return 0;
	}
	return fd_listen(fd, POLLIN, &priv->ping_sock->listener);
}

int golp_ping_down(struct net_device *dev)
{
	struct golp_priv *priv = netdev_priv(dev);
	if (priv->ping_sock != NULL) {
		golp_socket_put(priv->ping_sock);
		priv->ping_sock = NULL;
	}
	return 0;
}

void golp_ping4_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct golp_priv *priv = netdev_priv(dev);
	struct iphdr *ip = ip_hdr(skb);
	struct icmphdr *icmp = icmp_hdr(skb);
	int err;
	struct sockaddr_in addr = {
		.sin_family = PF_INET,
		.sin_addr.s_addr = ip->daddr,
		.sin_port = 0,
	};
	struct user_iovec data = {
		.base = icmp,
		.len = ip_transport_len(skb),
	};

	if (ip_transport_len(skb) < sizeof(*icmp))
		return golp_drop_tx(dev, skb, "icmp packet too short");
	if (icmp->type != ICMP_ECHO)
		return golp_drop_tx(dev, skb, "wrong icmp type");

	if (priv->ping_emulated || priv->ping_sock == NULL ||
	    priv->ping_sock->fd < 0) {
		/* Synthesize an echo reply from the destination. */
		__be32 s = ip->saddr;
		ip->saddr = ip->daddr;
		ip->daddr = s;
		icmp->type = ICMP_ECHOREPLY;
		icmp->checksum = 0;
		icmp->checksum = csum_fold(csum_partial(icmp, ip_transport_len(skb), 0));
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += ip_transport_len(skb);
		golp_rx(skb);
		return;
	}

	err = host_sendmsg(priv->ping_sock->fd, &data, 1, &addr, sizeof(addr), 0);
	if (err < 0)
		return golp_drop_tx(dev, skb, "icmp sendmsg failed: %pe", ERR_PTR(err));
	skb->dev->stats.tx_packets++;
	skb->dev->stats.tx_bytes += data.len;
}

void golp_ping4_rx(struct golp_socket *sock, struct net_device *dev)
{
	struct sockaddr_storage addr;
	struct user_iovec iov = {
		.base = __golp_irq_buf,
		.len = sizeof(__golp_irq_buf),
	};
	
	for (;;) {
		struct sk_buff *skb;
		int addr_len;
		int rcv_flags;
		ssize_t rcv_len;
		struct iphdr *ip;

		rcv_len = host_recvmsg(sock->fd, &iov, 1, &addr, &addr_len, &rcv_flags, MSG_DONTWAIT);
		if (rcv_len == -EAGAIN)
			break;
		if (rcv_len < 0) {
			golp_drop_rx(dev, "ping read failed: %pe", ERR_PTR(rcv_len));
			continue;
		}

		/* SOCK_DGRAM ICMP delivers the ICMP message without the IP
		 * header; build one so the guest (raw ICMP sockets include it)
		 * accepts the reply. */
		skb = netdev_alloc_skb(dev, sizeof(struct iphdr) + rcv_len);
		if (skb == NULL) {
			golp_drop_rx(dev, "no memory for skb");
			continue;
		}
		ip = skb_put(skb, sizeof(struct iphdr));
		memset(ip, 0, sizeof(*ip));
		ip->version = IPVERSION;
		ip->ihl = 5;
		ip->protocol = IPPROTO_ICMP;
		ip->saddr = ((struct sockaddr_in *) &addr)->sin_addr.s_addr;
		ip->daddr = inet_select_addr(dev, ip->saddr, RT_SCOPE_HOST);
		memcpy(skb_put(skb, rcv_len), __golp_irq_buf, rcv_len);
		skb_reset_network_header(skb);
		ip = ip_hdr(skb);

		golp_rx(skb);
	}
}
