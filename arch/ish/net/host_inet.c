#include <linux/init.h>
#include <linux/irqreturn.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/uio.h>
#include <net/sock.h>
#include <user/fs.h>

struct host_sock {
	struct sock sock;
	int fd;
	struct fd_listener listener;
};

static struct host_sock *host_sk(struct sock *sk)
{
	return (struct host_sock *) sk;
}

/* turn an iov_iter into an iovec array */

struct uiovec {
	struct user_iovec *vec;
	size_t len;
	size_t cap;
	struct page **pages;
	size_t nr_pages;
};
static void uiovec_grow(struct uiovec *uio)
{
	if (uio->len >= uio->cap) {
		if (uio->cap == 0)
			uio->cap = 8;
		else
			uio->cap *= 2;
		/* TODO check for failure */
		uio->vec = krealloc(uio->vec, uio->cap * sizeof(struct kvec), GFP_KERNEL);
	}
}
static int uiovec_from_iov_iter(struct iov_iter *iter, size_t n, struct uiovec *uio)
{
	int err;
	struct page **pages;
	size_t offset;
	ssize_t bytes;
	int npages;
	ssize_t i;

	uio->cap = 0;
	uio->len = 0;
	uio->vec = NULL;
	uio->nr_pages = 0;

	/* returns a bunch of pages as pages, offset into the first page as start, and total byte length */
	bytes = iov_iter_get_pages_alloc(iter, &pages, n, &offset);
	if (bytes < 0)
		return bytes;
	if (bytes == 0)
		return -ENOMEM;

	npages = DIV_ROUND_UP(bytes + offset, PAGE_SIZE);
	for (i = 0; i < npages; i++) {
		size_t len;

		uiovec_grow(uio);
		len = min_t(size_t, PAGE_SIZE - offset, bytes);
		uio->vec[uio->len].base = kmap(pages[i]) + offset;
		uio->vec[uio->len].len = len;
		uio->len++;
		offset = 0;
		bytes -= len;
	}

	uio->pages = pages;
	uio->nr_pages = npages;
	return 0;
}
static void uiovec_free(struct uiovec *uio)
{
	size_t i;

	kfree(uio->vec);
	for (i = 0; i < uio->nr_pages; i++) {
		put_page(uio->pages[i]);
	}
}

/* util */

static int check_sockaddr(struct sock *sk, void *addr, size_t addr_len)
{
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, addr);

	if (addr == NULL)
		return 0;

	switch (sk->sk_family) {
	case AF_INET:
		if (addr_len < sizeof(*sin))
			return -EINVAL;
		if (sin->sin_family != AF_INET)
			return -EAFNOSUPPORT;
		break;
	default:
		return -EAFNOSUPPORT;
	}
	return 0;
}

/* generic operations */

static int host_sock_release(struct socket *sock)
{
	struct host_sock *host = host_sk(sock->sk);
	host_close(host->fd); /* hopefully this unregisters the fd with the epoll */
	sock_put(sock->sk);
	sock->sk = NULL;
	return 0;
}

static int host_inet_sendmsg(struct socket *sock, struct msghdr *m, size_t total_len)
{
	struct sock *sk = sock->sk;
	struct host_sock *host = host_sk(sk);
	struct uiovec uio;
	int err;
	long timeo;
	DEFINE_WAIT(wait);

	err = check_sockaddr(sk, m->msg_name, m->msg_namelen);
	if (err < 0)
		return err;
	if (m->msg_control || m->msg_controllen) {
		printk("oh no a control message\n");
		return -EINVAL; // TODO
	}
	err = uiovec_from_iov_iter(&m->msg_iter, total_len, &uio);
	if (err < 0)
		return err;

	timeo = sock_sndtimeo(sk, m->msg_flags & MSG_DONTWAIT);
	for (;;) {
		int host_flags = m->msg_flags;
		host_flags &= ~(MSG_DONTWAIT | MSG_NOSIGNAL);

		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		err = host_sendmsg(host->fd, uio.vec, uio.len, m->msg_name, m->msg_namelen, host_flags);
		if (err != -EAGAIN || !timeo)
			break;
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
		timeo = schedule_timeout(timeo);
	}
	finish_wait(sk_sleep(sk), &wait);
	uiovec_free(&uio);

	if (sk->sk_type == SOCK_STREAM)
		err = sk_stream_error(sk, m->msg_flags, err);
	return err;
}

static int host_inet_recvmsg(struct socket *sock, struct msghdr *m, size_t total_len, int flags)
{
	struct sock *sk = sock->sk;
	struct host_sock *host = host_sk(sk);
	struct uiovec uio;
	int err;
	int noblock;
	long timeo;
	DEFINE_WAIT(wait);

	noblock = flags & MSG_DONTWAIT;
	flags &= ~MSG_DONTWAIT;
	flags &= ~MSG_CMSG_COMPAT; /* doesn't matter for inet messages */

	err = uiovec_from_iov_iter(&m->msg_iter, total_len, &uio);
	if (err < 0)
		return err;
	timeo = sock_rcvtimeo(sk, noblock);
	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		err = host_recvmsg(host->fd, uio.vec, uio.len, m->msg_name, &m->msg_namelen, &m->msg_flags, flags);
		if (err != -EAGAIN || !timeo)
			break;
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		}
		timeo = schedule_timeout(timeo);
	}
	finish_wait(sk_sleep(sk), &wait);
	uiovec_free(&uio);

	if (sk->sk_type == SOCK_STREAM)
		err = sk_stream_error(sk, flags, err);
	return err;
}

static int host_inet_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct host_sock *host = host_sk(sock->sk);
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, addr);
	int err;

	err = check_sockaddr(sock->sk, sin, addr_len);
	if (err < 0)
		return err;
	return host_bind(host->fd, sin, addr_len);
}

static int host_inet_connect(struct socket *sock, struct sockaddr *addr, int addr_len, int flags)
{
	struct host_sock *host = host_sk(sock->sk);
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, addr);
	int err;

	err = check_sockaddr(sock->sk, sin, addr_len);
	if (err < 0)
		return err;

	err = host_connect(host->fd, sin, addr_len);
	if (err == -EINPROGRESS) {
		DEFINE_WAIT(wait);
		long timeo = sock_sndtimeo(sock->sk, flags & O_NONBLOCK);
		while (timeo) {
			int poll_bits = fd_poll(host->fd);
			if (poll_bits < 0) {
				err = poll_bits;
				break;
			}
			if (poll_bits & POLLOUT) {
				err = 0;
				break;
			}
			prepare_to_wait(sk_sleep(sock->sk), &wait, TASK_INTERRUPTIBLE);
			if (signal_pending(current)) {
				err = -ERESTARTSYS;
				break;
			}
			timeo = schedule_timeout(timeo);
		}
		finish_wait(sk_sleep(sock->sk), &wait);
		err = 0;
	}

	if (err >= 0)
		err = host_get_so_error(host->fd);
	return err;
}

static __poll_t host_inet_poll(struct file *filp, struct socket *sock, struct poll_table_struct *wait)
{
	struct host_sock *host = host_sk(sock->sk);

	sock_poll_wait(filp, sock, wait);
	return fd_poll(host->fd);
}

static int host_inet_getname(struct socket *sock, struct sockaddr *addr, int peer)
{
	struct host_sock *host = host_sk(sock->sk);
	return host_getname(host->fd, addr, sizeof(*addr), peer);
}

static struct proto_ops host_inet_ops = {
	.family = PF_INET,
	.owner = THIS_MODULE,
	.release = host_sock_release,
	.bind = host_inet_bind,
	.connect = host_inet_connect,
	.getname = host_inet_getname,
	.poll = host_inet_poll,
	.setsockopt = sock_setsockopt,
	.getsockopt = sock_getsockopt,
	.sendmsg = host_inet_sendmsg,
	.recvmsg = host_inet_recvmsg,
};

static struct proto ping_proto = {
	.name = "PING",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct host_sock),
};

static struct proto udp_proto = {
	.name = "UDP",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct host_sock),
};

static struct proto tcp_proto = {
	.name = "TCP",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct host_sock),
};

static int inet_pipe_read, inet_pipe_write;

static irqreturn_t host_inet_irq(int irq, void *dev_id)
{
	struct sock *sk;
	rcu_read_lock();
	for (;;) {
		int err = host_read(inet_pipe_read, &sk, sizeof(struct sock *));
		if (err <= 0)
			break;

		BUG_ON(!sk);
		wake_up_interruptible_all(sk_sleep(sk));
	}
	rcu_read_unlock();
	return IRQ_HANDLED;
}

static int host_inet_create(struct net *net, struct socket *sock, int protocol, int kern)
{
	struct proto *proto = NULL;
	struct sock *sk;
	struct host_sock *host;
	int err;
	int host_type, host_proto;

	sock->state = SS_UNCONNECTED;
	sock->ops = &host_inet_ops;
	if ((sock->type == SOCK_DGRAM || sock->type == SOCK_RAW) && protocol == IPPROTO_ICMP) {
		proto = &ping_proto;
		host_type = SOCK_DGRAM;
		host_proto = IPPROTO_ICMP;
	} else if (sock->type == SOCK_DGRAM && (protocol == IPPROTO_UDP || protocol == IPPROTO_IP)) {
		proto = &udp_proto;
		host_type = SOCK_DGRAM;
		host_proto = IPPROTO_UDP;
	} else if (sock->type == SOCK_STREAM && (protocol == IPPROTO_TCP || protocol == IPPROTO_IP)) {
		proto = &tcp_proto;
		host_type = SOCK_STREAM;
		host_proto = IPPROTO_TCP;
	} else {
		printk("%d[%s] fuknope %d %d\n", current->pid, current->comm, sock->type, protocol);
		return -EINVAL;
	}

	sk = sk_alloc(net, PF_INET, GFP_KERNEL, proto, kern);
	if (!sk)
		return -ENOBUFS;
	sock_init_data(sock, sk);

	host = host_sk(sk);
	host->fd = host_socket(AF_INET, host_type, host_proto);
	if (host->fd < 0) {
		sock_put(sk);
		return host->fd;
	}

	err = fd_set_nonblock(host->fd);
	if (err < 0)
		goto err_close;
	host->listener.irq = HOST_INET_IRQ;
	host->listener.data = sk;
	host->listener.pipe = inet_pipe_write;
	err = fd_listen(host->fd, POLLIN|POLLOUT|EPOLLET, &host->listener);
	if (err < 0)
		goto err_close;

	return 0;

err_close:
	host_close(host->fd);
	if (sk)
		sock_put(sk);
	return err;
}

static struct net_proto_family host_inet_family = {
	.family = AF_INET,
	.create = host_inet_create,
};

static int __init host_inet_init(void)
{
	int err = host_pipe(&inet_pipe_read, &inet_pipe_write);
	if (err < 0) {
		pr_err("host-inet: failed host_pipe: %d\n", err);
		return err;
	}
	err = fd_set_nonblock(inet_pipe_read);
	if (err < 0) {
		pr_err("host-inet: failed to make host_pipe nonblock: %d\n", err);
		return err;
	}

	err = request_irq(HOST_INET_IRQ, host_inet_irq, 0, "host-inet", NULL);
	if (err < 0) {
		pr_err("host-inet: failed request_irq: %d\n", err);
		return err;
	}

	err = sock_register(&host_inet_family);
	if (err < 0) {
		pr_err("host-inet: failed sock_register: %d\n", err);
		return err;
	}
	return 0;
}
fs_initcall(host_inet_init);
