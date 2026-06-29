#pragma once

#include <stdint.h>

struct wlr_keyboard {
    /* minimal keyboard device structure */
    uint32_t dummy;
};

struct wlr_keyboard_key_event {
    uint32_t time_msec;
    uint32_t keycode;
    uint32_t state; // 0 = released, 1 = pressed
};
