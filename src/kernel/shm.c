#include "shm.h"
#include "pmm.h"
#include "string.h"
#include "errno.h"
#include "heap.h"
#include "printf.h"

static ShmObject shm_objs[SHM_MAX_OBJECTS];

void shm_init(void)
{
    memset(shm_objs, 0, sizeof(shm_objs));
}

int shm_open_object(const char *name, int flags)
{
    (void)flags;
    for (int i = 0; i < SHM_MAX_OBJECTS; i++) {
        if (shm_objs[i].used && strcmp(shm_objs[i].name, name) == 0) {
            return i;
        }
    }

    for (int i = 0; i < SHM_MAX_OBJECTS; i++) {
        if (!shm_objs[i].used) {
            memset(&shm_objs[i], 0, sizeof(ShmObject));
            strncpy(shm_objs[i].name, name, SHM_NAME_MAX - 1);
            shm_objs[i].used = true;
            shm_objs[i].size = 0;
            return i;
        }
    }
    return -ENOMEM;
}

int shm_truncate(int idx, size_t length)
{
    if (idx < 0 || idx >= SHM_MAX_OBJECTS || !shm_objs[idx].used)
        return -EINVAL;

    ShmObject *shm = &shm_objs[idx];
    size_t old_pages = (shm->size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t new_pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;

    if (new_pages > SHM_MAX_PAGES)
        return -EFBIG;

    for (size_t i = old_pages; i < new_pages; i++) {
        void *frame = pmm_alloc();
        if (!frame) {
            for (size_t j = old_pages; j < i; j++) {
                pmm_free((void *)shm->pages[j]);
                shm->pages[j] = 0;
            }
            return -ENOMEM;
        }
        memset(frame, 0, PAGE_SIZE);
        shm->pages[i] = (uint64_t)frame;
    }

    for (size_t i = new_pages; i < old_pages; i++) {
        if (shm->pages[i]) {
            pmm_free((void *)shm->pages[i]);
            shm->pages[i] = 0;
        }
    }

    shm->size = length;
    return 0;
}

int shm_read(int idx, void *buf, size_t len, size_t *off)
{
    if (idx < 0 || idx >= SHM_MAX_OBJECTS || !shm_objs[idx].used)
        return -EINVAL;

    ShmObject *shm = &shm_objs[idx];
    if (*off >= shm->size)
        return 0;

    size_t avail = shm->size - *off;
    size_t to_read = (len < avail) ? len : avail;

    size_t curr_off = *off;
    size_t copied = 0;

    while (copied < to_read) {
        size_t page_idx = curr_off / PAGE_SIZE;
        size_t page_off = curr_off % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - page_off;
        if (chunk > to_read - copied)
            chunk = to_read - copied;

        uint64_t phys_page = shm->pages[page_idx];
        if (!phys_page)
            return -EFAULT;

        memcpy((char *)buf + copied, (const char *)(phys_page + page_off), chunk);
        copied += chunk;
        curr_off += chunk;
    }

    *off = curr_off;
    return (int)copied;
}

int shm_write(int idx, const void *buf, size_t len, size_t *off)
{
    if (idx < 0 || idx >= SHM_MAX_OBJECTS || !shm_objs[idx].used)
        return -EINVAL;

    ShmObject *shm = &shm_objs[idx];
    size_t new_end = *off + len;
    if (new_end > shm->size) {
        int r = shm_truncate(idx, new_end);
        if (r < 0) return r;
    }

    size_t curr_off = *off;
    size_t written = 0;

    while (written < len) {
        size_t page_idx = curr_off / PAGE_SIZE;
        size_t page_off = curr_off % PAGE_SIZE;
        size_t chunk = PAGE_SIZE - page_off;
        if (chunk > len - written)
            chunk = len - written;

        uint64_t phys_page = shm->pages[page_idx];
        if (!phys_page)
            return -EFAULT;

        memcpy((char *)(phys_page + page_off), (const char *)buf + written, chunk);
        written += chunk;
        curr_off += chunk;
    }

    *off = curr_off;
    return (int)written;
}

uint64_t shm_get_page_phys(int idx, size_t page_offset)
{
    if (idx < 0 || idx >= SHM_MAX_OBJECTS || !shm_objs[idx].used)
        return 0;

    ShmObject *shm = &shm_objs[idx];
    size_t page_idx = page_offset / PAGE_SIZE;
    if (page_idx >= SHM_MAX_PAGES)
        return 0;

    return shm->pages[page_idx];
}

void shm_destroy(int idx)
{
    if (idx < 0 || idx >= SHM_MAX_OBJECTS || !shm_objs[idx].used)
        return;

    ShmObject *shm = &shm_objs[idx];
    size_t pages = (shm->size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        if (shm->pages[i]) {
            pmm_free((void *)shm->pages[i]);
            shm->pages[i] = 0;
        }
    }
    shm->used = false;
}
