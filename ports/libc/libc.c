#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <math.h>
#include <sys/time.h>

int errno = 0;

/* ---- Assembly Raw Syscall helper --------------------------------- */
static inline int64_t syscall_raw(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    int64_t ret;
    register int64_t r10 asm("r10") = a4;
    register int64_t r8  asm("r8")  = a5;
    register int64_t r9  asm("r9")  = a6;
    asm volatile("movq %1, %%rax; movq %2, %%rdi; movq %3, %%rsi; movq %4, %%rdx; int $0x80; movq %%rax, %0"
        : "=r"(ret)
        : "g"(num), "g"(a1), "g"(a2), "g"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rax", "rdi", "rsi", "rdx", "memory");
    return ret;
}

#define sys_read(fd, buf, sz)           syscall_raw(0, fd, (intptr_t)buf, sz, 0, 0, 0)
#define sys_write(fd, buf, sz)          syscall_raw(1, fd, (intptr_t)buf, sz, 0, 0, 0)
#define sys_open(path, flags)           syscall_raw(2, (intptr_t)path, flags, 0, 0, 0, 0)
#define sys_close(fd)                   syscall_raw(3, fd, 0, 0, 0, 0, 0)
#define sys_stat(path, st)              syscall_raw(4, (intptr_t)path, (intptr_t)st, 0, 0, 0, 0)
#define sys_fstat(fd, st)               syscall_raw(5, fd, (intptr_t)st, 0, 0, 0, 0)
#define sys_lstat(path, st)             syscall_raw(6, (intptr_t)path, (intptr_t)st, 0, 0, 0, 0)
#define sys_lseek(fd, off, whence)      syscall_raw(8, fd, off, whence, 0, 0, 0)
#define sys_mmap(adr, len, pr, fl, fd, o) syscall_raw(9, (intptr_t)adr, len, pr, fl, fd, o)
#define sys_mprotect(adr, len, pr)      syscall_raw(10, (intptr_t)adr, len, pr, 0, 0, 0)
#define sys_munmap(adr, len)            syscall_raw(11, (intptr_t)adr, len, 0, 0, 0, 0)
#define sys_ioctl(fd, req, arg)         syscall_raw(16, fd, req, (intptr_t)arg, 0, 0, 0)
#define sys_pipe(fds)                   syscall_raw(22, (intptr_t)fds, 0, 0, 0, 0, 0)
#define sys_dup(fd)                     syscall_raw(32, fd, 0, 0, 0, 0, 0)
#define sys_dup2(oldfd, newfd)          syscall_raw(33, oldfd, newfd, 0, 0, 0, 0)
#define sys_getpid()                    syscall_raw(39, 0, 0, 0, 0, 0, 0)
#define sys_socket(dom, ty, pr)         syscall_raw(41, dom, ty, pr, 0, 0, 0)
#define sys_connect(fd, adr, len)       syscall_raw(42, fd, (intptr_t)adr, len, 0, 0, 0)
#define sys_accept(fd, adr, len)        syscall_raw(43, fd, (intptr_t)adr, (intptr_t)len, 0, 0, 0)
#define sys_bind(fd, adr, len)          syscall_raw(49, fd, (intptr_t)adr, len, 0, 0, 0)
#define sys_listen(fd, bl)              syscall_raw(50, fd, bl, 0, 0, 0, 0)
#define sys_sendmsg(fd, msg, fl)        syscall_raw(46, fd, (intptr_t)msg, fl, 0, 0, 0)
#define sys_recvmsg(fd, msg, fl)        syscall_raw(47, fd, (intptr_t)msg, fl, 0, 0, 0)
#define sys_socketpair(dom, ty, pr, sv) syscall_raw(53, dom, ty, pr, (intptr_t)sv, 0, 0)
#define sys_exit(st)                    syscall_raw(60, st, 0, 0, 0, 0, 0)
#define sys_ftruncate(fd, len)          syscall_raw(77, fd, len, 0, 0, 0, 0)
#define sys_getcwd(buf, sz)             syscall_raw(79, (intptr_t)buf, sz, 0, 0, 0, 0)
#define sys_mkdir(path, md)             syscall_raw(83, (intptr_t)path, md, 0, 0, 0, 0)
#define sys_unlink(path)                syscall_raw(87, (intptr_t)path, 0, 0, 0, 0, 0)
#define sys_readlink(path, buf, sz)     syscall_raw(89, (intptr_t)path, (intptr_t)buf, sz, 0, 0, 0)
#define sys_getuid()                    syscall_raw(102, 0, 0, 0, 0, 0, 0)
#define sys_getgid()                    syscall_raw(104, 0, 0, 0, 0, 0, 0)
#define sys_getegid()                   syscall_raw(108, 0, 0, 0, 0, 0, 0)
#define sys_getdents64(fd, buf, sz)     syscall_raw(217, fd, (intptr_t)buf, sz, 0, 0, 0)
#define sys_exit_group(st)              syscall_raw(231, st, 0, 0, 0, 0, 0)
#define sys_fcntl(fd, cmd, arg)         syscall_raw(72, fd, cmd, (intptr_t)arg, 0, 0, 0)

#include <sys/timerfd.h>
#include <sys/signalfd.h>

struct timerfd_info {
    int read_fd;
    int write_fd;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct timespec deadline;
    int active;
    int stop;
};

#define MAX_TIMERFDS 32
static struct timerfd_info *timerfds[MAX_TIMERFDS] = {0};

struct signalfd_info {
    int read_fd;
    int write_fd;
    sigset_t mask;
};

#define MAX_SIGNALFDS 32
static struct signalfd_info *signalfds[MAX_SIGNALFDS] = {0};

/* ---- standard IO wrappers ---------------------------------------- */
int open(const char *pathname, int flags, ...) {
    int64_t r = sys_open(pathname, flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
int close(int fd) {
    // Check if it's a timerfd
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (timerfds[i] && (timerfds[i]->read_fd == fd || timerfds[i]->write_fd == fd)) {
            struct timerfd_info *t = timerfds[i];
            timerfds[i] = NULL;
            pthread_mutex_lock(&t->lock);
            t->stop = 1;
            pthread_cond_signal(&t->cond);
            pthread_mutex_unlock(&t->lock);
            pthread_join(t->thread, NULL);
            sys_close(t->read_fd);
            sys_close(t->write_fd);
            pthread_mutex_destroy(&t->lock);
            pthread_cond_destroy(&t->cond);
            free(t);
            return 0;
        }
    }
    
    // Check if it's a signalfd
    for (int i = 0; i < MAX_SIGNALFDS; i++) {
        if (signalfds[i] && (signalfds[i]->read_fd == fd || signalfds[i]->write_fd == fd)) {
            struct signalfd_info *s = signalfds[i];
            signalfds[i] = NULL;
            sys_close(s->read_fd);
            sys_close(s->write_fd);
            free(s);
            return 0;
        }
    }
    
    int64_t r = sys_close(fd);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int pipe(int pipefd[2]) {
    int64_t r = sys_pipe(pipefd);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
ssize_t read(int fd, void *buf, size_t count) {
    int64_t r = sys_read(fd, buf, count);
    if (r < 0) { errno = (int)-r; return -1; }
    return (ssize_t)r;
}
ssize_t write(int fd, const void *buf, size_t count) {
    int64_t r = sys_write(fd, buf, count);
    if (r < 0) { errno = (int)-r; return -1; }
    return (ssize_t)r;
}
off_t lseek(int fd, off_t offset, int whence) {
    int64_t r = sys_lseek(fd, offset, whence);
    if (r < 0) { errno = (int)-r; return -1; }
    return (off_t)r;
}
int dup(int oldfd) {
    int64_t r = sys_dup(oldfd);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
int dup2(int oldfd, int newfd) {
    int64_t r = sys_dup2(oldfd, newfd);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
char *getcwd(char *buf, size_t size) {
    int64_t r = sys_getcwd(buf, size);
    if (r < 0) { errno = (int)-r; return NULL; }
    return buf;
}
int unlink(const char *pathname) {
    int64_t r = sys_unlink(pathname);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int mkdir(const char *pathname, mode_t mode) {
    int64_t r = sys_mkdir(pathname, mode);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    int64_t r = sys_readlink(pathname, buf, bufsiz);
    if (r < 0) { errno = (int)-r; return -1; }
    return (ssize_t)r;
}
pid_t getpid(void) { return (pid_t)sys_getpid(); }
uid_t getuid(void) { return (uid_t)sys_getuid(); }
uid_t geteuid(void) { return (uid_t)sys_getuid(); }
gid_t getgid(void) { return (gid_t)sys_getgid(); }
gid_t getegid(void) { return (gid_t)sys_getegid(); }

void exit(int status) { sys_exit_group(status); __builtin_unreachable(); }
void abort(void) { sys_exit_group(127); __builtin_unreachable(); }
void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    printf("Assertion failed: %s (%s: %u: %s)\n", assertion, file, line, function);
    abort();
}

/* ---- stat wrappers ----------------------------------------------- */
static int translate_stat(int64_t r, struct stat *statbuf) {
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int stat(const char *pathname, struct stat *statbuf) { return translate_stat(sys_stat(pathname, statbuf), statbuf); }
int fstat(int fd, struct stat *statbuf) { return translate_stat(sys_fstat(fd, statbuf), statbuf); }
int lstat(const char *pathname, struct stat *statbuf) { return translate_stat(sys_lstat(pathname, statbuf), statbuf); }

/* ---- mman wrappers ----------------------------------------------- */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    int64_t r = sys_mmap(addr, length, prot, flags, fd, offset);
    if (r < 0) { errno = (int)-r; return MAP_FAILED; }
    return (void *)r;
}
int munmap(void *addr, size_t length) {
    int64_t r = sys_munmap(addr, length);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int mprotect(void *addr, size_t len, int prot) {
    int64_t r = sys_mprotect(addr, len, prot);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int ftruncate(int fd, off_t length) {
    int64_t r = sys_ftruncate(fd, length);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

/* ---- sockets ---------------------------------------------------- */
int socket(int domain, int type, int protocol) {
    int64_t r = sys_socket(domain, type, protocol);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int64_t r = sys_connect(sockfd, addr, addrlen);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    int64_t r = sys_accept(sockfd, addr, addrlen);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    int64_t r = sys_bind(sockfd, addr, addrlen);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int listen(int sockfd, int backlog) {
    int64_t r = sys_listen(sockfd, backlog);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags) {
    int64_t r = sys_sendmsg(sockfd, msg, flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return (ssize_t)r;
}
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    int64_t r = sys_recvmsg(sockfd, msg, flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return (ssize_t)r;
}
int socketpair(int domain, int type, int protocol, int sv[2]) {
    int64_t r = sys_socketpair(domain, type, protocol, sv);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

/* ---- ioctl, poll, dirent ----------------------------------------- */
int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    int64_t r = sys_ioctl(fd, request, arg);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
int fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    int64_t r = sys_fcntl(fd, cmd, arg);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}
int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    /* Simple poll stub: if timeout, sleep. In VNL, sockets block or return immediately. */
    (void)fds; (void)nfds;
    if (timeout > 0) {
        syscall_raw(103, timeout, 0, 0, 0, 0, 0); /* task sleep */
    }
    return 0;
}

DIR *opendir(const char *name) {
    int fd = open(name, O_RDONLY);
    if (fd < 0) return NULL;
    DIR *dir = malloc(sizeof(DIR));
    if (!dir) { close(fd); return NULL; }
    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_end = 0;
    return dir;
}
struct dirent *readdir(DIR *dirp) {
    if (dirp->buf_pos >= dirp->buf_end) {
        int r = (int)sys_getdents64(dirp->fd, dirp->buf, sizeof(dirp->buf));
        if (r <= 0) return NULL;
        dirp->buf_end = r;
        dirp->buf_pos = 0;
    }
    struct dirent *de = (struct dirent *)(dirp->buf + dirp->buf_pos);
    dirp->buf_pos += de->d_reclen;
    return de;
}
int closedir(DIR *dirp) {
    close(dirp->fd);
    free(dirp);
    return 0;
}

/* ---- Heap Memory Allocator (First-Fit) ---------------------------- */
struct block_hdr {
    size_t size;
    int    free;
    struct block_hdr *next;
};

static struct block_hdr *free_list_head = NULL;

void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~7ULL; /* 8-byte alignment */

    struct block_hdr *curr = free_list_head;
    struct block_hdr *prev = NULL;
    while (curr) {
        if (curr->free && curr->size >= size) {
            curr->free = 0;
            return (void *)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    /* Allocate new page chunk from kernel via mmap */
    size_t chunk_size = size + sizeof(struct block_hdr);
    if (chunk_size < 65536) chunk_size = 65536; /* 64KB minimum mapping */
    
    struct block_hdr *block = (struct block_hdr *)mmap(NULL, chunk_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED) return NULL;

    block->size = chunk_size - sizeof(struct block_hdr);
    block->free = 0;
    block->next = NULL;

    if (prev) {
        prev->next = block;
    } else {
        free_list_head = block;
    }

    /* If block has extra remaining space, split it */
    if (block->size > size + sizeof(struct block_hdr) + 8) {
        struct block_hdr *next_block = (struct block_hdr *)((char *)(block + 1) + size);
        next_block->size = block->size - size - sizeof(struct block_hdr);
        next_block->free = 1;
        next_block->next = block->next;
        block->size = size;
        block->next = next_block;
    }

    return (void *)(block + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    struct block_hdr *block = (struct block_hdr *)ptr - 1;
    block->free = 1;

    /* Merge adjacent free blocks */
    struct block_hdr *curr = free_list_head;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            curr->size += sizeof(struct block_hdr) + curr->next->size;
            curr->next = curr->next->next;
        }
        curr = curr->next;
    }
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    
    struct block_hdr *block = (struct block_hdr *)ptr - 1;
    if (block->size >= size) return ptr;
    
    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        free(ptr);
    }
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

/* ---- String and Formatting helpers ------------------------------ */
size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}
char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}
char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && (*dest++ = *src++)) n--;
    while (n--) *dest++ = '\0';
    return ret;
}
char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return ret;
}
char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) *dest++ = *src++;
    *dest = '\0';
    return ret;
}
int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}
int strncmp(const char *s1, const char *s2, size_t n) {
    while (n-- && *s1 && *s1 == *s2) { s1++; s2++; }
    if (n == (size_t)-1) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}
char *strchr(const char *s, int c) {
    for (; *s; s++)
        if ((unsigned char)*s == (unsigned char)c) return (char *)s;
    return NULL;
}
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (; *s; s++)
        if ((unsigned char)*s == (unsigned char)c) last = s;
    return (char *)last;
}
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    size_t nl = strlen(needle);
    for (; *haystack; haystack++)
        if (strncmp(haystack, needle, nl) == 0) return (char *)haystack;
    return NULL;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
char *strndup(const char *s, size_t n) {
    size_t len = strlen(s);
    if (len > n) len = n;
    char *p = malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}
void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}
void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p = s1, *q = s2;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}
char *strerror(int errnum) {
    (void)errnum;
    return "Unknown POSIX/VNL Error";
}

/* ---- Printf & stream implementation ----------------------------- */
FILE FILE_stdin = {0};
FILE FILE_stdout = {1};
FILE FILE_stderr = {2};

FILE *stdin = &FILE_stdin;
FILE *stdout = &FILE_stdout;
FILE *stderr = &FILE_stderr;

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    /* Lightweight snprintf using compiler va_arg */
    char buf[1024];
    int pos = 0;
    const char *fmt = format;
    while (*fmt && pos < (int)sizeof(buf)-1) {
        if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
        fmt++;
        if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < (int)sizeof(buf)-1) buf[pos++] = *s++;
            fmt++;
        } else if (*fmt == 'd') {
            int val = __builtin_va_arg(ap, int);
            if (val < 0) { buf[pos++] = '-'; val = -val; }
            char tmp[16]; int len = 0;
            if (val == 0) tmp[len++] = '0';
            while (val) { tmp[len++] = '0' + (val % 10); val /= 10; }
            for (int i = len-1; i >= 0 && pos < (int)sizeof(buf)-1; i--) buf[pos++] = tmp[i];
            fmt++;
        } else if (*fmt == 'u') {
            unsigned int val = __builtin_va_arg(ap, unsigned int);
            char tmp[16]; int len = 0;
            if (val == 0) tmp[len++] = '0';
            while (val) { tmp[len++] = '0' + (val % 10); val /= 10; }
            for (int i = len-1; i >= 0 && pos < (int)sizeof(buf)-1; i--) buf[pos++] = tmp[i];
            fmt++;
        } else if (*fmt == 'x') {
            unsigned int val = __builtin_va_arg(ap, unsigned int);
            char tmp[16]; int len = 0;
            if (val == 0) tmp[len++] = '0';
            while (val) {
                int rem = val % 16;
                tmp[len++] = rem < 10 ? '0' + rem : 'a' + rem - 10;
                val /= 16;
            }
            for (int i = len-1; i >= 0 && pos < (int)sizeof(buf)-1; i--) buf[pos++] = tmp[i];
            fmt++;
        } else {
            buf[pos++] = '%';
            if (*fmt) buf[pos++] = *fmt++;
        }
    }
    buf[pos] = '\0';
    size_t copy_len = pos < (int)size ? (size_t)pos : size - 1;
    memcpy(str, buf, copy_len);
    str[copy_len] = '\0';
    return pos;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(str, size, format, ap);
    va_end(ap);
    return n;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(str, 65536, format, ap);
    va_end(ap);
    return n;
}

int printf(const char *format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    write(1, buf, (size_t)n);
    return n;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    if (n < 0) return n;
    fwrite(buf, 1, (size_t)n, stream);
    return n;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int n = vfprintf(stream, format, ap);
    va_end(ap);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int puts(const char *s) {
    write(1, s, strlen(s));
    char nl = '\n';
    write(1, &nl, 1);
    return 0;
}

int fputs(const char *s, FILE *stream) {
    fwrite(s, 1, strlen(s), stream);
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    if (stream->is_memstream) {
        if (stream->len + total >= stream->cap) {
            size_t new_cap = stream->cap * 2 + total;
            char *new_buf = realloc(stream->buf, new_cap);
            if (!new_buf) return 0;
            stream->buf = new_buf;
            stream->cap = new_cap;
            *stream->buf_ptr = new_buf;
        }
        memcpy(stream->buf + stream->len, ptr, total);
        stream->len += total;
        stream->buf[stream->len] = '\0';
        *stream->size_ptr = stream->len;
        return nmemb;
    } else {
        ssize_t r = write(stream->fd, ptr, total);
        if (r < 0) return 0;
        return (size_t)r / size;
    }
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (stream->is_memstream) return 0;
    ssize_t r = read(stream->fd, ptr, size * nmemb);
    if (r < 0) return 0;
    return (size_t)r / size;
}

int fflush(FILE *stream) {
    if (stream->is_memstream) {
        if (stream->buf) {
            stream->buf[stream->len] = '\0';
            *stream->buf_ptr = stream->buf;
            *stream->size_ptr = stream->len;
        }
    }
    return 0;
}

FILE *open_memstream(char **ptr, size_t *sizeloc) {
    FILE *f = calloc(1, sizeof(FILE));
    if (!f) return NULL;
    f->fd = -1;
    f->buf_ptr = ptr;
    f->size_ptr = sizeloc;
    f->cap = 128;
    f->buf = malloc(f->cap);
    if (!f->buf) {
        free(f);
        return NULL;
    }
    f->buf[0] = '\0';
    f->len = 0;
    f->is_memstream = 1;
    *ptr = f->buf;
    *sizeloc = 0;
    return f;
}

char *getenv(const char *name) {
    if (strcmp(name, "WAYLAND_DISPLAY") == 0) return "wayland-0";
    if (strcmp(name, "XKB_DEFAULT_LAYOUT") == 0) return "us";
    return NULL;
}
int setenv(const char *name, const char *value, int overwrite) { (void)name; (void)value; (void)overwrite; return 0; }
int unsetenv(const char *name) { (void)name; return 0; }

/* ---- pthread and signal stubs ------------------------------------ */
int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine)(void *), void *arg) {
    (void)attr; (void)start_routine; (void)arg;
    /* Create single-threaded runtime identifier */
    *thread = 1;
    return 0;
}
int pthread_join(pthread_t thread, void **retval) { (void)thread; if (retval) *retval = NULL; return 0; }
int pthread_detach(pthread_t thread) { (void)thread; return 0; }
pthread_t pthread_self(void) { return 1; }
int pthread_equal(pthread_t t1, pthread_t t2) { return t1 == t2; }
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) { (void)attr; mutex->locked = 0; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *mutex) { (void)mutex; return 0; }
int pthread_mutex_lock(pthread_mutex_t *mutex) { mutex->locked = 1; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *mutex) { mutex->locked = 0; return 0; }
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) { (void)cond; (void)attr; return 0; }
int pthread_cond_destroy(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) { (void)cond; (void)mutex; return 0; }
int pthread_cond_signal(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_cond_broadcast(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) { (void)destructor; *key = 1; return 0; }
int pthread_key_delete(pthread_key_t key) { (void)key; return 0; }
int pthread_setspecific(pthread_key_t key, const void *value) { (void)key; (void)value; return 0; }
void *pthread_getspecific(pthread_key_t key) { (void)key; return NULL; }

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) { (void)signum; (void)act; (void)oldact; return 0; }
int sigemptyset(sigset_t *set) { memset(set, 0, sizeof(*set)); return 0; }
int sigfillset(sigset_t *set) { memset(set, 0xFF, sizeof(*set)); return 0; }
int sigaddset(sigset_t *set, int signum) { (void)set; (void)signum; return 0; }
int sigdelset(sigset_t *set, int signum) { (void)set; (void)signum; return 0; }
int sigismember(const sigset_t *set, int signum) { (void)set; (void)signum; return 0; }
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) { (void)how; (void)set; (void)oldset; return 0; }
int kill(pid_t pid, int sig) { (void)pid; (void)sig; return 0; }

unsigned int sleep(unsigned int seconds) {
    syscall_raw(103, seconds * 1000, 0, 0, 0, 0, 0); /* task sleep in ms */
    return 0;
}
int usleep(useconds_t usec) {
    syscall_raw(103, usec / 1000, 0, 0, 0, 0, 0);
    return 0;
}

/* ---- Math Functions ---------------------------------------------- */
#undef floor
#undef sqrt
double floor(double x) {
    if (x >= 0.0) {
        return (double)(long long)x;
    } else {
        double r = (double)(long long)x;
        if (r == x) return r;
        return r - 1.0;
    }
}
double sqrt(double x) {
    if (x < 0.0) return 0.0;
    if (x == 0.0) return 0.0;
    double val = x;
    double last;
    do {
        last = val;
        val = (val + x / val) * 0.5;
    } while (val != last);
    return val;
}
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    double diff = y - (1.0 / 3.0);
    if (diff < 0) diff = -diff;
    if (diff < 1e-4) {
        double z = x > 0.0 ? x : -x;
        double val = z;
        for (int i = 0; i < 20; i++) {
            val = (2.0 * val + z / (val * val)) / 3.0;
        }
        return x > 0.0 ? val : -val;
    }
    double fl = __builtin_floor(y);
    if (fl == y) {
        double res = 1.0;
        long long n = (long long)y;
        double base = x;
        if (n < 0) {
            base = 1.0 / base;
            n = -n;
        }
        while (n > 0) {
            if (n & 1) res *= base;
            base *= base;
            n >>= 1;
        }
        return res;
    }
    return x;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double temp = __builtin_floor(x / y);
    return x - temp * y;
}

double cos(double x) {
    double temp = fmod(x + M_PI, 2.0 * M_PI);
    if (temp < 0.0) temp += 2.0 * M_PI;
    temp -= M_PI;
    double xx = temp * temp;
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i <= 10; i++) {
        term *= -xx / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

double sin(double x) {
    return cos(x - M_PI / 2.0);
}

double acos(double x) {
    if (x >= 1.0) return 0.0;
    if (x <= -1.0) return M_PI;
    double y = (1.0 - x) * (M_PI / 2.0);
    for (int i = 0; i < 15; i++) {
        double cy = cos(y);
        double sy = __builtin_sqrt(1.0 - cy * cy);
        if (sy < 1e-9) break;
        y = y + (cy - x) / sy;
    }
    return y;
}

double tan(double x) {
    return sin(x) / cos(x);
}

double round(double x) {
    return __builtin_floor(x + 0.5);
}

double atan2(double y, double x) {
    double r = __builtin_sqrt(x * x + y * y);
    if (r == 0.0) return 0.0;
    double theta = acos(x / r);
    if (y < 0.0) theta = -theta;
    return theta;
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    unsigned long val = 0;
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') { s++; base = 16; }
            else { base = 8; }
        } else { base = 10; }
    } else if (base == 16) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    }
    while (1) {
        int c = *s;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return neg ? -(long)val : (long)val;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    unsigned long val = 0;
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') { s++; base = 16; }
            else { base = 8; }
        } else { base = 10; }
    } else if (base == 16) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    }
    while (1) {
        int c = *s;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return neg ? -val : val;
}

int vasprintf(char **strp, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0) return -1;
    char *str = malloc(len + 1);
    if (!str) return -1;
    int len2 = vsnprintf(str, len + 1, fmt, ap);
    if (len2 < 0) {
        free(str);
        return -1;
    }
    *strp = str;
    return len2;
}

int asprintf(char **strp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vasprintf(strp, fmt, ap);
    va_end(ap);
    return len;
}

int fileno(FILE *stream) {
    return stream->fd;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    FILE *f = calloc(1, sizeof(FILE));
    if (f) {
        f->fd = fd;
    }
    return f;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}

FILE *fopen(const char *pathname, const char *mode) {
    int flags = O_RDONLY;
    if (strchr(mode, 'w')) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strchr(mode, 'a')) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    int fd = open(pathname, flags, 0666);
    if (fd < 0) return NULL;
    FILE *f = calloc(1, sizeof(FILE));
    if (f) {
        f->fd = fd;
    } else {
        close(fd);
    }
    return f;
}

int fclose(FILE *stream) {
    int fd = stream->fd;
    if (stream->is_memstream) {
        fflush(stream);
        free(stream);
        return 0;
    }
    free(stream);
    return close(fd) == 0 ? 0 : EOF;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        if (strchr(accept, *s)) return (char *)s;
        s++;
    }
    return NULL;
}

double strtod(const char *nptr, char **endptr) {
    const char *p = nptr;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    double sign = 1.0;
    if (*p == '-') {
        sign = -1.0;
        p++;
    } else if (*p == '+') {
        p++;
    }
    double val = 0.0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10.0 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        double div = 10.0;
        while (*p >= '0' && *p <= '9') {
            val += (*p - '0') / div;
            div *= 10.0;
            p++;
        }
    }
    if (endptr) *endptr = (char *)p;
    return sign * val;
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

/* ---- timerfd and signalfd userland emulation functions ---- */

static void *timerfd_thread(void *arg) {
    struct timerfd_info *t = arg;
    pthread_mutex_lock(&t->lock);
    while (!t->stop) {
        if (!t->active) {
            pthread_cond_wait(&t->cond, &t->lock);
            continue;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > t->deadline.tv_sec ||
            (now.tv_sec == t->deadline.tv_sec && now.tv_nsec >= t->deadline.tv_nsec)) {
            uint64_t ticks = 1;
            write(t->write_fd, &ticks, sizeof(ticks));
            t->active = 0;
            continue;
        }
        pthread_mutex_unlock(&t->lock);
        
        struct timespec req;
        req.tv_sec = 0;
        req.tv_nsec = 10000000; // 10ms
        nanosleep(&req, NULL);
        
        pthread_mutex_lock(&t->lock);
    }
    pthread_mutex_unlock(&t->lock);
    return NULL;
}

int timerfd_create(int clockid, int flags) {
    (void)clockid; (void)flags;
    int fds[2];
    if (pipe(fds) < 0) return -1;
    
    struct timerfd_info *t = calloc(1, sizeof(struct timerfd_info));
    if (!t) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    t->read_fd = fds[0];
    t->write_fd = fds[1];
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);
    
    // Find a slot
    int slot = -1;
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (!timerfds[i]) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        free(t);
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    timerfds[slot] = t;
    
    pthread_create(&t->thread, NULL, timerfd_thread, t);
    return fds[0];
}

int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value) {
    (void)flags; (void)old_value;
    struct timerfd_info *t = NULL;
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (timerfds[i] && timerfds[i]->read_fd == fd) {
            t = timerfds[i];
            break;
        }
    }
    if (!t) return -1;
    
    pthread_mutex_lock(&t->lock);
    if (new_value->it_value.tv_sec == 0 && new_value->it_value.tv_nsec == 0) {
        t->active = 0;
    } else {
        if (flags & TFD_TIMER_ABSTIME) {
            t->deadline = new_value->it_value;
        } else {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            t->deadline.tv_sec = now.tv_sec + new_value->it_value.tv_sec;
            t->deadline.tv_nsec = now.tv_nsec + new_value->it_value.tv_nsec;
            if (t->deadline.tv_nsec >= 1000000000L) {
                t->deadline.tv_nsec -= 1000000000L;
                t->deadline.tv_sec++;
            }
        }
        t->active = 1;
        pthread_cond_signal(&t->cond);
    }
    pthread_mutex_unlock(&t->lock);
    return 0;
}


static void signalfd_handler(int sig) {
    struct signalfd_siginfo si = {0};
    si.ssi_signo = (uint32_t)sig;
    for (int i = 0; i < MAX_SIGNALFDS; i++) {
        if (signalfds[i]) {
            if (sigismember(&signalfds[i]->mask, sig)) {
                write(signalfds[i]->write_fd, &si, sizeof(si));
            }
        }
    }
}

int signalfd(int fd, const sigset_t *mask, int flags) {
    (void)flags;
    struct signalfd_info *s = NULL;
    if (fd != -1) {
        for (int i = 0; i < MAX_SIGNALFDS; i++) {
            if (signalfds[i] && signalfds[i]->read_fd == fd) {
                s = signalfds[i];
                break;
            }
        }
        if (!s) return -1;
    } else {
        int fds[2];
        if (pipe(fds) < 0) return -1;
        s = calloc(1, sizeof(struct signalfd_info));
        if (!s) {
            close(fds[0]);
            close(fds[1]);
            return -1;
        }
        s->read_fd = fds[0];
        s->write_fd = fds[1];
        
        int slot = -1;
        for (int i = 0; i < MAX_SIGNALFDS; i++) {
            if (!signalfds[i]) {
                slot = i;
                break;
            }
        }
        if (slot == -1) {
            free(s);
            close(fds[0]);
            close(fds[1]);
            return -1;
        }
        signalfds[slot] = s;
    }
    
    s->mask = *mask;
    
    // Register signal handler for all signals in the mask
    for (int sig = 1; sig < 32; sig++) {
        if (sigismember(mask, sig)) {
            struct sigaction sa = {0};
            sa.sa_handler = signalfd_handler;
            sa.sa_flags = SA_RESTART;
            sigaction(sig, &sa, NULL);
        }
    }
    return s->read_fd;
}

