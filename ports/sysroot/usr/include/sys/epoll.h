#ifndef _SYS_EPOLL_H
#define _SYS_EPOLL_H

#include <stdint.h>
#include <sys/types.h>

#define EPOLL_IN  0x001
#define EPOLL_OUT 0x004
#define EPOLL_ERR 0x008
#define EPOLL_HUP 0x010

#define EPOLLIN  EPOLL_IN
#define EPOLLOUT EPOLL_OUT
#define EPOLLERR EPOLL_ERR
#define EPOLLHUP EPOLL_HUP

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLL_CLOEXEC 02000000

typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event {
    uint32_t events;
    epoll_data_t data;
};

#ifdef __cplusplus
extern "C" {
#endif

static inline int epoll_create1(int flags) {
    long ret;
    __asm__ __volatile__ (
        "syscall"
        : "=a"(ret)
        : "a"(291), "D"(flags)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

static inline int epoll_create(int size) {
    (void)size;
    return epoll_create1(0);
}

static inline int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    long ret;
    register long r10 __asm__("r10") = (long)event;
    __asm__ __volatile__ (
        "syscall"
        : "=a"(ret)
        : "a"(233), "D"(epfd), "S"(op), "d"(fd), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

static inline int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    long ret;
    register long r10 __asm__("r10") = (long)timeout;
    __asm__ __volatile__ (
        "syscall"
        : "=a"(ret)
        : "a"(232), "D"(epfd), "S"(events), "d"(maxevents), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_EPOLL_H */
