#pragma once
#include <sys/types.h>

typedef long clock_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

time_t time(time_t *tloc);
clock_t clock(void);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
char *asctime(const struct tm *tm);
char *ctime(const time_t *timer);
int nanosleep(const struct timespec *req, struct timespec *rem);

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

struct timeval;
int gettimeofday(struct timeval *tv, void *tz);

static inline int clock_gettime(int clock_id, struct timespec *tp) {
    (void)clock_id;
    struct timeval {
        time_t tv_sec;
        long tv_usec;
    } tv;
    gettimeofday((void *)&tv, (void *)0);
    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = tv.tv_usec * 1000;
    return 0;
}
