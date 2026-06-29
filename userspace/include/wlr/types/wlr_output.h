#pragma once

#include "wlr/backend.h"

struct wlr_output {
    struct wlr_backend *backend;
    char name[32];
    uint32_t width;
    uint32_t height;
};

struct wlr_output_event_commit {
    struct wlr_output *output;
};
