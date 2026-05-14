#include "desktop.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "printf.h"
#include "string.h"
#include "timer.h"
#include "vfs.h"
#include "vga.h"
#include "heap.h"
#include "fonts.h"
#include "shell.h"
#include "rtl8139.h"
#include "vnet.h"

uint32_t *desktop_get_backbuffer(void);
void desktop_get_fb_info(FBInfo *info);
void desktop_refresh(void);

/* some shitty colors for the eyes */
#define VNL_GRAD_START 0xFF001F3F
#define VNL_GRAD_END   0xFF0074D9
#define VNL_GLASS      0xAAFFFFFF
#define VNL_SHADOW     0x60000000
#define VNL_SURFACE    0xFFF8F8F8
#define VNL_TITLE      0xFF0074D9
#define VNL_ACCENT     0xFFFF851B
#define VNL_DKSHAD     0xFF1A1A1A
#define VNL_WHITE      0xFFFFFFFF
#define VNL_GLOW       0x400074D9

#define TASKBAR_HEIGHT 40
#define CURSOR_W 12
#define CURSOR_H 19

#define TERM_COLS 60
#define TERM_ROWS 25
#define TERM_BUF_SIZE (TERM_COLS * TERM_ROWS)

extern void cmd_neovim_vnl(int argc, char **argv);
extern void cmd_htop_gui(int argc, char **argv);
extern void cmd_doom_generic(int argc, char **argv);
extern void cmd_vinstall(int argc, char **argv);
extern void cmd_vpkg(int argc, char **argv);
extern void cmd_vsnake(int argc, char **argv);

typedef enum { WIN_TERM, WIN_BROWSER, WIN_EXPLORER, WIN_MUSIC, WIN_SNAKE } WindowKind;

typedef struct Window {
    WindowKind kind;
    int x, y, w, h;
    const char *title;
    bool active, dragging, minimized;
    int drag_off_x, drag_off_y;
    uint16_t term_buffer[TERM_BUF_SIZE];
    int term_cursor_x, term_cursor_y;
    char term_cmd[64];
    int term_idx;
    char url[128];
    int url_idx;
    char content[1024];
    char path[128];
    struct Window *next;
} Window;

static Window *windows = NULL;
static Window *focused_win = NULL;
static uint32_t *backbuffer = NULL;
static FBInfo fb_info;

static const char *cursor_shape[CURSOR_H] = {
    "B", "BWB", "BWWB", "BWWWB", "BWWWWB", "BWWWWWB", "BWWWWWWB", "BWWWWWWWB",
    "BWWWWWWWWB", "BWWWWWWWWWB", "BWWWWWBBBBB", "BWWBWWB", "BWB BWWB",
    "BB  BWWB", "     BWWB", "     BWWB", "      BWB", "      BB", "       B"
};

static uint32_t vga_to_32bpp(uint8_t vga_col) {
    static const uint32_t palette[] = {
        0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
        0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
        0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
        0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
    };
    return palette[vga_col & 0xF];
}

static uint32_t blend(uint32_t bg, uint32_t fg) {
    uint32_t alpha = (fg >> 24) & 0xFF; if (alpha == 255) return fg; if (alpha == 0) return bg;
    uint32_t r_bg = (bg >> 16) & 0xFF, g_bg = (bg >> 8) & 0xFF, b_bg = bg & 0xFF;
    uint32_t r_fg = (fg >> 16) & 0xFF, g_fg = (fg >> 8) & 0xFF, b_fg = fg & 0xFF;
    uint32_t r = (r_fg * alpha + r_bg * (255 - alpha)) >> 8;
    uint32_t g = (g_fg * alpha + g_bg * (255 - alpha)) >> 8;
    uint32_t b = (b_fg * alpha + b_bg * (255 - alpha)) >> 8;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static uint32_t lerp_color(uint32_t c1, uint32_t c2, float t) {
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t), g = (uint8_t)(g1 + (g2 - g1) * t), b = (uint8_t)(b1 + (b2 - b1) * t);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void bb_plot(int x, int y, uint32_t c) {
    if (x < 0 || x >= (int)fb_info.width || y < 0 || y >= (int)fb_info.height) return;
    backbuffer[y * fb_info.width + x] = blend(backbuffer[y * fb_info.width + x], c);
}

static void bb_blur_rect(int x, int y, int w, int h) {
    if (x < 1 || y < 1 || x + w >= (int)fb_info.width - 1 || y + h >= (int)fb_info.height - 1) return;
    for (int r = 0; r < h; r++) {
        for (int col = 0; col < w; col++) {
            int cur_x = x + col; int cur_y = y + r;
            uint32_t c1 = backbuffer[cur_y * fb_info.width + (cur_x - 1)];
            uint32_t c2 = backbuffer[cur_y * fb_info.width + (cur_x + 1)];
            uint32_t c3 = backbuffer[(cur_y - 1) * fb_info.width + cur_x];
            uint32_t c4 = backbuffer[(cur_y + 1) * fb_info.width + cur_x];
            uint32_t r_b = (((c1>>16)&0xFF)+((c2>>16)&0xFF)+((c3>>16)&0xFF)+((c4>>16)&0xFF)) >> 2;
            uint32_t g_b = (((c1>>8)&0xFF)+((c2>>8)&0xFF)+((c3>>8)&0xFF)+((c4>>8)&0xFF)) >> 2;
            uint32_t b_b = ((c1&0xFF)+(c2&0xFF)+(c3&0xFF)+(c4&0xFF)) >> 2;
            backbuffer[cur_y * fb_info.width + cur_x] = 0xFF000000 | (r_b << 16) | (g_b << 8) | b_b;
        }
    }
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    for (int r = 0; r < h; r++) {
        for (int col = 0; col < w; col++) {
            bb_plot(x + col, y + r, c);
        }
    }
}

static void bb_draw_string(int x, int y, const char *s, uint32_t fg) { if (!s) return; int cx = x; while (*s) { int idx = glyph_index(*s); const uint8_t *gp = g_vnl_fonts[idx]; for (int yy = 0; yy < 8; yy++) { uint8_t bits = gp[yy]; for (int xx = 0; xx < 8; xx++) { if (bits & (1 << (7 - xx))) bb_plot(cx + xx, y + yy, fg); } } cx += 8; s++; } }

static void vga_universal_hook(VGAEvent ev, int x, int y, char c, uint8_t color) {
    if (!focused_win || focused_win->kind != WIN_TERM) return;
    Window *win = focused_win;
    if (ev == VGA_EVENT_CLEAR) {
        for(int i=0; i<TERM_BUF_SIZE; i++) win->term_buffer[i] = (uint16_t)(' ' | (color << 8));
        win->term_cursor_x = 0; win->term_cursor_y = 0;
    } else if (ev == VGA_EVENT_CURSOR) {
        win->term_cursor_x = x; win->term_cursor_y = y;
    } else if (ev == VGA_EVENT_PUTCHAR) {
        if (c == '\n') { win->term_cursor_x = 0; win->term_cursor_y++; }
        else if (c == '\r') { win->term_cursor_x = 0; }
        else if (c == '\b') { if (win->term_cursor_x > 0) win->term_cursor_x--; win->term_buffer[win->term_cursor_y * TERM_COLS + win->term_cursor_x] = (uint16_t)(' ' | (color << 8)); }
        else {
            if (win->term_cursor_x < TERM_COLS && win->term_cursor_y < TERM_ROWS) {
                win->term_buffer[win->term_cursor_y * TERM_COLS + win->term_cursor_x] = (uint16_t)((uint8_t)c | (color << 8));
                win->term_cursor_x++;
                if (win->term_cursor_x >= TERM_COLS) { win->term_cursor_x = 0; win->term_cursor_y++; }
            }
        }
    }
    if (win->term_cursor_y >= TERM_ROWS) {
        memmove(win->term_buffer, win->term_buffer + TERM_COLS, TERM_COLS * (TERM_ROWS - 1) * 2);
        for(int i=0; i<TERM_COLS; i++) win->term_buffer[TERM_COLS*(TERM_ROWS-1)+i] = (uint16_t)(' ' | (color << 8));
        win->term_cursor_y = TERM_ROWS - 1;
    }
}

static void bb_draw_cursor(int x, int y) { for (int r = 0; r < CURSOR_H; r++) { const char *row_str = cursor_shape[r]; for (int c = 0; row_str[c] && c < CURSOR_W; c++) { char ch = row_str[c]; if (ch == ' ') continue; uint32_t color = (ch == 'W') ? 0xFFFFFFFF : 0xFF000000; bb_plot(x + c, y + r, color); } } }

Window *window_create(WindowKind kind, int x, int y, int w, int h, const char *title) {
    Window *win = (Window *)kmalloc(sizeof(Window)); memset(win, 0, sizeof(Window));
    win->kind = kind; win->x = x; win->y = y; win->w = w; win->h = h; win->title = title;
    if (kind == WIN_TERM) for(int i=0; i<TERM_BUF_SIZE; i++) win->term_buffer[i] = (uint16_t)(' ' | (VGA_WHITE << 8));
    else if (kind == WIN_BROWSER) {
        strncpy(win->url, "vibe://home", 127); win->url_idx = strlen(win->url);
        strncpy(win->content, "Welcome to VibeWeb!\n\nFeatured Sites:\n- vibe://vnl (Official)\n- vibe://osdev (Resources)\n- vibe://doom (Fun)\n\nVNL - The vibe that never dies.", 1023);
    } else if (kind == WIN_EXPLORER) { strncpy(win->path, "/", 127); }
    win->next = windows; windows = win; focused_win = win; return win;
}

static void browser_navigate(Window *win) {
    char u[128]; strncpy(u, win->url, 127);
    for(int i=0; u[i]; i++) if(u[i]>='A'&&u[i]<='Z') u[i]+=32;
    if (strcmp(u, "vibe://vnl") == 0) strncpy(win->content, "Vibe Not Linux (VNL)\n------------------\nAn agentic low-level OS designed\nfor peak vibes and high performance.\n\nKernel: 1.2.2 (Stable)\nArch: x86_64\n\n- vibe://home (Back)", 1023);
    else if (strcmp(u, "vibe://home") == 0) strncpy(win->content, "Welcome to VibeWeb!\n\nFeatured Sites:\n- vibe://vnl (Official)\n- vibe://osdev (Resources)\n- vibe://doom (Fun)\n\nVNL - The vibe that never dies.", 1023);
    else if (strstr(u, "google.com") || strstr(u, "vibe://search") || strlen(u) < 15) strncpy(win->content, "G O O G L E\n-----------\nSearch: [                        ] [Search]\n\nResults:\n1. VNL RTL8139: [ONLINE]\n2. VNL Graphics: [STABLE]\n- vibe://home (Home)", 1023);
    else strncpy(win->content, "404 Not Found\n-------------\nThat vibe doesn't exist yet.\n\n- vibe://home (Home)", 1023);
}

void desktop_run(void) {
    if (!fb_is_available()) return;
    fb_get_info(&fb_info);
    if (!backbuffer) backbuffer = (uint32_t *)kmalloc(fb_info.width * fb_info.height * 4);
    if (!backbuffer) return;

    typedef struct { int x, y, w, h; const char *label; const char *binary_path; void (*action)(int argc, char **argv); uint32_t icon_color; } DesktopIcon;
    DesktopIcon icons_master[] = {
        {30, 30, 64, 64, "Files", NULL, NULL, 0xFFCCCCCC},
        {30, 110, 64, 64, "Shell", NULL, NULL, 0xFF444444},
        {30, 190, 64, 64, "VibeNet", NULL, NULL, 0xFF0000AA},
        {120, 30, 64, 64, "DOOM", "/usr/bin/doom", cmd_doom_generic, 0xFFAA0000},
        {120, 110, 64, 64, "Snake", NULL, NULL, 0xFF0074D9},
        {120, 190, 64, 64, "NeoVim", NULL, NULL, 0xFFFF851B}
    };
    DesktopIcon active_icons[10]; int active_count = 0;
    for (int i = 0; i < (int)(sizeof(icons_master)/sizeof(icons_master[0])); i++) {
        if (!icons_master[i].binary_path || vfs_resolve(icons_master[i].binary_path) >= 0) active_icons[active_count++] = icons_master[i];
    }

    bool exit_desktop = false, start_menu_open = false;
    int mx = fb_info.width / 2, my = fb_info.height / 2; uint8_t prev_buttons = 0; int selected_icon = -1;
    /* flush the mouse and hook this vga shit */
    mouse_flush(); vga_set_hook(vga_universal_hook);

    while (!exit_desktop) {
        /* draw the gradient because apparently we're artists now */
        for (uint32_t y = 0; y < fb_info.height; y++) {
            uint32_t c = lerp_color(VNL_GRAD_START, VNL_GRAD_END, (float)y / fb_info.height);
            uint32_t *row_ptr = backbuffer + y * fb_info.width;
            for (uint32_t x = 0; x < fb_info.width; x++) row_ptr[x] = c;
        }
        for(int y=0; y<(int)fb_info.height; y+=128) for(int x=0; x<(int)fb_info.width; x+=128) bb_fill_rect(x, y, 4, 4, 0x10FFFFFF);

        /* taskbar... what the fuck is a taskbar? */
        bb_fill_rect(0, fb_info.height - TASKBAR_HEIGHT, fb_info.width, TASKBAR_HEIGHT, blend(VNL_DKSHAD, 0xCC000000));
        bb_blur_rect(1, fb_info.height - TASKBAR_HEIGHT + 1, fb_info.width - 2, TASKBAR_HEIGHT - 2);
        
        bool start_hover = (mx >= 8 && mx < 120 && my >= (int)fb_info.height - TASKBAR_HEIGHT + 6 && my < (int)fb_info.height - 6);
        bb_fill_rect(8, fb_info.height - TASKBAR_HEIGHT + 6, 112, TASKBAR_HEIGHT - 12, start_menu_open ? VNL_ACCENT : (start_hover ? VNL_TITLE : 0x40FFFFFF));
        bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT + 16, "S T A R T", VNL_WHITE);

        for (int i = 0; i < active_count; i++) {
            bb_fill_rect(active_icons[i].x + 16, active_icons[i].y, 32, 32, active_icons[i].icon_color);
            int lx = active_icons[i].x + (64 - (strlen(active_icons[i].label) * 8)) / 2;
            if (selected_icon == i) { bb_fill_rect(lx - 4, active_icons[i].y + 35, (strlen(active_icons[i].label) * 8) + 8, 14, VNL_ACCENT); }
            bb_draw_string(lx, active_icons[i].y + 38, active_icons[i].label, VNL_WHITE);
        }

        /* windows... like a broken house */
        Window *win = windows;
        while (win) {
            if (win->minimized) { win = win->next; continue; }
            bool active = (win == focused_win);
            bb_fill_rect(win->x + 6, win->y + 6, win->w, win->h, VNL_SHADOW);
            if (active) bb_fill_rect(win->x - 2, win->y - 26, win->w + 4, win->h + 28, VNL_GLOW);
            bb_fill_rect(win->x, win->y, win->w, win->h, VNL_SURFACE);
            if (win->y > 24) bb_blur_rect(win->x + 1, win->y - 23, win->w - 2, 22);
            bb_fill_rect(win->x, win->y - 24, win->w, 24, active ? blend(VNL_TITLE, 0x80000000) : blend(VNL_DKSHAD, 0x80000000));
            bb_draw_string(win->x + 12, win->y - 15, win->title, VNL_WHITE);
            bb_fill_rect(win->x + win->w - 24, win->y - 20, 18, 16, VNL_DKSHAD);
            bb_draw_string(win->x + win->w - 18, win->y - 15, "X", VNL_WHITE);
            
            if (win->kind == WIN_TERM) {
                bb_fill_rect(win->x, win->y, win->w, win->h, 0xFF0A0A0A);
                for (int r = 0; r < TERM_ROWS; r++) {
                    for (int c = 0; c < TERM_COLS; c++) {
                        uint16_t entry = win->term_buffer[r * TERM_COLS + c];
                        char ch = (char)(entry & 0xFF); uint8_t color = (uint8_t)(entry >> 8);
                        if (ch != ' ') {
                            int idx = glyph_index(ch); const uint8_t *gp = g_vnl_fonts[idx]; uint32_t fg = vga_to_32bpp(color);
                            for (int yy = 0; yy < 8; yy++) { uint8_t bits = gp[yy]; for (int xx = 0; xx < 8; xx++) { if (bits & (1 << (7 - xx))) bb_plot(win->x + 8 + c * 8 + xx, win->y + 8 + r * 11 + yy, fg); } }
                        }
                    }
                }
                if (active) {
                    bb_fill_rect(win->x + 8 + win->term_cursor_x * 8, win->y + 8 + win->term_cursor_y * 11, 8, 10, 0x80FFFFFF);
                    if (win->term_idx > 0) { bb_draw_string(win->x + 8, win->y + win->h - 15, "> ", 0xFFFFFF00); bb_draw_string(win->x + 24, win->y + win->h - 15, win->term_cmd, VNL_WHITE); }
                }
            } else if (win->kind == WIN_BROWSER) {
                bb_fill_rect(win->x, win->y, win->w, 36, VNL_SURFACE);
                bb_fill_rect(win->x + 12, win->y + 8, win->w - 80, 20, VNL_WHITE);
                bb_draw_string(win->x + 20, win->y + 14, win->url, VNL_DKSHAD);
                if (active) bb_fill_rect(win->x + 20 + win->url_idx * 8, win->y + 10, 2, 16, VNL_TITLE);
                bb_fill_rect(win->x + win->w - 56, win->y + 8, 44, 20, VNL_TITLE);
                bb_draw_string(win->x + win->w - 46, win->y + 14, "GO", VNL_WHITE);
                bb_fill_rect(win->x, win->y + 36, win->w, win->h - 36, VNL_WHITE);
                int cy = win->y + 45; const char *p = win->content;
                while (*p) {
                    const char *ls = p; while (*p && *p != '\n') p++;
                    size_t llen = (size_t)(p - ls); char lb[128]; if(llen>=128)llen=127; memcpy(lb, ls, llen); lb[llen]='\0';
                    bb_draw_string(win->x + 15, cy, lb, (lb[0] == '-') ? VNL_TITLE : VNL_DKSHAD); cy += 13; if (*p == '\n') p++;
                }
            } else if (win->kind == WIN_EXPLORER) {
                bb_fill_rect(win->x, win->y, win->w, 24, VNL_TITLE);
                bb_draw_string(win->x + 10, win->y + 8, win->path, VNL_WHITE);
                bb_fill_rect(win->x, win->y + 24, win->w, win->h - 24, VNL_WHITE);
                char names[32][VFS_NAME_MAX]; int n = vfs_readdir(win->path, names, 32);
                for(int i=0; i<n; i++) { bb_fill_rect(win->x + 15, win->y + 40 + i * 22, 18, 18, VNL_ACCENT); bb_draw_string(win->x + 40, win->y + 45, names[i], VNL_DKSHAD); }
            }
            win = win->next;
        }

        if (start_menu_open) {
            bb_fill_rect(8, fb_info.height - TASKBAR_HEIGHT - 320, 240, 310, blend(VNL_DKSHAD, 0xEE000000));
            bb_blur_rect(9, fb_info.height - TASKBAR_HEIGHT - 319, 238, 308);
            bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT - 300, "VNL WORKSTATION", VNL_ACCENT);
            bb_fill_rect(20, fb_info.height - TASKBAR_HEIGHT - 280, 200, 1, 0x40FFFFFF);
            bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT - 260, "> Programs", VNL_WHITE);
            bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT - 235, "> Explorer", VNL_WHITE);
            bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT - 210, "> Terminal", VNL_WHITE);
            bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT - 185, "> Network", VNL_WHITE);
            bb_draw_string(24, fb_info.height - TASKBAR_HEIGHT - 80, "[X] SHUT DOWN", VNL_ACCENT);
        }

        bb_draw_cursor(mx, my);
        uint64_t phys, len; uint32_t pitch, w_fb, h_fb, bpp; fb_get_mmap_region(&phys, &len, &pitch, &w_fb, &h_fb, &bpp);
        /* blit this crap to the real screen */
        uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys; for (uint32_t y = 0; y < h_fb; y++) memcpy(fb_ptr + y * pitch, backbuffer + y * w_fb, w_fb * 4);

        int k = keyboard_poll(); if (k == 27) break;
        if (focused_win && k > 0) {
            Window *fw = focused_win;
            if (fw->kind == WIN_TERM) {
                if (k == '\n') { shell_exec_line(fw->term_cmd); fw->term_idx = 0; fw->term_cmd[0] = 0; }
                else if (k == '\b' && fw->term_idx > 0) fw->term_cmd[--fw->term_idx] = 0;
                else if (fw->term_idx < 48 && k >= 32 && k <= 126) { fw->term_cmd[fw->term_idx++] = (char)k; fw->term_cmd[fw->term_idx] = 0; }
            } else if (fw->kind == WIN_BROWSER) {
                if (k == '\n') browser_navigate(fw);
                else if (k == '\b' && fw->url_idx > 0) fw->url[--fw->url_idx] = 0;
                else if (fw->url_idx < 120 && k >= 32 && k <= 126) { fw->url[fw->url_idx++] = (char)k; fw->url[fw->url_idx] = 0; }
            }
        }

        int dx, dy; uint8_t buttons;
        if (mouse_poll(&dx, &dy, &buttons)) {
            mx += dx; my -= dy; if (mx < 0) mx = 0; if (mx >= (int)fb_info.width) mx = (int)fb_info.width - 1; if (my < 0) my = 0; if (my >= (int)fb_info.height) my = (int)fb_info.height - 1;
            if ((buttons & 1) && !(prev_buttons & 1)) {
                if (start_hover) start_menu_open = !start_menu_open;
                else if (start_menu_open && mx >= 8 && mx < 248 && my >= (int)fb_info.height - TASKBAR_HEIGHT - 320 && my < (int)fb_info.height - TASKBAR_HEIGHT) {
                    int menu_top = (int)fb_info.height - TASKBAR_HEIGHT - 320;
                    int item_y = my - menu_top;
                    if (item_y >= 60 && item_y < 85) { /* Programs */ }
                    else if (item_y >= 85 && item_y < 110) { window_create(WIN_EXPLORER, 80, 80, 350, 450, "File Explorer"); }
                    else if (item_y >= 110 && item_y < 135) { window_create(WIN_TERM, 100, 100, 500, 350, "VNL Command Prompt"); }
                    else if (item_y >= 135 && item_y < 160) { window_create(WIN_BROWSER, 150, 120, 550, 400, "VibeNet Pro"); }
                    else if (item_y >= 240 && item_y < 265) { exit_desktop = true; }
                    start_menu_open = false;
                }
                else {
                    start_menu_open = false; Window *clicked_win = NULL, *curr = windows;
                    while (curr) {
                        if (mx >= curr->x + curr->w - 24 && mx < curr->x + curr->w - 6 && my >= curr->y - 20 && my < curr->y - 4) {
                            if (windows == curr) windows = curr->next; else { Window *p = windows; while (p->next != curr) p = p->next; p->next = curr->next; }
                            if (focused_win == curr) focused_win = windows; kfree(curr); break;
                        }
                        if (mx >= curr->x && mx < curr->x + curr->w && my >= curr->y - 24 && my < curr->y) { clicked_win = curr; curr->dragging = true; curr->drag_off_x = mx - curr->x; curr->drag_off_y = my - curr->y; break; }
                        else if (mx >= curr->x && mx < curr->x + curr->w && my >= curr->y && my < curr->y + curr->h) {
                            clicked_win = curr; if (curr->kind == WIN_BROWSER && mx >= curr->x + curr->w - 56 && mx < curr->x + curr->w - 12 && my >= curr->y + 8 && my < curr->y + 28) browser_navigate(curr);
                            break;
                        }
                        curr = curr->next;
                    }
                    if (clicked_win) {
                        focused_win = clicked_win;
                        if (clicked_win->next) {
                            if (windows == clicked_win) windows = clicked_win->next; else { Window *p = windows; while (p->next != clicked_win) p = p->next; p->next = clicked_win->next; }
                            Window *t = windows; while (t->next) t = t->next; t->next = clicked_win; clicked_win->next = NULL;
                        }
                    } else {
                        selected_icon = -1;
                        for (int i = 0; i < active_count; i++) {
                            if (mx >= active_icons[i].x && mx < active_icons[i].x + 64 && my >= active_icons[i].y && my < active_icons[i].y + 64) {
                                selected_icon = i;
                                if (active_icons[i].action) { fb_console_reset(); active_icons[i].action(0, NULL); mouse_flush(); }
                                else if (strcmp(active_icons[i].label, "Shell") == 0) window_create(WIN_TERM, 100, 100, 500, 350, "VNL Command Prompt");
                                else if (strcmp(active_icons[i].label, "VibeNet") == 0) window_create(WIN_BROWSER, 150, 120, 550, 400, "VibeNet Pro");
                                else if (strcmp(active_icons[i].label, "Files") == 0) window_create(WIN_EXPLORER, 80, 80, 350, 450, "File Explorer");
                                else if (strcmp(active_icons[i].label, "Snake") == 0) {
                                    Window *w = window_create(WIN_TERM, 120, 80, 500, 350, "VNL Snake Game");
                                    focused_win = w; vga_clear(); cmd_vsnake(0, NULL);
                                } else if (strcmp(active_icons[i].label, "NeoVim") == 0) {
                                    Window *w = window_create(WIN_TERM, 50, 50, 600, 400, "NeoVim (Professional)");
                                    focused_win = w; vga_clear(); cmd_neovim_vnl(0, NULL);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (!(buttons & 1)) {
                Window *curr = windows;
                while (curr) {
                    if (curr->dragging) {
                        if (mx < 10) { curr->x = 0; curr->y = 0; curr->w = fb_info.width / 2; curr->h = fb_info.height - TASKBAR_HEIGHT; }
                        else if (mx > (int)fb_info.width - 10) { curr->x = fb_info.width / 2; curr->y = 0; curr->w = fb_info.width / 2; curr->h = fb_info.height - TASKBAR_HEIGHT; }
                    }
                    curr->dragging = false; curr = curr->next;
                }
            }
            if (buttons & 1) { Window *curr = windows; while (curr) { if (curr->dragging) { curr->x = mx - curr->drag_off_x; curr->y = my - curr->drag_off_y; } curr = curr->next; } }
            prev_buttons = buttons;
        }
        timer_sleep(16);
    }
    vga_set_hook(NULL); fb_console_reset(); vga_fb_mirror_refresh();
}

uint32_t *desktop_get_backbuffer(void) { return backbuffer; }
void desktop_get_fb_info(FBInfo *info) { if (info) fb_get_info(info); }
void desktop_refresh(void) {
    uint64_t phys, len; uint32_t pitch, w_fb, h_fb, bpp; fb_get_mmap_region(&phys, &len, &pitch, &w_fb, &h_fb, &bpp);
    uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys; for (uint32_t y = 0; y < h_fb; y++) memcpy(fb_ptr + y * pitch, backbuffer + y * w_fb, w_fb * 4);
}
