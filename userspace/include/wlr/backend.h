#pragma once

#include "wayland-server.h"

struct wlr_backend {
    struct wl_display *display;
    int fb_fd;
    uint32_t *fb_mem;
    int kbd_fd;
    int mouse_fd;
};

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display);
bool wlr_backend_start(struct wlr_backend *backend);
void wlr_backend_destroy(struct wlr_backend *backend);
