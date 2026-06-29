/*
 * Minimal AF_UNIX SOCK_STREAM Unix-domain local socket transport.
 */
#include "unix_socket.h"
#include "vfs.h"
#include "string.h"
#include "errno.h"
#include "sched.h"

#define RX_CAP 8192
#define ACCEPT_Q 8

typedef enum {
    SK_UNUSED = 0,
    SK_CREATED,
    SK_BOUND,
    SK_LISTENING,
    SK_CONNECTED,
} SkState;

typedef struct {
    bool    used;
    SkState state;
    char    path[120];
    uint8_t rx[RX_CAP];
    size_t  rhd, rtl;
    int     peer_idx;
    int listen_q[ACCEPT_Q];
    int n_wait;
    int     fds_rx[16];
    size_t  fds_rhd, fds_rtl;
    int     flags;
} USock;

#define PEER_NONE (-1)
static USock socks[UNIX_SOCK_MAX];

void unix_socket_init(void)
{
    memset(socks, 0, sizeof(socks));
}

static int idx_from_fd(int fd)
{
    if (fd < UNIX_SOCK_FD_BASE || fd >= UNIX_SOCK_FD_BASE + UNIX_SOCK_MAX) return -1;
    return fd - UNIX_SOCK_FD_BASE;
}

static int alloc_slot(void)
{
    for (int i = 0; i < UNIX_SOCK_MAX; i++) {
        if (!socks[i].used) {
            memset(&socks[i], 0, sizeof(socks[i]));
            socks[i].used      = true;
            socks[i].state     = SK_CREATED;
            socks[i].peer_idx  = PEER_NONE;
            socks[i].rhd = socks[i].rtl = 0;
            socks[i].n_wait    = 0;
            socks[i].fds_rhd = socks[i].fds_rtl = 0;
            return i;
        }
    }
    return -1;
}

static int push_rx(USock *dst, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        size_t next = (dst->rhd + 1) % RX_CAP;
        if (next == dst->rtl)
            return -ENOMEM;
        dst->rx[dst->rhd] = p[i];
        dst->rhd = next;
    }
    return (int)len;
}

static int pop_rx(USock *s, void *buf, size_t len)
{
    while (s->rtl == s->rhd && s->peer_idx != PEER_NONE) {
        if (s->flags & 2048) { // O_NONBLOCK = 2048
            return -EAGAIN;
        }
        task_sleep(1);
    }
    uint8_t *out = (uint8_t *)buf;
    size_t n = 0;
    while (n < len && s->rtl != s->rhd) {
        out[n++] = s->rx[s->rtl];
        s->rtl = (s->rtl + 1) % RX_CAP;
    }
    return (int)n;
}

bool unix_is_sockfd(int fd)
{
    int i = idx_from_fd(fd);
    return i >= 0 && socks[i].used;
}

int unix_socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != AF_UNIX)
        return -EINVAL;
    if (type != SOCK_STREAM)
        return -EINVAL;
    int i = alloc_slot();
    if (i < 0)
        return -ENOMEM;
    return UNIX_SOCK_FD_BASE + i;
}

int unix_socketpair(int domain, int type, int protocol, int *sv)
{
    (void)protocol;
    if (!sv)
        return -EFAULT;
    if (domain != AF_UNIX || type != SOCK_STREAM)
        return -EINVAL;
    int a = alloc_slot();
    if (a < 0)
        return -ENOMEM;
    int b = alloc_slot();
    if (b < 0) {
        socks[a].used = false;
        return -ENOMEM;
    }
    socks[a].state    = SK_CONNECTED;
    socks[b].state    = SK_CONNECTED;
    socks[a].peer_idx = b;
    socks[b].peer_idx = a;
    sv[0] = UNIX_SOCK_FD_BASE + a;
    sv[1] = UNIX_SOCK_FD_BASE + b;
    return 0;
}

int unix_bind(int fd, const void *addr, size_t addrlen)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CREATED)
        return -EINVAL;
    if (!addr || addrlen < 3)
        return -EINVAL;
    uint16_t fam = *(const uint16_t *)addr;
    if (fam != AF_UNIX)
        return -EINVAL;
    size_t plen = addrlen - sizeof(uint16_t);
    if (plen == 0 || plen >= sizeof(s->path))
        return -EINVAL;
    memcpy(s->path, (const char *)addr + sizeof(uint16_t), plen);
    s->path[plen] = '\0';
    if (s->path[0] == '\0')
        return -EINVAL; /* abstract: not yet */

    if (vfs_resolve(s->path) >= 0)
        return -EADDRINUSE;
    for (int k = 0; k < UNIX_SOCK_MAX; k++) {
        if (!socks[k].used || k == i)
            continue;
        if (socks[k].path[0] && strcmp(socks[k].path, s->path) == 0)
            return -EADDRINUSE;
    }
    int tfd = vfs_open(s->path, VFS_O_RDWR | VFS_O_CREATE | VFS_O_TRUNC);
    if (tfd < 0)
        return -EIO;
    vfs_close(tfd);
    s->state = SK_BOUND;
    return 0;
}

int unix_listen(int fd, int backlog)
{
    (void)backlog;
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_BOUND)
        return -EINVAL;
    s->state = SK_LISTENING;
    return 0;
}

static int find_listener(const char *path)
{
    for (int k = 0; k < UNIX_SOCK_MAX; k++) {
        if (!socks[k].used)
            continue;
        if (socks[k].state != SK_LISTENING)
            continue;
        if (strcmp(socks[k].path, path) == 0)
            return k;
    }
    return -1;
}

int unix_accept(int fd, void *addr, size_t *addrlen)
{
    (void)addr;
    if (addrlen)
        *addrlen = 0;
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *L = &socks[i];
    if (L->state != SK_LISTENING)
        return -EINVAL;
    while (L->n_wait == 0) {
        if (L->flags & 2048) { // O_NONBLOCK = 2048
            return -EAGAIN;
        }
        task_sleep(1);
    }
    int si = L->listen_q[0];
    memmove(L->listen_q, L->listen_q + 1, (size_t)(L->n_wait - 1) * sizeof(int));
    L->n_wait--;
    if (si < 0 || si >= UNIX_SOCK_MAX || !socks[si].used)
        return -EIO;
    return UNIX_SOCK_FD_BASE + si;
}

int unix_connect(int fd, const void *addr, size_t addrlen)
{
    int ci = idx_from_fd(fd);
    if (ci < 0 || !socks[ci].used)
        return -EBADF;
    USock *C = &socks[ci];
    if (C->state != SK_CREATED)
        return -EINVAL;
    if (!addr || addrlen < 3)
        return -EINVAL;
    if (*(const uint16_t *)addr != AF_UNIX)
        return -EINVAL;
    char path[120];
    size_t plen = addrlen - sizeof(uint16_t);
    if (plen == 0 || plen >= sizeof(path))
        return -EINVAL;
    memcpy(path, (const char *)addr + sizeof(uint16_t), plen);
    path[plen] = '\0';

    int li = find_listener(path);
    if (li < 0)
        return -ECONNREFUSED;
    USock *L = &socks[li];

    int si = alloc_slot();
    if (si < 0)
        return -ENOMEM;
    USock *S = &socks[si];
    S->state    = SK_CONNECTED;
    C->state    = SK_CONNECTED;
    C->peer_idx = si;
    S->peer_idx = ci;
    if (L->n_wait >= ACCEPT_Q) {
        S->used = false;
        C->peer_idx = PEER_NONE;
        C->state    = SK_CREATED;
        return -ECONNREFUSED;
    }
    L->listen_q[L->n_wait++] = si;
    return 0;
}

int unix_sock_read(int fd, void *buf, size_t len)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CONNECTED)
        return -ENOTCONN;
    return pop_rx(s, buf, len);
}

int unix_sock_write(int fd, const void *buf, size_t len)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CONNECTED || s->peer_idx == PEER_NONE)
        return -ENOTCONN;
    USock *peer = &socks[s->peer_idx];
    return push_rx(peer, buf, len);
}

int unix_sock_close(int fd)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used)
        return -EBADF;
    USock *s = &socks[i];
    if (s->peer_idx != PEER_NONE && s->peer_idx >= 0 && s->peer_idx < UNIX_SOCK_MAX) {
        USock *p = &socks[s->peer_idx];
        if (p->used && p->peer_idx == i)
            p->peer_idx = PEER_NONE;
    }
    s->peer_idx = PEER_NONE;
    if ((s->state == SK_BOUND || s->state == SK_LISTENING) && s->path[0])
        vfs_unlink(s->path);
    
    while (s->fds_rtl != s->fds_rhd) {
        int fd_to_close = s->fds_rx[s->fds_rtl];
        if (fd_to_close >= 0 && !unix_is_sockfd(fd_to_close)) {
            vfs_close(fd_to_close);
        }
        s->fds_rtl = (s->fds_rtl + 1) % 16;
    }
    
    s->used = false;
    memset(s, 0, sizeof(*s));
    return 0;
}

int unix_sock_sendmsg(int fd, const struct msghdr *msg, int flags)
{
    (void)flags;
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used) return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CONNECTED || s->peer_idx == PEER_NONE) return -ENOTCONN;
    USock *peer = &socks[s->peer_idx];

    if (!msg) return -EFAULT;

    int total_sent = 0;
    for (size_t k = 0; k < msg->msg_iovlen; k++) {
        struct iovec iov = msg->msg_iov[k];
        if (iov.iov_len == 0) continue;
        if (!iov.iov_base) return -EFAULT;
        int sent = push_rx(peer, iov.iov_base, iov.iov_len);
        if (sent < 0) return sent;
        total_sent += sent;
    }

    if (msg->msg_control && msg->msg_controllen >= sizeof(struct cmsghdr)) {
        size_t offset = 0;
        while (offset + sizeof(struct cmsghdr) <= msg->msg_controllen) {
            struct cmsghdr *cmsg = (struct cmsghdr *)((char *)msg->msg_control + offset);
            if (cmsg->cmsg_len < sizeof(struct cmsghdr) || offset + cmsg->cmsg_len > msg->msg_controllen)
                break;
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                int *fds_buf = (int *)((char *)cmsg + sizeof(struct cmsghdr));
                size_t num_fds = (cmsg->cmsg_len - sizeof(struct cmsghdr)) / sizeof(int);
                for (size_t f = 0; f < num_fds; f++) {
                    int send_fd = fds_buf[f];
                    int recv_fd = -1;
                    if (unix_is_sockfd(send_fd)) {
                        recv_fd = send_fd;
                    } else {
                        recv_fd = vfs_dup(send_fd);
                        if (recv_fd < 0) return recv_fd;
                    }

                    size_t next = (peer->fds_rhd + 1) % 16;
                    if (next == peer->fds_rtl) {
                        if (!unix_is_sockfd(recv_fd)) vfs_close(recv_fd);
                        return -ENOMEM;
                    }
                    peer->fds_rx[peer->fds_rhd] = recv_fd;
                    peer->fds_rhd = next;
                }
            }
            offset += ALIGN_UP(cmsg->cmsg_len, 8);
        }
    }

    return total_sent;
}

int unix_sock_recvmsg(int fd, struct msghdr *msg, int flags)
{
    (void)flags;
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used) return -EBADF;
    USock *s = &socks[i];
    if (s->state != SK_CONNECTED) return -ENOTCONN;

    if (!msg) return -EFAULT;

    int total_recvd = 0;
    for (size_t k = 0; k < msg->msg_iovlen; k++) {
        struct iovec iov = msg->msg_iov[k];
        if (iov.iov_len == 0) continue;
        if (!iov.iov_base) return -EFAULT;
        int recvd = pop_rx(s, iov.iov_base, iov.iov_len);
        if (recvd < 0) return recvd;
        total_recvd += recvd;
        if (s->rtl == s->rhd) break;
    }

    if (msg->msg_control && msg->msg_controllen >= sizeof(struct cmsghdr)) {
        struct cmsghdr *cmsg = (struct cmsghdr *)msg->msg_control;
        size_t max_fds = (msg->msg_controllen - sizeof(struct cmsghdr)) / sizeof(int);
        size_t popped_fds = 0;
        int *out_fds = (int *)((char *)cmsg + sizeof(struct cmsghdr));

        while (popped_fds < max_fds && s->fds_rtl != s->fds_rhd) {
            out_fds[popped_fds++] = s->fds_rx[s->fds_rtl];
            s->fds_rtl = (s->fds_rtl + 1) % 16;
        }

        if (popped_fds > 0) {
            cmsg->cmsg_len = sizeof(struct cmsghdr) + popped_fds * sizeof(int);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            msg->msg_controllen = cmsg->cmsg_len;
        } else {
            msg->msg_controllen = 0;
        }
    } else {
        msg->msg_controllen = 0;
    }

    return total_recvd;
}

int unix_sock_fcntl(int fd, int cmd, int64_t arg)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used) return -EBADF;
    USock *s = &socks[i];
    if (cmd == 3) { // F_GETFL
        return s->flags;
    } else if (cmd == 4) { // F_SETFL
        s->flags = (int)arg;
        return 0;
    }
    return -EINVAL;
}

int unix_eventfd(unsigned int initval, int flags)
{
    int i = alloc_slot();
    if (i < 0) return -ENOMEM;
    socks[i].state = SK_CONNECTED;
    socks[i].peer_idx = i; // Connect to itself
    socks[i].flags = flags;
    if (initval > 0) {
        uint64_t val = initval;
        push_rx(&socks[i], &val, sizeof(val));
    }
    return UNIX_SOCK_FD_BASE + i;
}

int unix_sock_poll(int fd, int events)
{
    int i = idx_from_fd(fd);
    if (i < 0 || !socks[i].used) return 0;
    USock *s = &socks[i];
    int revents = 0;
    if (events & 0x001) { // POLLIN
        if (s->rtl != s->rhd || s->peer_idx == PEER_NONE) {
            revents |= 0x001;
        }
    }
    if (events & 0x004) { // POLLOUT
        if (s->peer_idx != PEER_NONE) {
            int peer = s->peer_idx;
            size_t free_space = (socks[peer].rtl + RX_CAP - 1 - socks[peer].rhd) % RX_CAP;
            if (free_space > 0) {
                revents |= 0x004;
            }
        }
    }
    if (s->peer_idx == PEER_NONE && s->state == SK_CONNECTED) {
        revents |= 0x010; // POLLHUP
    }
    return revents;
}
