#pragma once

#include <stdint.h>

struct wlr_pointer {
    /* minimal pointer device structure */
    uint32_t dummy;
};

struct wlr_pointer_motion_event {
    uint32_t time_msec;
    double delta_x;
    double delta_y;
};
