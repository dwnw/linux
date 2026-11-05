#ifndef __ISH_HOST_H
#define __ISH_HOST_H
#ifndef __KERNEL__
#include <stdint.h>
#endif

void *host_mmap(void *addr, size_t len);
uint64_t host_monotonic_nanos(void);
uint64_t host_unix_nanos(void);

void host_pause(void);
void host_exit(int status);

int host_start_thread(void (*entry)(void *), void *arg, void *stack, size_t stack_size);
int host_get_nproc(void);

const char *host_getenv(const char *name);

#endif
