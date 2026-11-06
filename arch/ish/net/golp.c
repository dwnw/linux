#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <user/fs.h>
#include "golp.h"

static int golp_dev_init(struct net_device *dev)
{
	struct golp_priv *priv = netdev_priv(dev);
	int err;

	hash_init(priv->tcp_hash);
	hash_init(priv->udp_hash);
	spin_lock_init(&priv->sockets_lock);
	err = host_pipe(&priv->irq_pipe_read, &priv->irq_pipe_write);
	if (err < 0) return err;
	err = fd_set_nonblock(priv->irq_pipe_read);
	if (err < 0) return err;
	return 0;
}

static void golp_dev_uninit(struct net_device *dev)
{
	panic("ha ha ha! I physically cannot die!");
}

static int golp_up(struct net_device *dev)
{
	int err;

	/* TODO @ipv6: find equivalent, or set dst on outgoing skbs */
	IN_DEV_CONF_SET(__in_dev_get_rtnl(dev), ROUTE_LOCALNET, 1);

	err = golp_ping_up(dev);
	if (err < 0)
		pr_warn("golp: ping setup failed (%pe); continuing without ICMP\n",
			ERR_PTR(err));
	return 0;
}

static int golp_down(struct net_device *dev)
{
	struct golp_priv *priv = netdev_priv(dev);
	int bucket;
	struct hlist_node *tmp;
	struct golp_socket *socket;
	int err;

	hash_for_each_safe(priv->udp_hash, bucket, tmp, socket, link) {
		hash_del(&socket->link);
		golp_socket_put(socket);
	}
	hash_for_each_safe(priv->tcp_hash, bucket, tmp, socket, link) {
		hash_del(&socket->link);
		golp_socket_put(socket);
	}

	err = golp_ping_down(dev);
	if (err < 0) return err;
	return 0;
}

static netdev_tx_t golp_tx(struct sk_buff *skb, struct net_device *dev)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		switch (ip_hdr(skb)->protocol) {
		case IPPROTO_ICMP:
			golp_ping4_tx(skb, dev);
			break;
		case IPPROTO_UDP:
			golp_udp4_tx(skb, dev);
			break;
		case IPPROTO_TCP:
			golp_tcp4_tx(skb, dev);
			break;
		default:
			golp_drop_tx(dev, skb, "unrecognized IP protocol %d",
				     ip_hdr(skb)->protocol);
			break;
		}
		break;
	default:
		golp_drop_tx(dev, skb, "unrecognized protocol %d",
			     skb->protocol);
		break;
	}
	return NETDEV_TX_OK;
}

/* Mitigation for the classic multithreaded epoll UaF in which:
 * Thread 1 wakes up from epoll, grabs a data pointer
 * Thread 2 removes the corresponding FD from the epoll and frees the data
 * Thread 1 uses the data pointer
 * The fix is to delay the freeing until epoll processing is done. Here,
 * that is at the end of the IRQ handler.
 */
static LIST_HEAD(sock_freelist);
static DEFINE_SPINLOCK(sock_freelist_lock);

char __golp_irq_buf[65536];
static irqreturn_t golp_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct golp_priv *priv = netdev_priv(dev);
	struct golp_socket *sock, *n;

	for (;;) {
		int err = host_read(priv->irq_pipe_read, &sock,
				    sizeof(struct golp_socket *));
		if (err <= 0)
			break;

		switch (sock->protocol) {
		case IPPROTO_ICMP:
			golp_ping4_rx(sock, dev);
			break;
		case IPPROTO_UDP:
			golp_udp4_rx(sock, dev);
			break;
		case IPPROTO_TCP:
			golp_tcp4_rx(sock, dev);
			break;
		}
	}

	spin_lock(&sock_freelist_lock);
	list_for_each_entry_safe(sock, n, &sock_freelist, freelink) {
		list_del(&sock->freelink);
		kfree(sock);
	}
	spin_unlock(&sock_freelist_lock);
	return IRQ_HANDLED;
}

void golp_rx(struct sk_buff *skb)
{
	struct iphdr *ip;

	skb->protocol = htons(ETH_P_IP);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_reset_network_header(skb);

	ip = ip_hdr(skb);
	ip->version = IPVERSION;
	ip->ihl = 5;
	ip->tos = 0;
	ip->tot_len = htons(skb->len);
	ip->id = 0;
	ip->frag_off = 0;
	ip->ttl = 64;
	ip->check = 0;
	ip->check = ip_fast_csum(ip, ip->ihl);

	/* TODO atomicity? */
	skb->dev->stats.rx_packets++;
	skb->dev->stats.rx_bytes += skb->len;
	netif_rx(skb);
}

struct golp_socket *golp_socket_alloc(struct net_device *dev, int proto)
{
	struct golp_priv *priv = netdev_priv(dev);
	struct golp_socket *sock = kmalloc(sizeof(*sock), GFP_KERNEL);
	if (sock == NULL)
		return NULL;
	refcount_set(&sock->refcount, 1);
	spin_lock_init(&sock->lock);
	sock->protocol = proto;
	sock->listener.irq = GOLP_IRQ;
	sock->listener.pipe = priv->irq_pipe_write;
	sock->listener.data = (void *) sock;
	return sock;
}

void __golp_socket_destroy(struct golp_socket *sock)
{
	int fd;
	unsigned long flags;

	/* Dance to ensure an irq will see either -1 or a valid fd */
	fd = sock->fd;
	WRITE_ONCE(sock->fd, -1);
	smp_wmb();
	host_close(fd);
	
	spin_lock_irqsave(&sock_freelist_lock, flags);
	list_add(&sock->freelink, &sock_freelist);
	spin_unlock_irqrestore(&sock_freelist_lock, flags);
}

static int golp_ip_listen(struct net_device *dev, int protocol, __be32 addr,
			  unsigned short port)
{
	if (protocol == IPPROTO_UDP)
		return golp_udp4_ip_listen(dev, addr, port);
	return 0;
}

static void golp_ip_unlisten(struct net_device *dev, int protocol, __be32 addr,
			     unsigned short port)
{
	if (protocol == IPPROTO_UDP)
		return golp_udp4_ip_unlisten(dev, addr, port);
}

static void golp_ip_listen_newaddr(struct net_device *dev, int protocol,
				   __be32 old_addr, __be32 new_addr,
				   unsigned short port)
{
	if (protocol == IPPROTO_UDP)
		return golp_udp4_ip_listen_newaddr(dev, old_addr, new_addr, port);
}

static const struct net_device_ops golp_ops = {
	.ndo_init = golp_dev_init,
	.ndo_uninit = golp_dev_uninit,
	.ndo_open = golp_up,
	.ndo_stop = golp_down,
	.ndo_start_xmit = golp_tx,
	.ndo_ip_listen = golp_ip_listen,
	.ndo_ip_listen_newaddr = golp_ip_listen_newaddr,
	.ndo_ip_unlisten = golp_ip_unlisten,
};

static DEFINE_RATELIMIT_STATE(drop_log_ratelimit, 5 * HZ, 10);

void __golp_drop_tx(struct net_device *dev, struct sk_buff *skb,
		    const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	if (__ratelimit(&drop_log_ratelimit))
		vprintk(msg, args);
	va_end(args);
	dev->stats.tx_dropped++;
}

void __golp_drop_rx(struct net_device *dev, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	if (__ratelimit(&drop_log_ratelimit))
		vprintk(msg, args);
	va_end(args);
	dev->stats.rx_dropped++;
}

static void golp_setup(struct net_device *dev) {
	dev->netdev_ops = &golp_ops;
	dev->features = NETIF_F_HW_CSUM;
	/* dev->mtu = 1500; */
	dev->mtu = IP_MAX_MTU; /* why limit ourselves? */
}

static int golp_socket_hash_read(struct seq_file *seq, void *data)
{
	struct net_device *dev =
		container_of(seq->private, struct net_device, dev);
	struct golp_priv *priv = netdev_priv(dev);
	int bkt;
	struct golp_socket *sock;
	hash_for_each(priv->tcp_hash, bkt, sock, link) {
		seq_printf(seq, "tcp %pI4:%d refcnt=%d fd=%d\n", &sock->addr,
			   sock->port, refcount_read(&sock->refcount),
			   sock->fd);
	}
	hash_for_each(priv->udp_hash, bkt, sock, link) {
		seq_printf(seq, "udp %pI4:%d refcnt=%d fd=%d\n", &sock->addr,
			   sock->port, refcount_read(&sock->refcount),
			   sock->fd);
	}
	return 0;
}

static int __init golp_init(void)
{
	int err;
	struct net_device *dev = alloc_netdev(sizeof(struct golp_priv),
					      "golp", NET_NAME_PREDICTABLE,
					      golp_setup);
	if (dev == NULL)
		return -ENOMEM;
	err = register_netdev(dev);
	if (err < 0)
		return err;
	err = request_irq(GOLP_IRQ, golp_irq, 0, "golp", dev);
	if (err < 0)
		return err;

	debugfs_create_devm_seqfile(&dev->dev, "golp_socket_hash", NULL,
				    golp_socket_hash_read);
	return 0;
}
__initcall(golp_init);
