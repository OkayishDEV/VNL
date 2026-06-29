#pragma once
#include "types.h"
#include "idt.h"

/* Linux x86_64 ABI System Call Numbers */
#define SYS_READ         0
#define SYS_WRITE        1
#define SYS_OPEN         2
#define SYS_CLOSE        3
#define SYS_STAT         4
#define SYS_FSTAT        5
#define SYS_LSTAT        6
#define SYS_LSEEK        8
#define SYS_MMAP         9
#define SYS_MPROTECT    10
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_RT_SIGACTION 13
#define SYS_RT_SIGPROCMASK 14
#define SYS_IOCTL       16
#define SYS_PIPE        22
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_GETPID      39
#define SYS_SOCKET      41
#define SYS_CONNECT     42
#define SYS_ACCEPT      43
#define SYS_SENDTO      44
#define SYS_RECVFROM    45
#define SYS_SENDMSG     46
#define SYS_RECVMSG     47
#define SYS_BIND        49
#define SYS_LISTEN      50
#define SYS_SOCKETPAIR  53
#define SYS_CLONE       56
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_UNAME       63
#define SYS_FCNTL       72
#define SYS_FTRUNCATE   77
#define SYS_GETCWD      79
#define SYS_MKDIR       83
#define SYS_UNLINK      87
#define SYS_READLINK    89
#define SYS_GETUID     102
#define SYS_GETGID     104
#define SYS_SETUID     105
#define SYS_SETGID     106
#define SYS_GETEUID    107
#define SYS_GETEGID    108
#define SYS_ARCH_PRCTL 158
#define SYS_FUTEX      202
#define SYS_GETDENTS64 217
#define SYS_SET_TID_ADDRESS 218
#define SYS_EXIT_GROUP 231
#define SYS_EPOLL_WAIT 232
#define SYS_EPOLL_CTL  233
#define SYS_EVENTFD2   290
#define SYS_EPOLL_CREATE1 291
#define SYS_PRLIMIT64  302

/* VNL Custom System Calls */
#define SYS_UPTIME      100
#define SYS_TASK_CREATE 101
#define SYS_TASK_SLEEP  103

#define VNL_MAP_SHARED     0x01
#define VNL_MAP_PRIVATE    0x02
#define VNL_MAP_FIXED      0x10
#define VNL_MAP_ANONYMOUS  0x20

struct linux_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    uint64_t st_atime_sec;
    uint64_t st_atime_nsec;
    uint64_t st_mtime_sec;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime_sec;
    uint64_t st_ctime_nsec;
};

void syscall_init(void);
