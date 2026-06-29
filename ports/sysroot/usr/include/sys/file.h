#ifndef _SYS_FILE_H
#define _SYS_FILE_H

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

static inline int flock(int fd, int operation) {
    (void)fd; (void)operation;
    return 0;
}

#endif /* _SYS_FILE_H */
