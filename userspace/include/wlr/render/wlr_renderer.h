#pragma once

#include "wlr/backend.h"

struct wlr_renderer {
    struct wlr_backend *backend;
    uint32_t current_width;
    uint32_t current_height;
};

struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend);
void wlr_renderer_begin(struct wlr_renderer *r, uint32_t width, uint32_t height);
void wlr_renderer_end(struct wlr_renderer *r);
void wlr_renderer_clear(struct wlr_renderer *r, const float color[4]);
