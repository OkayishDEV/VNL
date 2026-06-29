#ifndef _SYS_UIO_H
#define _SYS_UIO_H

#include <sys/types.h>
#include <sys/socket.h>

static inline ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    struct msghdr msg = {0};
    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = iovcnt;
    return sendmsg(fd, &msg, 0);
}

static inline ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    struct msghdr msg = {0};
    msg.msg_iov = (struct iovec *)iov;
    msg.msg_iovlen = iovcnt;
    return recvmsg(fd, &msg, 0);
}

#endif /* _SYS_UIO_H */
