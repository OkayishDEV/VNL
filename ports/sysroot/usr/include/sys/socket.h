#pragma once
#include <sys/types.h>

typedef unsigned int socklen_t;

#define AF_UNIX  1
#define AF_LOCAL 1
#define PF_UNIX  1
#define PF_LOCAL 1
#define SOCK_STREAM  1
#define SOCK_CLOEXEC 02000000

#define SOL_SOCKET 1
#define SCM_RIGHTS 0x01
#define MSG_CMSG_CLOEXEC 0x40000000
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    size_t        msg_iovlen;
    void         *msg_control;
    size_t        msg_controllen;
    int           msg_flags;
};

struct cmsghdr {
    size_t cmsg_len;
    int    cmsg_level;
    int    cmsg_type;
};

#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((unsigned char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(mhdr) ((size_t)(mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(mhdr)->msg_control : (struct cmsghdr *)0)

static inline struct cmsghdr *__cmsg_nxthdr(struct msghdr *mhdr, struct cmsghdr *cmsg) {
    if (cmsg->cmsg_len < sizeof(struct cmsghdr)) return (struct cmsghdr *)0;
    unsigned char *next = (unsigned char *)cmsg + CMSG_ALIGN(cmsg->cmsg_len);
    unsigned char *end = (unsigned char *)mhdr->msg_control + mhdr->msg_controllen;
    if (next + sizeof(struct cmsghdr) > end) return (struct cmsghdr *)0;
    if (next + CMSG_ALIGN(((struct cmsghdr *)next)->cmsg_len) > end) return (struct cmsghdr *)0;
    return (struct cmsghdr *)next;
}
#define CMSG_NXTHDR(mhdr, cmsg) __cmsg_nxthdr(mhdr, cmsg)

struct sockaddr {
    unsigned short sa_family;
    char           sa_data[14];
};

int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
int socketpair(int domain, int type, int protocol, int sv[2]);

struct ucred {
    int pid;
    int uid;
    int gid;
};
#define SO_PEERCRED 17

static inline int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    (void)sockfd; (void)level; (void)optname;
    if (optval && optlen && *optlen >= sizeof(struct ucred)) {
        struct ucred *u = (struct ucred *)optval;
        u->pid = 1;
        u->uid = 0;
        u->gid = 0;
        *optlen = sizeof(struct ucred);
        return 0;
    }
    return -1;
}
