#ifndef __ISH_USER_FS_H
#define __ISH_USER_FS_H

#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2
ssize_t host_write(int fd, const void *data, size_t len);
ssize_t host_read(int fd, void *data, size_t len);
ssize_t host_pwrite(int fd, void *data, size_t len, off_t offset);
ssize_t host_pread(int fd, void *data, size_t len, off_t offset);
int host_fstat_size(int fd, ssize_t *size);
int host_ftruncate(int fd, off_t length);
int fd_set_nonblock(int fd);
int host_fsync(int fd, int datasync);

struct host_timespec {
	uint64_t tv_sec;
	long tv_nsec;
};
int host_futimens(int fd, const struct host_timespec times[2]);

int host_open(const char *path, int flags);
int host_openat(int dir, const char *path, int flags, int mode);
int host_pipe(int *r, int *w);
int host_close(int fd);

int host_dup_opendir(int fd, void **dir_out);
struct host_dirent {
	uint64_t ino;
	uint8_t type;
	char *name;
};
int host_readdir(void *dir, struct host_dirent *out);
long host_telldir(void *dir);
int host_seekdir(void *dir, long off);
int host_rewinddir(void *dir);
int host_closedir(void *dir);

int host_renameat(int from_fd, const char *from, int to_fd, const char *to);
int host_linkat(int from_fd, const char *from, int to_fd, const char *to);
int host_unlinkat(int dir_fd, const char *path);
int host_mkdirat(int dir_fd, const char *path, int mode);
int host_rmdirat(int dir_fd, const char *path);

struct host_statfs {
	unsigned long bsize;
	unsigned long frsize;
	unsigned long blocks;
	unsigned long bfree;
	unsigned long bavail;
	unsigned long files;
	unsigned long ffree;
	unsigned long fsid;
	unsigned long namemax;
};
int host_fstatfs(int fd, struct host_statfs *statfs);

struct user_iovec {
	void *base;
	size_t len;
};

void termio_make_raw(int fd);
int termio_getwinsz(int fd, unsigned short *cols, unsigned short *rows);

struct fd_listener {
	int irq;
	/* If data is non-NULL, it will be written into pipe */
	void *data;
	int pipe;
};
int fd_listen(int fd, int types, struct fd_listener *listener);

int fd_poll(int fd);

/* net stuff */
int host_socket(int domain, int type, int protocol);
ssize_t host_sendmsg(int fd, struct user_iovec *iov, size_t iov_len, void *name, unsigned name_len, int flags);
ssize_t host_recvmsg(int fd, struct user_iovec *iov, size_t iov_len, void *name, int *name_len_out, int *flags_out, int flags);
int host_bind(int fd, void *name, int name_len);
int host_connect(int fd, void *name, int name_len);
#define GETNAME_SOCK 0
#define GETNAME_PEER 1
int host_getname(int fd, void *name, int name_len, int type);
int host_get_so_error(int fd);
int host_get_so_nwrite(int fd, uint32_t *res);
int host_get_so_nread(int fd, uint32_t *res);
int host_get_so_sndbuf(int fd, uint32_t *res);
int host_set_so_reuseport(int fd, int value);
int host_set_so_linger(int fd, int linger, int interval);
int host_shutdown_write(int fd);

#endif
