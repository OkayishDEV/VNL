#include "gui.h"
#include "fb.h"
#include "keyboard.h"
#include "mouse.h"
#include "vga.h"
#include "types.h"
#include "string.h"
#include "printf.h"

#define CURSOR_W 12
#define CURSOR_H 19

static const char *cursor_shape[CURSOR_H] = {
    "B",
    "BWB",
    "BWWB",
    "BWWWB",
    "BWWWWB",
    "BWWWWWB",
    "BWWWWWWB",
    "BWWWWWWWB",
    "BWWWWWWWWB",
    "BWWWWWWWWWB",
    "BWWWWWBBBBB",
    "BWWBWWB",
    "BWB BWWB",
    "BB  BWWB",
    "     BWWB",
    "     BWWB",
    "      BWB",
    "      BB",
    "       B"
};

static uint32_t s_saved_bg[CURSOR_H][CURSOR_W];
static bool s_cursor_visible = false;
static int s_cur_x = 0;
static int s_cur_y = 0;

static void hide_cursor(void)
{
    if (!s_cursor_visible) return;
    FBInfo fb;
    fb_get_info(&fb);
    for (int r = 0; r < CURSOR_H; r++) {
        for (int c = 0; c < CURSOR_W; c++) {
            int px = s_cur_x + c;
            int py = s_cur_y + r;
            if (px >= 0 && px < (int)fb.width && py >= 0 && py < (int)fb.height) {
                fb_plot((uint32_t)px, (uint32_t)py, s_saved_bg[r][c]);
            }
        }
    }
    s_cursor_visible = false;
}

static void show_cursor(int x, int y)
{
    hide_cursor();
    FBInfo fb;
    fb_get_info(&fb);
    s_cur_x = x;
    s_cur_y = y;

    /* Save background */
    for (int r = 0; r < CURSOR_H; r++) {
        for (int c = 0; c < CURSOR_W; c++) {
            int px = s_cur_x + c;
            int py = s_cur_y + r;
            if (px >= 0 && px < (int)fb.width && py >= 0 && py < (int)fb.height) {
                s_saved_bg[r][c] = fb_get_pixel((uint32_t)px, (uint32_t)py);
            } else {
                s_saved_bg[r][c] = 0;
            }
        }
    }

    /* Draw cursor */
    for (int r = 0; r < CURSOR_H; r++) {
        const char *row_str = cursor_shape[r];
        for (int c = 0; row_str[c] && c < CURSOR_W; c++) {
            char ch = row_str[c];
            if (ch == ' ') continue;
            uint32_t color = (ch == 'W') ? 0xFFFFFFFF : 0xFF000000;
            int px = s_cur_x + c;
            int py = s_cur_y + r;
            if (px >= 0 && px < (int)fb.width && py >= 0 && py < (int)fb.height) {
                fb_plot((uint32_t)px, (uint32_t)py, color);
            }
        }
    }
    s_cursor_visible = true;
}

static void draw_ui_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *label, bool hovered, bool pressed)
{
    uint32_t bg_color = pressed ? 0xFF353550 : (hovered ? 0xFF65658C : 0xFF505075);
    uint32_t border   = pressed ? 0xFF202035 : 0xFF353550;
    uint32_t text_col = hovered ? 0xFFFFFFFF : 0xFFE8E8F5;

    fb_fill_rect(x, y, w, h, border);
    fb_fill_rect(x + 1, y + 1, w - 2, h - 2, bg_color);

    if (label) {
        uint32_t slen = (uint32_t)strlen(label);
        uint32_t tx = x + (w - slen * 8) / 2;
        uint32_t ty = y + (h - 8) / 2;
        fb_draw_string(tx, ty, label, text_col, bg_color);
    }
}

void gui_session_run(void)
{
    if (!fb_is_available())
        return;

    FBInfo fb;
    fb_get_info(&fb);

    const uint32_t bg  = 0xFF2D2D3D;
    const uint32_t bar = 0xFF3A3A5C;
    const uint32_t fg  = 0xFFE8E8F5;
    const uint32_t dim = 0xFF9A9AB8;

    /* Full desktop clear */
    fb_fill_rect(0, 0, fb.width, fb.height, bg);

    uint32_t bar_h = 40;
    if (bar_h > fb.height / 4) bar_h = fb.height / 8;
    if (bar_h < 24) bar_h = 24;
    fb_fill_rect(0, 0, fb.width, bar_h, bar);

    fb_draw_string(12, 11, "VNL :0   Interactive GUI Environment (Double-Buffered Cursor)", fg, bar);

    /* Desktop info text */
    uint32_t y = bar_h + 20;
    fb_draw_string(12, y, "Welcome to the real-time VNL GUI framework.", fg, bg); y += 16;
    fb_draw_string(12, y, "Move your mouse to control the cursor pointer smoothly.", dim, bg); y += 16;
    fb_draw_string(12, y, "Press Esc or q, or click the Exit button below to return to the shell.", dim, bg);

    /* Premium centered Window */
    uint32_t win_w = 420;
    uint32_t win_h = 240;
    uint32_t win_x = (fb.width > win_w) ? (fb.width - win_w) / 2 : 10;
    uint32_t win_y = (fb.height > win_h) ? (fb.height - win_h) / 2 : 80;

    /* Window shadow and border */
    fb_fill_rect(win_x + 6, win_y + 6, win_w, win_h, 0xFF151520);
    fb_fill_rect(win_x, win_y, win_w, win_h, 0xFF3D3D4D);
    fb_fill_rect(win_x + 2, win_y + 2, win_w - 4, win_h - 4, 0xFF282838);

    /* Title bar */
    fb_fill_rect(win_x + 2, win_y + 2, win_w - 4, 28, 0xFF4A4A6C);
    fb_draw_string(win_x + 12, win_y + 10, "Control Panel", fg, 0xFF4A4A6C);

    /* Inside window content */
    uint32_t wy = win_y + 45;
    fb_draw_string(win_x + 20, wy, "Mouse input processing is fully live.", fg, 0xFF282838); wy += 20;
    
    int click_count = 0;
    char click_buf[64];
    ksprintf(click_buf, sizeof(click_buf), "Total Clicks Registered: %d", click_count);
    fb_draw_string(win_x + 20, wy, click_buf, 0xFFFFFF00, 0xFF282838);

    /* Buttons layout */
    uint32_t btn1_w = 140;
    uint32_t btn1_h = 32;
    uint32_t btn1_x = win_x + 40;
    uint32_t btn1_y = win_y + win_h - 50;

    uint32_t btn2_w = 140;
    uint32_t btn2_h = 32;
    uint32_t btn2_x = win_x + win_w - btn2_w - 40;
    uint32_t btn2_y = win_y + win_h - 50;

    bool btn1_hover = false, btn1_press = false;
    bool btn2_hover = false, btn2_press = false;

    draw_ui_button(btn1_x, btn1_y, btn1_w, btn1_h, "Click Me", btn1_hover, btn1_press);
    draw_ui_button(btn2_x, btn2_y, btn2_w, btn2_h, "Exit GUI", btn2_hover, btn2_press);

    int mx = fb.width / 2;
    int my = fb.height / 2;
    s_cursor_visible = false;
    show_cursor(mx, my);

    /* Flush leftover mouse events before entering loop */
    mouse_flush();

    bool exit_session = false;
    uint8_t prev_buttons = 0;

    while (!exit_session) {
        /* Check keyboard */
        int k = keyboard_poll();
        if (k >= 0) {
            if (k == 27 || k == 'q' || k == 'Q') {
                break;
            }
        }

        /* Check mouse */
        int dx = 0, dy = 0;
        uint8_t buttons = prev_buttons;
        bool mouse_moved = false;
        bool redraw_ui = false;

        while (mouse_poll(&dx, &dy, &buttons)) {
            mx += dx;
            my -= dy;
            if (dx != 0 || dy != 0) mouse_moved = true;

            if (mx < 0) mx = 0;
            if (mx >= (int)fb.width) mx = (int)fb.width - 1;
            if (my < 0) my = 0;
            if (my >= (int)fb.height) my = (int)fb.height - 1;

            bool in_btn1 = (mx >= (int)btn1_x && mx < (int)(btn1_x + btn1_w) &&
                            my >= (int)btn1_y && my < (int)(btn1_y + btn1_h));
            bool in_btn2 = (mx >= (int)btn2_x && mx < (int)(btn2_x + btn2_w) &&
                            my >= (int)btn2_y && my < (int)(btn2_y + btn2_h));

            bool b_down = (buttons & 1) != 0;
            bool prev_down = (prev_buttons & 1) != 0;

            /* Check button 1 click (press -> release) */
            if (in_btn1 && prev_down && !b_down) {
                click_count++;
                ksprintf(click_buf, sizeof(click_buf), "Total Clicks Registered: %d", click_count);
                redraw_ui = true;
            }
            /* Check button 2 click */
            if (in_btn2 && prev_down && !b_down) {
                exit_session = true;
            }

            /* Update hover/press visuals */
            bool new_b1_h = in_btn1;
            bool new_b1_p = in_btn1 && b_down;
            bool new_b2_h = in_btn2;
            bool new_b2_p = in_btn2 && b_down;

            if (new_b1_h != btn1_hover || new_b1_p != btn1_press) {
                btn1_hover = new_b1_h;
                btn1_press = new_b1_p;
                redraw_ui = true;
            }
            if (new_b2_h != btn2_hover || new_b2_p != btn2_press) {
                btn2_hover = new_b2_h;
                btn2_press = new_b2_p;
                redraw_ui = true;
            }

            prev_buttons = buttons;
        }

        if (mouse_moved || redraw_ui) {
            hide_cursor();

            if (redraw_ui) {
                /* Update click label */
                fb_draw_string(win_x + 20, wy, click_buf, 0xFFFFFF00, 0xFF282838);
                draw_ui_button(btn1_x, btn1_y, btn1_w, btn1_h, "Click Me", btn1_hover, btn1_press);
                draw_ui_button(btn2_x, btn2_y, btn2_w, btn2_h, "Exit GUI", btn2_hover, btn2_press);
            }

            show_cursor(mx, my);
        } else {
            /* Small halt/yield to not melt CPU */
            asm volatile("hlt");
        }
    }

    hide_cursor();
    fb_console_reset();
    vga_fb_mirror_refresh();
}
