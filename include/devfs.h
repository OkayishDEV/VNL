#pragma once
#include "types.h"
#include "vfs.h"

#define DEV_FB_MAJOR  29
#define DEV_FB0_MINOR 0
#define DEV_NULL_MAJOR 1
#define DEV_NULL_MINOR 3
#define DEV_SHM_MAJOR 24
#define DEV_KEYBOARD_MAJOR 11
#define DEV_MOUSE_MAJOR    12
#define DEV_SERIAL_MAJOR   4

void devfs_init(void);
int  devfs_chr_read(VFSNode *n, void *buf, size_t len, size_t *off);
int  devfs_chr_write(VFSNode *n, const void *buf, size_t len, size_t *off);
int  devfs_chr_ioctl(VFSNode *n, uint64_t request, void *arg);
