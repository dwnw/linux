#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include <user/user.h>
#include <user/errno.h>
#include <user/fs.h>

int err_map(int err)
{
	switch (err) {
		case 0: return 0;
#define ERRNO_CASE(name, val) \
		case name: return -val;
		ERRNOS(ERRNO_CASE)
#undef ERRNO_CASE
		default:
			return -(0x800 | err);
	}
}
int errno_map()
{
	return err_map(errno);
}

int host_openat(int dir, const char *file, int flags, int mode)
{
	/* You may have noticed that this sucks. */
	int host_flags = 0;
	host_flags |= flags & O_ACCMODE;
	if (flags & 00000100) host_flags |= O_CREAT;
	if (flags & 00000200) host_flags |= O_EXCL;
	if (flags & 00200000) host_flags |= O_DIRECTORY;
	int fd = openat(dir, file, host_flags, mode);
	if (fd < 0)
		return errno_map();
	return fd;
}
int host_open(const char *file, int flags)
{
	return host_openat(AT_FDCWD, file, flags, 0);
}

int host_pipe(int *r, int *w) {
	int p[2];
	int err = pipe(p);
	if (err < 0)
		return errno_map();
	*r = p[0];
	*w = p[1];
	return 0;
}

ssize_t host_write(int fd, const void *data, size_t len)
{
	ssize_t res = write(fd, data, len);
	if (res < 0)
		return errno_map();
	return res;
}
ssize_t host_read(int fd, void *data, size_t len)
{
	ssize_t res = read(fd, data, len);
	if (res < 0)
		return errno_map();
	return res;
}
ssize_t host_pwrite(int fd, void *data, size_t len, off_t offset)
{
	ssize_t res = pwrite(fd, data, len, offset);
	if (res < 0)
		return errno_map();
	return res;
}
ssize_t host_pread(int fd, void *data, size_t len, off_t offset)
{
	ssize_t res = pread(fd, data, len, offset);
	if (res < 0)
		return errno_map();
	return res;
}

int host_fstat_size(int fd, ssize_t *size)
{
	struct stat stat;
	int err = fstat(fd, &stat);
	if (err < 0)
		return errno_map();
	*size = stat.st_size;
	return 0;
}

int host_ftruncate(int fd, off_t length)
{
	int err = ftruncate(fd, length);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_fsync(int fd, int datasync)
{
#if __linux__
	int err = (datasync ? fdatasync : fsync)(fd);
#else
    int err = fsync(fd);
#endif
	if (err < 0)
		return errno_map();
	return 0;
}

int host_futimens(int fd, const struct host_timespec in_times[2])
{
	struct timespec times[2] = {
		{.tv_sec = in_times[0].tv_sec, .tv_nsec = in_times[0].tv_nsec},
		{.tv_sec = in_times[1].tv_sec, .tv_nsec = in_times[1].tv_nsec},
	};
	int err = futimens(fd, times);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_dup_opendir(int fd, void **dir_out)
{
	fd = dup(fd);
	if (fd < 0)
		return errno_map();
	*dir_out = fdopendir(fd);
	if (*dir_out == NULL)
		return errno_map();
	return 0;
}

int host_readdir(void *dir, struct host_dirent *out)
{
	errno = 0;
	struct dirent *ent = readdir(dir);
	if (ent == NULL)
		return errno_map();
	out->ino = ent->d_ino;
	out->name = ent->d_name;
	out->type = ent->d_type;
	return 1;
}

long host_telldir(void *dir)
{
	long tell = telldir(dir);
	return tell;
}

int host_seekdir(void *dir, long off)
{
	seekdir(dir, off);
	return 0;
}

int host_rewinddir(void *dir)
{
	rewinddir(dir);
	return 0;
}

int host_closedir(void *dir)
{
	int err = closedir(dir);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_close(int fd)
{
	int err = close(fd);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_renameat(int from_fd, const char *from, int to_fd, const char *to)
{
	int err = renameat(from_fd, from, to_fd, to);
	if (err < 0)
		return errno_map();
	return 0;
}
int host_linkat(int from_fd, const char *from, int to_fd, const char *to)
{
	int err = linkat(from_fd, from, to_fd, to, 0);
	if (err < 0)
		return errno_map();
	return 0;
}
int host_unlinkat(int dir_fd, const char *path)
{
	int err = unlinkat(dir_fd, path, 0);
	if (err < 0)
		return errno_map();
	return 0;
}
int host_mkdirat(int dir_fd, const char *path, int mode)
{
	int err = mkdirat(dir_fd, path, mode);
	if (err < 0)
		return errno_map();
	return 0;
}
int host_rmdirat(int dir_fd, const char *path)
{
	int err = unlinkat(dir_fd, path, AT_REMOVEDIR);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_fstatfs(int fd, struct host_statfs *statfs)
{
	struct statvfs svfs;
	int err = fstatvfs(fd, &svfs);
	if (err < 0)
		return errno_map();
	statfs->bsize = svfs.f_bsize;
	statfs->frsize = svfs.f_frsize;
	statfs->blocks = svfs.f_blocks;
	statfs->bfree = svfs.f_bfree;
	statfs->bavail = svfs.f_bavail;
	statfs->files = svfs.f_files;
	statfs->ffree = svfs.f_ffree;
	statfs->fsid = svfs.f_fsid;
	statfs->namemax = svfs.f_namemax;
	return 0;
}

int host_socket(int domain, int type, int protocol)
{
	/* I can only promise this will work on darwin and linux */
	int sock = socket(domain, type, protocol);
	if (sock < 0)
		return errno_map();
#if defined(__APPLE__)
	/* Darwin has no MSG_NOSIGNAL; without SO_NOSIGPIPE a write to a broken
	 * connection raises SIGPIPE and can kill the app. */
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	return sock;
}

static_assert(sizeof(struct iovec) == sizeof(struct user_iovec), "iovec size");
static_assert(offsetof(struct iovec, iov_base) == offsetof(struct user_iovec, base), "iovec.iov_base");
static_assert(offsetof(struct iovec, iov_len) == offsetof(struct user_iovec, len), "iovec.iov_len");

struct kernel_sockaddr {
    uint16_t family;
    char data[14];
};
static_assert(offsetof(struct kernel_sockaddr, data) == offsetof(struct sockaddr, sa_data), "sockaddr.sa_data");

static void sockaddr_from_kernel(void *name, int name_len)
{
	struct sockaddr *host_name = name;
	struct kernel_sockaddr *kernel_name = name;
	if (name) {
		host_name->sa_family = kernel_name->family;
	}
}
static void sockaddr_to_kernel(void *name, int name_len)
{
	struct sockaddr *host_name = name;
	struct kernel_sockaddr *kernel_name = name;
	if (name) {
		kernel_name->family = host_name->sa_family;
	}
}

ssize_t host_sendmsg(int fd, struct user_iovec *iov, size_t iov_len, void *name, unsigned name_len, int flags)
{
	/* need to translate flags */
	/* need to translate name */
	assert(flags == 0);
	sockaddr_from_kernel(name, name_len);
	struct msghdr msg = {
		.msg_iov = (struct iovec *) iov,
		.msg_iovlen = iov_len,
		.msg_name = name,
		.msg_namelen = name_len,
	};
	ssize_t len = sendmsg(fd, &msg, flags);
	if (len < 0)
		return errno_map();
	return len;
}

ssize_t host_recvmsg(int fd, struct user_iovec *iov, size_t iov_len, void *name, int *name_len_out, int *flags_out, int kflags)
{
	int flags = 0;
	if (kflags & 0x40)
		flags |= MSG_DONTWAIT;
	if (kflags & 0x2)
		flags |= MSG_PEEK;
	assert((kflags & ~0x42) == 0);

	struct msghdr msg = {
		.msg_iov = (struct iovec *) iov,
		.msg_iovlen = iov_len,
		.msg_name = name,
		.msg_namelen = 128, /* TODO do better */
	};
	ssize_t len = recvmsg(fd, &msg, flags);
	if (len < 0)
		return errno_map();
	if (name) {
		sockaddr_to_kernel(msg.msg_name, msg.msg_namelen);
		*name_len_out = msg.msg_namelen;
	}
	msg.msg_flags &= ~flags;
	assert(msg.msg_flags == 0); /* TODO */
	if (flags_out)
		*flags_out = msg.msg_flags;
	return len;
}

int host_bind(int fd, void *name, int name_len)
{
	int err;

	sockaddr_from_kernel(name, name_len);
	err = bind(fd, name, name_len);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_connect(int fd, void *name, int name_len)
{
	int err;

	sockaddr_from_kernel(name, name_len);
	err = connect(fd, name, name_len);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_getname(int fd, void *name, int name_len, int type)
{
	socklen_t len = name_len;
	int err = (type == GETNAME_PEER ? getpeername : getsockname)(fd, name, &len);
	if (err < 0)
		return errno_map();
	assert(name_len == len);
	return 0;
}

int host_get_so_error(int fd)
{
	int so_error;
	socklen_t so_error_len = sizeof(so_error);
	int err = getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len);
	if (err < 0)
		return errno_map();
	return err_map(so_error);
}

static int __host_getsockopt(int fd, int level, int opt, void *res, socklen_t len)
{
	int err = getsockopt(fd, level, opt, res, &len);
	if (err < 0)
		return errno_map();
	return 0;
}

int host_get_so_nwrite(int fd, uint32_t *res)
{
#if defined(__APPLE__)
	return __host_getsockopt(fd, SOL_SOCKET, SO_NWRITE, res, sizeof(*res));
#elif defined(__linux__)
	int err = ioctl(fd, TIOCOUTQ, res);
	if (err < 0)
		return errno_map();
	return 0;
#endif
}

int host_get_so_nread(int fd, uint32_t *res)
{
#if defined(__APPLE__)
	return __host_getsockopt(fd, SOL_SOCKET, SO_NREAD, res, sizeof(*res));
#elif defined(__linux__)
	int err = ioctl(fd, FIONREAD, res);
	if (err < 0)
		return errno_map();
	return 0;
#endif
}

int host_get_so_sndbuf(int fd, uint32_t *res)
{
	return __host_getsockopt(fd, SOL_SOCKET, SO_SNDBUF, res, sizeof(*res));
}

int host_set_so_reuseport(int fd, int value)
{
	int err = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (int[]){1}, sizeof(int));
	if (err < 0)
		return errno_map();
	return 0;
}

int host_set_so_linger(int fd, int linger, int interval)
{
	struct linger l = {.l_onoff = linger, .l_linger = interval};
	int err = setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
	if (err < 0)
	    return errno_map();
	return 0;
}

int host_shutdown_write(int fd)
{
	int err = shutdown(fd, SHUT_WR);
	if (err < 0)
		return errno_map();
	return 0;
}

int fd_set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return errno_map();
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return errno_map();
	return 0;
}

int fd_poll(int fd)
{
	int err;
	/* This is dramatically simplified by assuming fd will always be a
	 * socket, and ignoring any Darwin idiosyncracies with other types of
	 * fds. */
	struct pollfd p = {.fd = fd, .events = POLLPRI};
	/* attempt to prevent POLLNVAL */
	int flags = fcntl(fd, F_GETFL, 0);
	if ((flags & O_ACCMODE) != O_WRONLY)
		p.events |= POLLIN;
	if ((flags & O_ACCMODE) != O_RDONLY)
		p.events |= POLLOUT;
	err = poll(&p, 1, 0);
	if (err < 0)
		return errno_map();
	if (err == 0)
		return 0;
	return p.revents;
}

void termio_make_raw(int fd)
{
	struct termios tio;
	tcgetattr(fd, &tio);
	cfmakeraw(&tio);
	tcsetattr(fd, TCSANOW, &tio);
}

int termio_getwinsz(int fd, unsigned short *cols, unsigned short *rows)
{
	struct winsize winsz;
	int err = ioctl(fd, TIOCGWINSZ, &winsz);
	if (err < 0)
		return errno_map();
	*cols = winsz.ws_col;
	*rows = winsz.ws_row;
	return 0;
}

void host_pause(void)
{
	pause();
}

void host_exit(int status)
{
	_exit(status);
}

void *host_mmap(void *addr, size_t len)
{
	return mmap(addr, len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
}

uint64_t host_monotonic_nanos(void)
{
#ifdef __APPLE__
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#endif
#ifdef __linux__
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

uint64_t host_unix_nanos(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

#if defined(__linux__)
int host_get_nproc(void)
{
	return sysconf(_SC_NPROCESSORS_ONLN);
}
#elif defined(__APPLE__)
int host_get_nproc(void)
{
	int nproc = -1;
	size_t sizeof_nproc = sizeof(nproc);
	int err = sysctlbyname("hw.activecpu", &nproc, &sizeof_nproc, NULL, 0);
	if (err < 0)
		return errno_map();
	return nproc;
}
#endif

const char *host_getenv(const char *name)
{
	return getenv(name);
}
