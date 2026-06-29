#pragma once
#include "types.h"

#define SHM_MAX_OBJECTS 32
#define SHM_NAME_MAX    64
#define SHM_MAX_PAGES   1024

typedef struct {
    bool     used;
    char     name[SHM_NAME_MAX];
    uint64_t pages[SHM_MAX_PAGES];
    size_t   size;
} ShmObject;

void shm_init(void);
int  shm_open_object(const char *name, int flags);
int  shm_truncate(int idx, size_t length);
int  shm_read(int idx, void *buf, size_t len, size_t *off);
int  shm_write(int idx, const void *buf, size_t len, size_t *off);
uint64_t shm_get_page_phys(int idx, size_t page_offset);
void shm_destroy(int idx);
