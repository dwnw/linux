/* sanitized (headers_install-style) linux/stddef.h */
#ifndef _LINUX_STDDEF_H
#define _LINUX_STDDEF_H

#ifndef __always_inline
#define __always_inline inline __attribute__((__always_inline__))
#endif
#ifndef __force
#define __force
#endif
#ifndef __user
#define __user
#endif
#ifndef __kernel
#define __kernel
#endif
#ifndef __bitwise
#define __bitwise
#endif

#undef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)

#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

#endif
