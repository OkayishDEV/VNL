#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H

#include <stdint.h>
#include <unistd.h>

#define EFD_SEMAPHORE 1
#define EFD_CLOEXEC 02000000
#define EFD_NONBLOCK 00004000

typedef uint64_t eventfd_t;

#ifdef __cplusplus
extern "C" {
#endif

static inline int eventfd(unsigned int initval, int flags) {
    long ret;
    __asm__ __volatile__ (
        "syscall"
        : "=a"(ret)
        : "a"(290), "D"(initval), "S"(flags)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

static inline int eventfd_read(int fd, eventfd_t *value) {
    if (!value) return -1;
    return read(fd, value, sizeof(*value)) == sizeof(*value) ? 0 : -1;
}

static inline int eventfd_write(int fd, eventfd_t value) {
    return write(fd, &value, sizeof(value)) == sizeof(value) ? 0 : -1;
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_EVENTFD_H */
