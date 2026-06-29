/*
 * userspace/backend/vnl/backend.c
 * Custom VNL hardware backend for wlroots shim library.
 */

#include "wlr/backend.h"
#include "wlr/render/wlr_renderer.h"
#include "wlr/types/wlr_output.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define VFS_O_READ  0x0001
#define VFS_O_WRITE 0x0002

#define sys_open(path, flags) \
    open(path, \
         (((flags) & VFS_O_READ) && ((flags) & VFS_O_WRITE)) ? O_RDWR : \
         ((flags) & VFS_O_WRITE) ? O_WRONLY : O_RDONLY)

#define sys_close(fd) close(fd)
#define sys_mmap(addr, len, prot, fl, fd, o) mmap(addr, len, prot, fl, fd, o)
#define sys_exit(status) exit(status)
#define kmalloc(sz) malloc(sz)
#define kfree(p) free(p)

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display) {
    struct wlr_backend *backend = (struct wlr_backend *)kmalloc(sizeof(*backend));
    if (!backend) return NULL;
    backend->display = display;

    /* Bind Framebuffer */
    int fb_fd = (int)sys_open("/dev/fb0", VFS_O_READ | VFS_O_WRITE);
    if (fb_fd < 0) {
        kfree(backend);
        return NULL;
    }
    backend->fb_fd = fb_fd;

    uint32_t *fb_mem = (uint32_t *)sys_mmap(NULL, 1024 * 768 * 4, 3, 1, fb_fd, 0);
    if ((intptr_t)fb_mem <= 0) {
        sys_close(fb_fd);
        kfree(backend);
        return NULL;
    }
    backend->fb_mem = fb_mem;

    /* Bind Input Devices */
    backend->kbd_fd = (int)sys_open("/dev/input/keyboard", VFS_O_READ);
    backend->mouse_fd = (int)sys_open("/dev/input/mouse", VFS_O_READ);
    if (backend->kbd_fd < 0 || backend->mouse_fd < 0) {
        sys_close(backend->kbd_fd >= 0 ? backend->kbd_fd : backend->mouse_fd);
        sys_close(fb_fd);
        kfree(backend);
        return NULL;
    }

    return backend;
}

bool wlr_backend_start(struct wlr_backend *backend) {
    return backend != NULL;
}

void wlr_backend_destroy(struct wlr_backend *backend) {
    if (backend) {
        sys_close(backend->kbd_fd);
        sys_close(backend->mouse_fd);
        sys_close(backend->fb_fd);
        kfree(backend);
    }
}

/* wlroots software renderer implementation */
struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend) {
    struct wlr_renderer *renderer = (struct wlr_renderer *)kmalloc(sizeof(*renderer));
    if (!renderer) return NULL;
    renderer->backend = backend;
    renderer->current_width = 1024;
    renderer->current_height = 768;
    return renderer;
}

void wlr_renderer_begin(struct wlr_renderer *r, uint32_t width, uint32_t height) {
    r->current_width = width;
    r->current_height = height;
}

void wlr_renderer_end(struct wlr_renderer *r) {
    (void)r;
}

void wlr_renderer_clear(struct wlr_renderer *r, const float color[4]) {
    uint8_t a = (uint8_t)(color[3] * 255.0f);
    uint8_t red = (uint8_t)(color[0] * 255.0f);
    uint8_t g = (uint8_t)(color[1] * 255.0f);
    uint8_t b = (uint8_t)(color[2] * 255.0f);
    uint32_t val = (a << 24) | (red << 16) | (g << 8) | b;

    uint32_t limit = r->current_width * r->current_height;
    uint32_t *fb = r->backend->fb_mem;
    for (uint32_t i = 0; i < limit; i++) {
        fb[i] = val;
    }
}
