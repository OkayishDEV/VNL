#pragma once
#include <sys/types.h>

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGBUS   7

#define SA_SIGINFO 4
#define SA_NODEFER 0x40000000
#define SA_RESTART 0x10000000

typedef struct {
    int si_signo;
    int si_code;
    int si_errno;
    void *si_addr;
} siginfo_t;

static inline int raise(int sig) {
    (void)sig;
    return 0;
}

typedef struct {
    unsigned long sig[2];
} sigset_t;

struct sigaction {
    sighandler_t sa_handler;
    void       (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int kill(pid_t pid, int sig);
