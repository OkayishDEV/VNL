#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H

#include <time.h>

#define TFD_TIMER_ABSTIME 1
#define TFD_CLOEXEC 02000000
#define TFD_NONBLOCK 00004000

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

int timerfd_create(int clockid, int flags);
int timerfd_settime(int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);

#endif /* _SYS_TIMERFD_H */
