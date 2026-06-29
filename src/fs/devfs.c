#include "devfs.h"
#include "vfs.h"
#include "fb.h"
#include "uapi_fb.h"
#include "string.h"
#include "errno.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"

void devfs_init(void)
{
    if (vfs_resolve("/dev") < 0)
        vfs_mkdir("/dev");
    vfs_mkdir("/dev/shm");
    vfs_mkdir("/dev/input");
    vfs_mknod_chr("/dev/fb0", DEV_FB_MAJOR, DEV_FB0_MINOR);
    vfs_mknod_chr("/dev/null", DEV_NULL_MAJOR, DEV_NULL_MINOR);
    vfs_mknod_chr("/dev/input/keyboard", DEV_KEYBOARD_MAJOR, 0);
    vfs_mknod_chr("/dev/input/mouse", DEV_MOUSE_MAJOR, 0);
    vfs_mknod_chr("/dev/ttyS0", DEV_SERIAL_MAJOR, 0);
}

int devfs_chr_read(VFSNode *n, void *buf, size_t len, size_t *off)
{
    if (!n || n->type != VFS_CHR) return -EIO;
    if (n->dev_major == DEV_NULL_MAJOR) return 0;
    if (n->dev_major == DEV_FB_MAJOR) return 0;
    if (n->dev_major == DEV_SHM_MAJOR) {
        extern int shm_read(int idx, void *buf, size_t len, size_t *off);
        return shm_read(n->dev_minor, buf, len, off);
    }
    if (n->dev_major == DEV_KEYBOARD_MAJOR) {
        int count = 0;
        uint8_t *ubuf = (uint8_t *)buf;
        while ((size_t)count < len) {
            int val = keyboard_poll_raw();
            if (val < 0) break;
            ubuf[count++] = (uint8_t)val;
        }
        return count;
    }
    if (n->dev_major == DEV_MOUSE_MAJOR) {
        struct PACKED {
            int8_t dx;
            int8_t dy;
            uint8_t buttons;
        } pkt;
        if (len < sizeof(pkt)) return -EINVAL;
        int count = 0;
        uint8_t *ubuf = (uint8_t *)buf;
        while (count + sizeof(pkt) <= len) {
            int dx, dy;
            uint8_t buttons;
            if (!mouse_poll(&dx, &dy, &buttons)) break;
            pkt.dx = (int8_t)dx;
            pkt.dy = (int8_t)dy;
            pkt.buttons = buttons;
            memcpy(ubuf + count, &pkt, sizeof(pkt));
            count += sizeof(pkt);
        }
        return count;
    }
    if (n->dev_major == DEV_SERIAL_MAJOR) {
        int count = 0;
        char *ubuf = (char *)buf;
        while ((size_t)count < len) {
            if (!serial_received()) break;
            ubuf[count++] = serial_getchar();
        }
        return count;
    }
    return -ENODEV;
}

int devfs_chr_write(VFSNode *n, const void *buf, size_t len, size_t *off)
{
    if (!n || n->type != VFS_CHR) return -EIO;
    if (n->dev_major == DEV_NULL_MAJOR) return (int)len;
    if (n->dev_major == DEV_FB_MAJOR) return -EINVAL;
    if (n->dev_major == DEV_SHM_MAJOR) {
        extern int shm_write(int idx, const void *buf, size_t len, size_t *off);
        return shm_write(n->dev_minor, buf, len, off);
    }
    if (n->dev_major == DEV_SERIAL_MAJOR) {
        const char *ubuf = (const char *)buf;
        for (size_t i = 0; i < len; i++) {
            serial_putchar(ubuf[i]);
        }
        return (int)len;
    }
    return -ENODEV;
}

int devfs_chr_ioctl(VFSNode *n, uint64_t request, void *arg)
{
    if (!n || n->type != VFS_CHR || !arg) return -EINVAL;
    if (n->dev_major != DEV_FB_MAJOR) return -ENOTTY;

    if (request == FBIOGET_FSCREENINFO) {
        fb_fix_screeninfo *fix = (fb_fix_screeninfo *)arg;
        memset(fix, 0, sizeof(*fix));
        strncpy(fix->id, "VNLFB", sizeof(fix->id) - 1);
        uint64_t p0, ln;
        uint32_t ll, w, h, bpp;
        if (!fb_get_mmap_region(&p0, &ln, &ll, &w, &h, &bpp))
            return -ENODEV;
        fix->smem_start  = p0;
        fix->smem_len    = ln > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)ln;
        fix->line_length = ll;
        fix->type        = 1;
        fix->visual      = 2;
        return 0;
    }
    if (request == FBIOGET_VSCREENINFO) {
        fb_var_screeninfo *v = (fb_var_screeninfo *)arg;
        memset(v, 0, sizeof(*v));
        uint64_t p0, ln;
        uint32_t ll, w, h, bpp;
        (void)p0;
        (void)ln;
        if (!fb_get_mmap_region(&p0, &ln, &ll, &w, &h, &bpp))
            return -ENODEV;
        v->xres = v->xres_virtual = w;
        v->yres = v->yres_virtual = h;
        v->bits_per_pixel = bpp;
        v->red_offset = 16;
        v->green_offset = 8;
        v->blue_offset = 0;
        v->red_length = v->green_length = v->blue_length = 8;
        return 0;
    }
    return -ENOTTY;
}
