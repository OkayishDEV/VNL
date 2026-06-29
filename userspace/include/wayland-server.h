#pragma once

#include "types.h"

struct wl_event_loop;
struct wl_client;

struct wl_display {
    struct wl_event_loop *loop;
    int socket_fd;
};

struct wl_signal {
    /* minimal signal placeholder */
    void *dummy;
};

struct wl_listener {
    void (*notify)(struct wl_listener *listener, void *data);
};

static inline struct wl_display *wl_display_create(void) {
    extern void *kmalloc(size_t);
    struct wl_display *d = (struct wl_display *)kmalloc(sizeof(*d));
    d->socket_fd = -1;
    return d;
}

static inline void wl_display_destroy(struct wl_display *display) {
    extern void kfree(void *);
    kfree(display);
}
