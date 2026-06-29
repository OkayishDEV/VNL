#ifndef _DLFCN_H
#define _DLFCN_H

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 256
#define RTLD_LOCAL 0

static inline void *dlopen(const char *filename, int flag) {
    (void)filename; (void)flag;
    return (void *)0;
}

static inline int dlclose(void *handle) {
    (void)handle;
    return -1;
}

static inline void *dlsym(void *handle, const char *symbol) {
    (void)handle; (void)symbol;
    return (void *)0;
}

static inline char *dlerror(void) {
    return "Dynamic linking not supported";
}

#endif /* _DLFCN_H */
