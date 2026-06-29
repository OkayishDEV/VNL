/* 
 * junk numbers i stole from somewhere... what the fuck was i thinking? 
 * most of this is broken anyway. 
 */
#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "printf.h"
#include "timer.h"
#include "string.h"
#include "vfs.h"
#include "sched.h"
#include "cpu.h"
#include "keyboard.h"
#include "errno.h"
#include "vmm.h"
#include "devfs.h"
#include "fb.h"
#include "unix_socket.h"
#include "heap.h"
#include "elf_load.h"
#include "uaccess.h"
#include "uspace.h"
#include "pmm.h"
#include "shm.h"

static const char *uname_str = "VNL 0.2.0 (Vibe Not Linux) x86_64";

#ifndef ARCH_SET_FS
#define ARCH_SET_FS 0x1002
#endif
#define MSR_FS_BASE 0xC0000100

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

#define EPOLL_FD_BASE 200
#define MAX_EPOLLS 16
#define MAX_EPOLL_FDS 32

typedef struct {
    int fd;
    struct epoll_event event;
} EpollEventEntry;

typedef struct {
    bool used;
    EpollEventEntry fds[MAX_EPOLL_FDS];
    int count;
} EpollInstance;

static EpollInstance epolls[MAX_EPOLLS];

static int epoll_alloc(void) {
    for (int i = 0; i < MAX_EPOLLS; i++) {
        if (!epolls[i].used) {
            epolls[i].used = true;
            epolls[i].count = 0;
            for (int j = 0; j < MAX_EPOLL_FDS; j++) {
                epolls[i].fds[j].fd = -1;
            }
            return i;
        }
    }
    return -1;
}

static int sys_epoll_create1(int flags) {
    (void)flags;
    int idx = epoll_alloc();
    if (idx < 0) return -ENOMEM;
    return EPOLL_FD_BASE + idx;
}

static int sys_epoll_ctl(int epfd, int op, int fd, uint64_t event_ptr) {
    int idx = epfd - EPOLL_FD_BASE;
    if (idx < 0 || idx >= MAX_EPOLLS || !epolls[idx].used) return -EBADF;
    EpollInstance *ep = &epolls[idx];

    if (op == 2) { // EPOLL_CTL_DEL
        for (int i = 0; i < ep->count; i++) {
            if (ep->fds[i].fd == fd) {
                for (int j = i; j < ep->count - 1; j++) {
                    ep->fds[j] = ep->fds[j + 1];
                }
                ep->count--;
                return 0;
            }
        }
        return -ENOENT;
    }

    struct epoll_event ev;
    memcpy(&ev, (const void *)event_ptr, sizeof(ev));

    if (op == 1) { // EPOLL_CTL_ADD
        if (ep->count >= MAX_EPOLL_FDS) return -ENOMEM;
        for (int i = 0; i < ep->count; i++) {
            if (ep->fds[i].fd == fd) return -EEXIST;
        }
        ep->fds[ep->count].fd = fd;
        ep->fds[ep->count].event = ev;
        ep->count++;
        return 0;
    } else if (op == 3) { // EPOLL_CTL_MOD
        for (int i = 0; i < ep->count; i++) {
            if (ep->fds[i].fd == fd) {
                ep->fds[i].event = ev;
                return 0;
            }
        }
        return -ENOENT;
    }

    return -EINVAL;
}

static int sys_epoll_wait(int epfd, uint64_t events_ptr, int maxevents, int timeout) {
    int idx = epfd - EPOLL_FD_BASE;
    if (idx < 0 || idx >= MAX_EPOLLS || !epolls[idx].used) return -EBADF;
    EpollInstance *ep = &epolls[idx];

    int ready_count = 0;
    int loops = 0;
    int max_loops = (timeout < 0) ? -1 : timeout;

    static struct epoll_event out_events[MAX_EPOLL_FDS];

    while (1) {
        ready_count = 0;
        for (int i = 0; i < ep->count; i++) {
            int fd = ep->fds[i].fd;
            uint32_t req_events = ep->fds[i].event.events;
            uint32_t revents = 0;

            if (unix_is_sockfd(fd)) {
                revents = unix_sock_poll(fd, req_events);
            } else {
                revents = req_events & (0x001 | 0x004);
            }

            if (revents) {
                out_events[ready_count].events = revents;
                out_events[ready_count].data = ep->fds[i].event.data;
                ready_count++;
                if (ready_count >= maxevents) break;
            }
        }

        if (ready_count > 0 || timeout == 0) break;
        if (max_loops >= 0 && loops >= max_loops) break;

        task_sleep(1);
        loops++;
    }

    if (ready_count > 0) {
        Task *t = sched_current();
        vnl_copy_to_user_va(t->cr3_phys, events_ptr, out_events, ready_count * sizeof(struct epoll_event));
    }

    return ready_count;
}

static void syscall_handler(Registers *r)
{
    uint64_t num  = r->rax;
    uint64_t arg1 = r->rdi;
    uint64_t arg2 = r->rsi;
    uint64_t arg3 = r->rdx;
    int64_t  ret  = -ENOSYS;

    switch (num) {
    case SYS_WRITE: {
        int fd         = (int)arg1;
        const char *buf = (const char *)arg2;
        size_t len     = (size_t)arg3;
        if (fd == 1 || fd == 2) {
            for (size_t i = 0; i < len; i++) vga_putchar(buf[i]);
        }
        if (unix_is_sockfd(fd)) {
            ret = unix_sock_write(fd, buf, len);
        } else {
            ret = vfs_write(fd, buf, len);
        }
        break;
    }
    case SYS_READ: {
        int fd = (int)arg1;
        void *buf = (void *)arg2;
        size_t len = (size_t)arg3;
        if (unix_is_sockfd(fd)) {
            ret = unix_sock_read(fd, buf, len);
        } else {
            ret = vfs_read(fd, buf, len);
            if (ret <= 0 && fd == 0) {
                char c = keyboard_getchar();
                *(char *)buf = c;
                ret = 1;
            }
        }
        break;
    }
    case SYS_OPEN:
        ret = vfs_open((const char *)arg1, (int)arg2);
        break;
    case SYS_CLOSE: {
        int fd = (int)arg1;
        if (unix_is_sockfd(fd)) ret = unix_sock_close(fd);
        else ret = vfs_close(fd);
        break;
    }
    case SYS_IOCTL:
        ret = vfs_ioctl((int)arg1, arg2, (void *)arg3);
        break;
    case SYS_BRK: {
        Task *t = sched_current();
        uint64_t nb = arg1;
        if (t->brk_start == 0 && t->brk_end == 0) {
            ret = (nb == 0) ? 0 : (int64_t)-ENOMEM;
            break;
        }
        if (nb == 0) {
            ret = (int64_t)t->brk_end;
            break;
        }
        if (nb < t->brk_start) {
            ret = (int64_t)t->brk_end;
            break;
        }
        uint64_t old_pg = ALIGN_UP(t->brk_end, PAGE_SIZE);
        uint64_t new_pg = ALIGN_UP(nb, PAGE_SIZE);
        for (uint64_t v = old_pg; v < new_pg; v += PAGE_SIZE) {
            void *pg = pmm_alloc();
            if (!pg) {
                ret = (int64_t)t->brk_end;
                goto brk_done;
            }
            memset(pg, 0, PAGE_SIZE);
            vmm_map_in_pml4(t->cr3_phys, v, (uint64_t)pg,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
        t->brk_end = nb;
        ret = (int64_t)nb;
    brk_done:
        break;
    }
    case SYS_MMAP: {
        void  *addr = (void *)arg1;
        size_t len  = (size_t)arg2;
        int    prot = (int)arg3;
        int    flags = (int)r->r10;
        int    fd    = (int)r->r8;
        int64_t off  = (int64_t)r->r9;
        (void)prot;

        Task *t = sched_current();
        if (len == 0 || len > (64u * 1024u * 1024u)) {
            ret = -EINVAL;
            break;
        }
        size_t map_len = ALIGN_UP(len, PAGE_SIZE);
        uint64_t va;
        if (flags & VNL_MAP_FIXED)
            va = (uint64_t)addr & ~(PAGE_SIZE - 1ULL);
        else {
            va = t->mmap_next;
            t->mmap_next += map_len;
        }

        if (flags & VNL_MAP_ANONYMOUS) {
            if (off != 0) {
                ret = -EINVAL;
                break;
            }
            uint64_t cr3p = t->cr3_phys;
            size_t npages = map_len / PAGE_SIZE;
            int64_t aret = (int64_t)va;
            for (size_t i = 0; i < npages; i++) {
                void *pg = pmm_alloc();
                if (!pg) {
                    aret = -ENOMEM;
                    break;
                }
                memset(pg, 0, PAGE_SIZE);
                vmm_map_in_pml4(cr3p, va + i * PAGE_SIZE, (uint64_t)pg,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            }
            ret = aret;
            break;
        }

        if (fd < 0) {
            ret = -EBADF;
            break;
        }
        VFSNode *fn = vfs_node_from_fd(fd);
        if (!fn || fn->type != VFS_CHR) {
            ret = -ENODEV;
            break;
        }
        if (fn->dev_major == DEV_FB_MAJOR) {
            uint64_t p0, ln;
            uint32_t ll, w, h, bpp;
            if (!fb_get_mmap_region(&p0, &ln, &ll, &w, &h, &bpp)) {
                ret = -ENODEV;
                break;
            }
            if (off != 0) {
                ret = -EINVAL;
                break;
            }
            uint64_t map_rest = ln;
            size_t file_map = len;
            if (file_map > map_rest) file_map = (size_t)map_rest;

            uint64_t cr3p = t->cr3_phys;
            size_t npages = ALIGN_UP(file_map, PAGE_SIZE) / PAGE_SIZE;
            for (size_t i = 0; i < npages; i++)
                vmm_map_in_pml4(cr3p, va + i * PAGE_SIZE, p0 + i * PAGE_SIZE,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            ret = (int64_t)va;
        } else if (fn->dev_major == DEV_SHM_MAJOR) {
            size_t npages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
            uint64_t cr3p = t->cr3_phys;
            int64_t aret = (int64_t)va;
            for (size_t i = 0; i < npages; i++) {
                uint64_t phys = shm_get_page_phys(fn->dev_minor, off + i * PAGE_SIZE);
                if (!phys) {
                    aret = -EINVAL;
                    break;
                }
                vmm_map_in_pml4(cr3p, va + i * PAGE_SIZE, phys,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            }
            ret = aret;
        } else {
            ret = -ENODEV;
        }
        break;
    }
    case SYS_MUNMAP:
        vmm_unmap_range_pml4(sched_current()->cr3_phys, arg1, (size_t)arg2);
        ret = 0;
        break;
    case SYS_MKDIR:
        ret = vfs_mkdir((const char *)arg1);
        break;
    case SYS_UNLINK:
        ret = vfs_unlink((const char *)arg1);
        break;
    case SYS_GETPID:
        ret = (int64_t)sched_current()->pid;
        break;
    case SYS_UNAME: {
        char *buf = (char *)arg1;
        if (buf) {
            strncpy(buf, uname_str, 128);
            ret = 0;
        }
        break;
    }
    case SYS_UPTIME:
        ret = (int64_t)timer_ticks();
        break;
    case SYS_EXIT:
        task_exit();
        /* should never reach here... unless it's haunted */
        __builtin_unreachable();
    case SYS_TASK_CREATE: {
        void (*fn)(void) = (void (*)(void))arg1;
        Task *curr_t = sched_current();
        char name[32];
        if (r->cs == 0x1B) {
            if (copy_user_cstring(curr_t->cr3_phys, (const void *)arg2, name, sizeof(name)) < 0) {
                ret = -EFAULT;
                break;
            }
            /* Allocate a userspace stack for the new thread */
            uint64_t stack_sz = 32 * 1024;
            uint64_t va_base = curr_t->mmap_next;
            curr_t->mmap_next += stack_sz;
            bool failed = false;
            for (uint64_t offset = 0; offset < stack_sz; offset += PAGE_SIZE) {
                void *pg = pmm_alloc();
                if (!pg) {
                    failed = true;
                    break;
                }
                memset(pg, 0, PAGE_SIZE);
                vmm_map_in_pml4(curr_t->cr3_phys, va_base + offset, (uint64_t)pg,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
            }
            if (failed) {
                ret = -ENOMEM;
                break;
            }
            uint64_t ustack_top = va_base + stack_sz;
            ret = task_create_user(name, curr_t->cr3_phys, (uint64_t)fn, ustack_top - 16,
                                   curr_t->brk_start, curr_t->brk_end);
        } else {
            ret = task_create((const char *)arg2, fn);
        }
        break;
    }
    case SYS_TASK_SLEEP:
        task_sleep(arg1);
        ret = 0;
        break;
    case SYS_SOCKET:
        ret = unix_socket((int)arg1, (int)arg2, (int)arg3);
        break;
    case SYS_SOCKETPAIR: {
        int sv_local[2];
        ret = unix_socketpair((int)arg1, (int)arg2, (int)arg3, sv_local);
        if (ret == 0) {
            Task *t = sched_current();
            vnl_copy_to_user_va(t->cr3_phys, r->r10, sv_local, sizeof(sv_local));
        }
        break;
    }
    case SYS_BIND:
        ret = unix_bind((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_LISTEN:
        ret = unix_listen((int)arg1, (int)arg2);
        break;
    case SYS_ACCEPT:
        ret = unix_accept((int)arg1, (void *)arg2, (size_t *)arg3);
        break;
    case SYS_CONNECT:
        ret = unix_connect((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_SENDTO:
        if (r->r8 != 0)
            ret = -ENOSYS;
        else if (unix_is_sockfd((int)arg1))
            ret = unix_sock_write((int)arg1, (void *)arg2, (size_t)arg3);
        else
            ret = vfs_write((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_RECVFROM:
        if (r->r8 != 0)
            ret = -ENOSYS;
        else if (unix_is_sockfd((int)arg1))
            ret = unix_sock_read((int)arg1, (void *)arg2, (size_t)arg3);
        else
            ret = vfs_read((int)arg1, (void *)arg2, (size_t)arg3);
        break;
    case SYS_SENDMSG:
        if (unix_is_sockfd((int)arg1))
            ret = unix_sock_sendmsg((int)arg1, (const struct msghdr *)arg2, (int)arg3);
        else
            ret = -ENOTSOCK;
        break;
    case SYS_RECVMSG:
        if (unix_is_sockfd((int)arg1))
            ret = unix_sock_recvmsg((int)arg1, (struct msghdr *)arg2, (int)arg3);
        else
            ret = -ENOTSOCK;
        break;
    case SYS_FTRUNCATE:
        ret = vfs_ftruncate((int)arg1, (int64_t)arg2);
        break;
    case SYS_EXECVE: {
        if (r->cs != 0x1B) {
            ret = -EPERM;
            break;
        }
        Task *t = sched_current();
        char path[256];
        if (copy_user_cstring(t->cr3_phys, (const void *)arg1, path, sizeof path) < 0) {
            ret = -EFAULT;
            break;
        }
        int fd = vfs_open(path, VFS_O_READ);
        if (fd < 0) {
            ret = -ENOENT;
            break;
        }
        char *buf = (char *)kmalloc(512 * 1024);
        if (!buf) {
            vfs_close(fd);
            ret = -ENOMEM;
            break;
        }
        int n = vfs_read(fd, buf, 512 * 1024);
        vfs_close(fd);
        if (n < 64) {
            kfree(buf);
            ret = -ENOEXEC;
            break;
        }
        uint64_t new_pml4 = uspace_create_pml4();
        if (!new_pml4) {
            kfree(buf);
            ret = -ENOMEM;
            break;
        }
        uint64_t entry, brk;
        if (elf_load_exec(new_pml4, buf, (size_t)n, &entry, &brk) < 0) {
            kfree(buf);
            ret = -ENOEXEC;
            break;
        }
        kfree(buf);
        bool stk_ok = true;
        for (size_t i = 0; i < VNL_USER_STACK_PAGES; i++) {
            void *pg = pmm_alloc();
            if (!pg) {
                stk_ok = false;
                break;
            }
            memset(pg, 0, PAGE_SIZE);
            vmm_map_in_pml4(new_pml4, VNL_USER_STACK_BASE + i * PAGE_SIZE, (uint64_t)pg,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }
        if (!stk_ok) {
            ret = -ENOMEM;
            break;
        }

        uint64_t sp = VNL_USER_STACK_BASE + VNL_USER_STACK_PAGES * PAGE_SIZE - 0x40;
        uint64_t z = 0;
        vnl_copy_to_user_va(new_pml4, sp, &z, 8);
        vnl_copy_to_user_va(new_pml4, sp + 8, &z, 8);

        t->cr3_phys = new_pml4;
        t->brk_start = brk;
        t->brk_end = brk;
        t->mmap_next = 0x40000000ULL;
        write_cr3(new_pml4);

        r->rip = entry;
        r->rsp = sp;
        r->rdi = 0;
        r->rsi = sp + 8;
        ret = 0;
        break;
    }
    case SYS_GETUID:
    case SYS_GETEUID:
        ret = 0;
        break;
    case SYS_SETUID:
        ret = 0;
        break;
    case SYS_ARCH_PRCTL:
        if (arg1 == ARCH_SET_FS)
            wrmsr_q(MSR_FS_BASE, arg2);
        ret = (arg1 == ARCH_SET_FS) ? 0 : (int64_t)-EINVAL;
        break;
    case SYS_SET_TID_ADDRESS:
        ret = (int64_t)sched_current()->pid;
        break;
    case SYS_MPROTECT:
        ret = 0;
        break;
    case SYS_STAT:
    case SYS_LSTAT: {
        char path[256];
        Task *t = sched_current();
        if (copy_user_cstring(t->cr3_phys, (const void *)arg1, path, sizeof(path)) < 0) {
            ret = -EFAULT;
            break;
        }
        VFSNodeType type;
        size_t size;
        int rstat = vfs_stat(path, &type, &size);
        if (rstat < 0) {
            ret = -ENOENT;
            break;
        }
        struct linux_stat st;
        memset(&st, 0, sizeof(st));
        st.st_size = (int64_t)size;
        st.st_blksize = 512;
        st.st_blocks = (int64_t)((size + 511) / 512);
        if (type == VFS_DIR) {
            st.st_mode = 0040000 | 0755;
        } else if (type == VFS_CHR) {
            st.st_mode = 0020000 | 0666;
        } else {
            st.st_mode = 0100000 | 0644;
        }
        vnl_copy_to_user_va(t->cr3_phys, arg2, &st, sizeof(st));
        ret = 0;
        break;
    }
    case SYS_FSTAT: {
        int fd = (int)arg1;
        VFSNode *n = NULL;
        if (fd >= 0 && fd < VFS_MAX_FDS) {
            n = vfs_node_from_fd(fd);
        }
        if (!n) {
            ret = -EBADF;
            break;
        }
        struct linux_stat st;
        memset(&st, 0, sizeof(st));
        st.st_size = (int64_t)n->size;
        st.st_blksize = 512;
        st.st_blocks = (int64_t)((n->size + 511) / 512);
        if (n->type == VFS_DIR) {
            st.st_mode = 0040000 | 0755;
        } else if (n->type == VFS_CHR) {
            st.st_mode = 0020000 | 0666;
        } else {
            st.st_mode = 0100000 | 0644;
        }
        Task *t = sched_current();
        vnl_copy_to_user_va(t->cr3_phys, arg2, &st, sizeof(st));
        ret = 0;
        break;
    }
    case SYS_LSEEK:
        ret = vfs_lseek((int)arg1, (int64_t)arg2, (int)arg3);
        break;
    case SYS_DUP:
        ret = vfs_dup((int)arg1);
        break;
    case SYS_DUP2:
        ret = vfs_dup2((int)arg1, (int)arg2);
        break;
    case SYS_GETCWD: {
        char buf[256];
        if (!vfs_getcwd(buf, sizeof(buf))) {
            ret = -ERANGE;
            break;
        }
        size_t len = strlen(buf) + 1;
        if (len > arg2) {
            ret = -ERANGE;
            break;
        }
        Task *t = sched_current();
        vnl_copy_to_user_va(t->cr3_phys, arg1, buf, len);
        ret = (int64_t)len;
        break;
    }
    case SYS_READLINK:
        ret = -EINVAL;
        break;
    case SYS_GETGID:
    case SYS_SETGID:
    case SYS_GETEGID:
        ret = 0;
        break;
    case SYS_EXIT_GROUP:
        task_exit();
        __builtin_unreachable();
    case SYS_GETDENTS64: {
        int fd = (int)arg1;
        void *dirp = (void *)arg2;
        size_t count = (size_t)arg3;
        
        uint8_t *kbuf = kmalloc(count);
        if (!kbuf) {
            ret = -ENOMEM;
            break;
        }
        memset(kbuf, 0, count);
        
        int r_dents = vfs_getdents64(fd, kbuf, count);
        if (r_dents < 0) {
            kfree(kbuf);
            ret = r_dents;
            break;
        }
        
        Task *t = sched_current();
        vnl_copy_to_user_va(t->cr3_phys, (uint64_t)dirp, kbuf, (size_t)r_dents);
        ret = r_dents;
        kfree(kbuf);
        break;
    }
    /* more crap that isn't finished */
    case SYS_FCNTL: {
        int fd = (int)arg1;
        int cmd = (int)arg2;
        int64_t arg = arg3;
        if (unix_is_sockfd(fd)) {
            ret = unix_sock_fcntl(fd, cmd, arg);
        } else {
            ret = vfs_fcntl(fd, cmd, arg);
        }
        break;
    }
    case SYS_EVENTFD2:
        ret = unix_eventfd((unsigned int)arg1, (int)arg2);
        break;
    case SYS_EPOLL_CREATE1:
        ret = sys_epoll_create1((int)arg1);
        break;
    case SYS_EPOLL_CTL:
        ret = sys_epoll_ctl((int)arg1, (int)arg2, (int)arg3, r->r10);
        break;
    case SYS_EPOLL_WAIT:
        ret = sys_epoll_wait((int)arg1, arg2, (int)arg3, (int)r->r10);
        break;
    case SYS_FORK:
    case SYS_CLONE:
    case SYS_FUTEX:
    case SYS_PIPE:
    case SYS_RT_SIGACTION:
    case SYS_RT_SIGPROCMASK:
    case SYS_PRLIMIT64:
        ret = -ENOSYS;
        break;
    default:
        ret = -ENOSYS;
    }

    r->rax = (uint64_t)ret;
}

void syscall_init(void)
{
    idt_set_handler(0x80, syscall_handler);
    /* allow ring 3 to call this shit */
    idt_set_interrupt_dpl(0x80, 3);
}
