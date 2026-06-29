#pragma once
#include "types.h"

#define AF_UNIX  1
#define SOCK_STREAM 1

#define UNIX_SOCK_FD_BASE 128
#define UNIX_SOCK_MAX     24

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

struct msghdr {
    void         *msg_name;
    uint32_t      msg_namelen;
    struct iovec *msg_iov;
    uint64_t      msg_iovlen;
    void         *msg_control;
    uint64_t      msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

#define SOL_SOCKET 1
#define SCM_RIGHTS 1

void   unix_socket_init(void);
bool   unix_is_sockfd(int fd);
int    unix_sock_read(int fd, void *buf, size_t len);
int    unix_sock_write(int fd, const void *buf, size_t len);
int    unix_sock_close(int fd);

int unix_socket(int domain, int type, int protocol);
int unix_socketpair(int domain, int type, int protocol, int *sv);
int unix_bind(int fd, const void *addr, size_t addrlen);
int unix_listen(int fd, int backlog);
int unix_accept(int fd, void *addr, size_t *addrlen);
int unix_connect(int fd, const void *addr, size_t addrlen);

int unix_sock_sendmsg(int fd, const struct msghdr *msg, int flags);
int unix_sock_recvmsg(int fd, struct msghdr *msg, int flags);
int unix_sock_fcntl(int fd, int cmd, int64_t arg);
int unix_eventfd(unsigned int initval, int flags);
int unix_sock_poll(int fd, int events);
