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

/* Include stb headers and math kernel */
#define STB_IMAGE_IMPLEMENTATION
#define STBIDEF static inline __attribute__((always_inline))
#define STBI_MALLOC(sz)           kmalloc(sz)
#define STBI_FREE(p)              kfree(p)
#define STBI_REALLOC(p,newsz)     krealloc(p,newsz)
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_ASSERT(x)
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_DEF static inline __attribute__((always_inline))
#define STBTT_malloc(sz,u)        ((void)(u), kmalloc(sz))
#define STBTT_free(p,u)           ((void)(u), kfree(p))
#define STBTT_assert(x)
#define STBTT_sqrt(x)             sqrt(x)
#define STBTT_pow(x,y)            pow(x,y)
#define STBTT_floor(x)            floor(x)
#define STBTT_ceil(x)             ceil(x)
#define STBTT_fabs(x)             fabs(x)
#define STBTT_fmod(x,y)           fmod(x,y)
#define STBTT_cos(x)              cos(x)
#define STBTT_acos(x)             acos(x)
#include "stb_truetype.h"
#include "math_kernel.h"

uint32_t *desktop_get_backbuffer(void);
void desktop_get_fb_info(FBInfo *info);
void desktop_refresh(void);

static inline int vfs_filesize(int fd) {
    VFSNode *node = vfs_node_from_fd(fd);
    return node ? (int)node->size : 0;
}

/* modern KDE Plasma color palette */
#define VNL_GRAD_START 0xFF1B1437
#define VNL_GRAD_END   0xFF2D1B4E
#define VNL_GLASS      0xBCF5F7FA
#define VNL_SHADOW     0x50000000
#define VNL_SURFACE    0xEEF8F8FA
#define VNL_TITLE      0xEE0D5C97
#define VNL_ACCENT     0xFFFF851B
#define VNL_DKSHAD     0xCC1A1A24
#define VNL_WHITE      0xFFFFFFFF
#define VNL_GLOW       0x400D5C97
#define VNL_DARK_BLUE  0xFF0A0C14

#define TASKBAR_HEIGHT 44
#define CURSOR_W 12
#define CURSOR_H 19

#define TERM_COLS 60
#define TERM_ROWS 25
#define TERM_BUF_SIZE (TERM_COLS * TERM_ROWS)

extern void cmd_neovim_vnl(int argc, char **argv);
extern void cmd_htop_gui(int argc, char **argv);
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

/* Font system state */
static unsigned char *font_file_data = NULL;
static stbtt_fontinfo font_info;
static bool font_loaded = false;

/* TTF Glyph cache — render each glyph once, blit from cache */
#define GLYPH_CACHE_SIZE 128
#define MAX_GLYPH_SIZES 3
typedef struct {
    unsigned char *bitmap;
    int w, h, xoff, yoff, advance;
    bool valid;
} CachedGlyph;
static CachedGlyph glyph_cache[MAX_GLYPH_SIZES][GLYPH_CACHE_SIZE];
static float cached_scales[MAX_GLYPH_SIZES] = {0};
static int cached_baselines[MAX_GLYPH_SIZES] = {0};

/* Wallpaper system state */
static unsigned char *wallpaper_img_data = NULL;
static int wall_w = 0, wall_h = 0, wall_comp = 0;
static uint32_t *wallpaper_scaled = NULL; /* pre-scaled to framebuffer size */

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
    uint32_t alpha = (fg >> 24) & 0xFF; 
    if (alpha == 255) return fg; 
    if (alpha == 0) return bg;
    uint32_t r_bg = (bg >> 16) & 0xFF, g_bg = (bg >> 8) & 0xFF, b_bg = bg & 0xFF;
    uint32_t r_fg = (fg >> 16) & 0xFF, g_fg = (fg >> 8) & 0xFF, b_fg = fg & 0xFF;
    uint32_t r = (r_fg * alpha + r_bg * (255 - alpha)) >> 8;
    uint32_t g = (g_fg * alpha + g_bg * (255 - alpha)) >> 8;
    uint32_t b = (b_fg * alpha + b_bg * (255 - alpha)) >> 8;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

/* Integer-only color interpolation: t is 0..256 (fixed-point 8.8) */
static uint32_t lerp_color_int(uint32_t c1, uint32_t c2, int t256) {
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int r = r1 + (((r2 - r1) * t256) >> 8);
    int g = g1 + (((g2 - g1) * t256) >> 8);
    int b = b1 + (((b2 - b1) * t256) >> 8);
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static inline void bb_plot(int x, int y, uint32_t c) {
    if ((unsigned)x >= fb_info.width || (unsigned)y >= fb_info.height) return;
    uint32_t alpha = (c >> 24) & 0xFF;
    if (alpha == 255) { backbuffer[y * fb_info.width + x] = c; return; }
    if (alpha == 0) return;
    backbuffer[y * fb_info.width + x] = blend(backbuffer[y * fb_info.width + x], c);
}

static void bb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    /* Clip to framebuffer bounds */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)fb_info.width ? (int)fb_info.width : x + w;
    int y1 = y + h > (int)fb_info.height ? (int)fb_info.height : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t alpha = (c >> 24) & 0xFF;
    if (alpha == 255) {
        /* Fast opaque fill — direct write, no blend */
        for (int r = y0; r < y1; r++) {
            uint32_t *row = backbuffer + r * fb_info.width;
            for (int col = x0; col < x1; col++) row[col] = c;
        }
    } else if (alpha > 0) {
        for (int r = y0; r < y1; r++) {
            uint32_t *row = backbuffer + r * fb_info.width;
            for (int col = x0; col < x1; col++) row[col] = blend(row[col], c);
        }
    }
}

static void draw_gradient_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2, bool vertical) {
    if (vertical) {
        int denom = h > 1 ? h - 1 : 1;
        for (int r = 0; r < h; r++) {
            int t256 = (r * 256) / denom;
            uint32_t color = lerp_color_int(c1, c2, t256);
            int ry = y + r;
            if ((unsigned)ry >= fb_info.height) continue;
            uint32_t *row = backbuffer + ry * fb_info.width;
            int x0 = x < 0 ? 0 : x;
            int x1 = x + w > (int)fb_info.width ? (int)fb_info.width : x + w;
            for (int col = x0; col < x1; col++) row[col] = color;
        }
    } else {
        int denom = w > 1 ? w - 1 : 1;
        for (int r = 0; r < h; r++) {
            int ry = y + r;
            if ((unsigned)ry >= fb_info.height) continue;
            uint32_t *row = backbuffer + ry * fb_info.width;
            for (int col = 0; col < w; col++) {
                int t256 = (col * 256) / denom;
                uint32_t color = lerp_color_int(c1, c2, t256);
                int cx = x + col;
                if ((unsigned)cx < fb_info.width) row[cx] = color;
            }
        }
    }
}

static void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color) {
    /* Integer-only rounded rect: uses dx*dx + dy*dy <= r*r instead of sqrt */
    int r2 = radius * radius;
    uint32_t alpha = (color >> 24) & 0xFF;
    if (alpha == 0) return;
    bool opaque = (alpha == 255);

    for (int row = 0; row < h; row++) {
        int py = y + row;
        if ((unsigned)py >= fb_info.height) continue;
        uint32_t *fb_row = backbuffer + py * fb_info.width;

        /* Determine horizontal span for this row */
        int xs = 0, xe = w;
        bool in_top = (row < radius);
        bool in_bot = (row >= h - radius);

        if (in_top || in_bot) {
            int cy = in_top ? (radius - 1) : (h - radius);
            int dy = row - cy;
            int dy2 = dy * dy;
            /* Find leftmost and rightmost visible columns in the corner arcs */
            /* Skip pixels outside the circle on both ends */
            int left_start = 0;
            while (left_start < radius) {
                int dx = left_start - (radius - 1);
                if (dx * dx + dy2 <= r2) break;
                left_start++;
            }
            int right_end = w;
            while (right_end > w - radius) {
                int dx = (right_end - 1) - (w - radius);
                if (dx * dx + dy2 <= r2) break;
                right_end--;
            }
            xs = left_start;
            xe = right_end;
        }
        /* Clip to framebuffer */
        int px0 = x + xs;
        int px1 = x + xe;
        if (px0 < 0) px0 = 0;
        if (px1 > (int)fb_info.width) px1 = (int)fb_info.width;

        if (opaque) {
            for (int px = px0; px < px1; px++) fb_row[px] = color;
        } else {
            for (int px = px0; px < px1; px++) fb_row[px] = blend(fb_row[px], color);
        }
    }
}

static void draw_window_shadow(int x, int y, int w, int h) {
    /* 2 shadow passes instead of 6 — much faster */
    bb_fill_rect(x + 4, y + 4, w, h, 0x18000000);
    bb_fill_rect(x + 2, y + 2, w, h, 0x10000000);
}

static void bb_draw_string(int x, int y, const char *s, uint32_t fg) { 
    if (!s) return; 
    int cx = x; 
    while (*s) { 
        int idx = glyph_index(*s); 
        const uint8_t *gp = g_vnl_fonts[idx]; 
        for (int yy = 0; yy < 8; yy++) { 
            uint8_t bits = gp[yy]; 
            for (int xx = 0; xx < 8; xx++) { 
                if (bits & (1 << (7 - xx))) bb_plot(cx + xx, y + yy, fg); 
            } 
        } 
        cx += 8; 
        s++; 
    } 
}

/* TTF Fonts implementation */
static void init_desktop_font(void) {
    if (font_loaded) return;
    int fd = vfs_open("/etc/font.ttf", VFS_O_READ);
    if (fd >= 0) {
        int size = vfs_filesize(fd);
        if (size > 0) {
            font_file_data = (unsigned char *)kmalloc(size);
            vfs_read(fd, font_file_data, size);
            if (stbtt_InitFont(&font_info, font_file_data, 0)) {
                font_loaded = true;
            }
        }
        vfs_close(fd);
    }
}

/* Map pixel height to cache slot index */
static int get_cache_slot(int pixel_height) {
    if (pixel_height <= 11) return 0;
    if (pixel_height <= 12) return 1;
    return 2;
}

static void ensure_glyph_cached(int slot, char ch) {
    if (!font_loaded) return;
    int idx = (unsigned char)ch;
    if (idx >= GLYPH_CACHE_SIZE) return;
    CachedGlyph *g = &glyph_cache[slot][idx];
    if (g->valid) return;
    
    float scale = cached_scales[slot];
    int w, h, xoff, yoff;
    unsigned char *bitmap = stbtt_GetCodepointBitmap(&font_info, 0, scale, ch, &w, &h, &xoff, &yoff);
    g->bitmap = bitmap;
    g->w = w; g->h = h; g->xoff = xoff; g->yoff = yoff;
    int advanceWidth, lsb;
    stbtt_GetCodepointHMetrics(&font_info, ch, &advanceWidth, &lsb);
    g->advance = (int)(advanceWidth * scale);
    g->valid = true;
}

static void init_cache_slot(int pixel_height) {
    int slot = get_cache_slot(pixel_height);
    if (cached_scales[slot] != 0) return; /* already initialized */
    float scale = stbtt_ScaleForPixelHeight(&font_info, (float)pixel_height);
    cached_scales[slot] = scale;
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &lineGap);
    cached_baselines[slot] = (int)(ascent * scale);
}

static void draw_ttf_string(int x, int y, const char *text, int pixel_height, uint32_t color) {
    if (!font_loaded) {
        bb_draw_string(x, y - 6, text, color);
        return;
    }
    int slot = get_cache_slot(pixel_height);
    init_cache_slot(pixel_height);
    int baseline = cached_baselines[slot];
    uint8_t base_a = (color >> 24) & 0xFF;
    uint32_t rgb = color & 0x00FFFFFF;
    
    int cx = x;
    while (*text) {
        char ch = *text;
        int idx = (unsigned char)ch;
        if (idx < GLYPH_CACHE_SIZE) {
            ensure_glyph_cached(slot, ch);
            CachedGlyph *g = &glyph_cache[slot][idx];
            if (g->bitmap) {
                int gx = cx + g->xoff;
                int gy = y + baseline + g->yoff;
                for (int r = 0; r < g->h; r++) {
                    int py = gy + r;
                    if ((unsigned)py >= fb_info.height) continue;
                    uint32_t *fb_row = backbuffer + py * fb_info.width;
                    unsigned char *src_row = g->bitmap + r * g->w;
                    for (int c = 0; c < g->w; c++) {
                        uint8_t alpha = src_row[c];
                        if (alpha > 0) {
                            int px = gx + c;
                            if ((unsigned)px < fb_info.width) {
                                uint32_t fa = (base_a * alpha) / 255;
                                fb_row[px] = blend(fb_row[px], (fa << 24) | rgb);
                            }
                        }
                    }
                }
            }
            cx += g->advance;
        }
        text++;
    }
}

static void draw_ttf_string_shadow(int x, int y, const char *text, int pixel_height, uint32_t color) {
    draw_ttf_string(x + 1, y + 1, text, pixel_height, 0xAA000000);
    draw_ttf_string(x, y, text, pixel_height, color);
}

/* Wallpaper implementation */
static void load_wallpaper(void) {
    if (wallpaper_img_data) return;
    int fd = vfs_open("/etc/wallpaper.png", VFS_O_READ);
    if (fd >= 0) {
        int size = vfs_filesize(fd);
        if (size > 0) {
            unsigned char *temp_buf = (unsigned char *)kmalloc(size);
            vfs_read(fd, temp_buf, size);
            wallpaper_img_data = stbi_load_from_memory(temp_buf, size, &wall_w, &wall_h, &wall_comp, 4);
            kfree(temp_buf);
        }
        vfs_close(fd);
    }
}

static void draw_wallpaper(void) {
    load_wallpaper();
    if (wallpaper_img_data) {
        /* Pre-scale wallpaper once, then memcpy every frame */
        if (!wallpaper_scaled) {
            wallpaper_scaled = (uint32_t *)kmalloc(fb_info.width * fb_info.height * 4);
            if (wallpaper_scaled) {
                for (int y = 0; y < (int)fb_info.height; y++) {
                    int src_y = (y * wall_h) / fb_info.height;
                    uint32_t *dest_row = wallpaper_scaled + y * fb_info.width;
                    unsigned char *src_row = wallpaper_img_data + src_y * wall_w * 4;
                    for (int x = 0; x < (int)fb_info.width; x++) {
                        int src_x = (x * wall_w) / fb_info.width;
                        unsigned char *pixel = src_row + src_x * 4;
                        dest_row[x] = 0xFF000000 | (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
                    }
                }
            }
        }
        if (wallpaper_scaled) {
            memcpy(backbuffer, wallpaper_scaled, fb_info.width * fb_info.height * 4);
        }
    } else {
        draw_gradient_rect(0, 0, fb_info.width, fb_info.height, VNL_GRAD_START, VNL_GRAD_END, true);
    }
}

/* PNG Icon Loader */
static unsigned char *load_png_icon(const char *path, int *w, int *h) {
    int fd = vfs_open(path, VFS_O_READ);
    if (fd < 0) return NULL;
    int size = vfs_filesize(fd);
    if (size <= 0) { vfs_close(fd); return NULL; }
    unsigned char *temp_buf = (unsigned char *)kmalloc(size);
    vfs_read(fd, temp_buf, size);
    int comp;
    unsigned char *img = stbi_load_from_memory(temp_buf, size, w, h, &comp, 4);
    kfree(temp_buf);
    vfs_close(fd);
    return img;
}

static void draw_png_icon(int x, int y, unsigned char *img, int w, int h) {
    if (!img) return;
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            unsigned char *pixel = img + (r * w + c) * 4;
            uint32_t color = (pixel[3] << 24) | (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
            bb_plot(x + c, y + r, color);
        }
    }
}

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

static void bb_draw_cursor(int x, int y) { 
    /* Draw drop shadow */
    for (int r = 0; r < CURSOR_H; r++) { 
        const char *row_str = cursor_shape[r]; 
        for (int c = 0; row_str[c] && c < CURSOR_W; c++) { 
            char ch = row_str[c]; 
            if (ch == ' ') continue; 
            bb_plot(x + c + 3, y + r + 3, 0x40000000); 
        } 
    } 
    /* Draw cursor foreground */
    for (int r = 0; r < CURSOR_H; r++) { 
        const char *row_str = cursor_shape[r]; 
        for (int c = 0; row_str[c] && c < CURSOR_W; c++) { 
            char ch = row_str[c]; 
            if (ch == ' ') continue; 
            uint32_t color = (ch == 'W') ? 0xFFFFFFFF : 0xFF000000; 
            bb_plot(x + c, y + r, color); 
        } 
    } 
}

Window *window_create(WindowKind kind, int x, int y, int w, int h, const char *title) {
    Window *win = (Window *)kmalloc(sizeof(Window)); 
    memset(win, 0, sizeof(Window));
    win->kind = kind; win->x = x; win->y = y; win->w = w; win->h = h; win->title = title;
    if (kind == WIN_TERM) {
        for(int i=0; i<TERM_BUF_SIZE; i++) win->term_buffer[i] = (uint16_t)(' ' | (VGA_WHITE << 8));
    } else if (kind == WIN_BROWSER) {
        strncpy(win->url, "vibe://home", 127); win->url_idx = strlen(win->url);
        strncpy(win->content, "Welcome to VibeWeb!\n\nFeatured Sites:\n- vibe://vnl (Official)\n- vibe://osdev (Resources)\n\nVNL - The vibe that never dies.", 1023);
    } else if (kind == WIN_EXPLORER) { 
        strncpy(win->path, "/", 127); 
    }
    win->next = windows; 
    windows = win; 
    focused_win = win; 
    return win;
}

static void browser_navigate(Window *win) {
    char u[128]; strncpy(u, win->url, 127);
    for(int i=0; u[i]; i++) if(u[i]>='A'&&u[i]<='Z') u[i]+=32;
    if (strcmp(u, "vibe://vnl") == 0) strncpy(win->content, "Vibe Not Linux (VNL)\n------------------\nAn agentic low-level OS designed\nfor peak vibes and high performance.\n\nKernel: 1.2.2 (Stable)\nArch: x86_64\n\n- vibe://home (Back)", 1023);
    else if (strcmp(u, "vibe://home") == 0) strncpy(win->content, "Welcome to VibeWeb!\n\nFeatured Sites:\n- vibe://vnl (Official)\n- vibe://osdev (Resources)\n\nVNL - The vibe that never dies.", 1023);
    else if (strstr(u, "google.com") || strstr(u, "vibe://search") || strlen(u) < 15) strncpy(win->content, "G O O G L E\n-----------\nSearch: [                        ] [Search]\n\nResults:\n1. VNL RTL8139: [ONLINE]\n2. VNL Graphics: [STABLE]\n- vibe://home (Home)", 1023);
    else strncpy(win->content, "404 Not Found\n-------------\nThat vibe doesn't exist yet.\n\n- vibe://home (Home)", 1023);
}

void desktop_run(void) {
    if (!fb_is_available()) return;
    fb_get_info(&fb_info);
    if (!backbuffer) backbuffer = (uint32_t *)kmalloc(fb_info.width * fb_info.height * 4);
    if (!backbuffer) return;

    /* Initialize fonts and resources */
    init_desktop_font();

    typedef struct {
        int x, y, w, h;
        const char *label;
        const char *binary_path;
        void (*action)(int argc, char **argv);
        uint32_t icon_color;
        const char *png_path;
        unsigned char *loaded_img;
        int img_w, img_h;
    } DesktopIcon;

    DesktopIcon icons_master[] = {
        {30, 30, 64, 64, "Files", NULL, NULL, 0xFFCCCCCC, "/etc/icon_files.png", NULL, 0, 0},
        {30, 110, 64, 64, "Shell", NULL, NULL, 0xFF444444, "/etc/icon_shell.png", NULL, 0, 0},
        {30, 190, 64, 64, "VibeNet", NULL, NULL, 0xFF0000AA, "/etc/icon_vibenet.png", NULL, 0, 0},
        {120, 110, 64, 64, "Snake", NULL, NULL, 0xFF0074D9, "/etc/icon_snake.png", NULL, 0, 0},
        {120, 190, 64, 64, "NeoVim", NULL, NULL, 0xFFFF851B, "/etc/icon_neovim.png", NULL, 0, 0}
    };
    DesktopIcon active_icons[10]; 
    int active_count = 0;
    for (int i = 0; i < (int)(sizeof(icons_master)/sizeof(icons_master[0])); i++) {
        if (!icons_master[i].binary_path || vfs_resolve(icons_master[i].binary_path) >= 0) {
            active_icons[active_count++] = icons_master[i];
        }
    }

    /* Load all active icon images */
    for (int i = 0; i < active_count; i++) {
        if (active_icons[i].png_path) {
            active_icons[i].loaded_img = load_png_icon(active_icons[i].png_path, &active_icons[i].img_w, &active_icons[i].img_h);
        }
    }

    bool exit_desktop = false, start_menu_open = false;
    int mx = fb_info.width / 2, my = fb_info.height / 2; 
    uint8_t prev_buttons = 0; 
    int selected_icon = -1;
    
    mouse_flush(); 
    vga_set_hook(vga_universal_hook);

    while (!exit_desktop) {
        /* 1. Wallpaper */
        draw_wallpaper();

        /* grid pattern (subtle premium look) */
        for(int y=0; y<(int)fb_info.height; y+=128) {
            for(int x=0; x<(int)fb_info.width; x+=128) {
                bb_fill_rect(x, y, 4, 4, 0x10FFFFFF);
            }
        }

        /* 2. Floating Frosted Taskbar (no blur — solid translucent fill) */
        draw_rounded_rect_filled(8, fb_info.height - TASKBAR_HEIGHT + 4, fb_info.width - 16, TASKBAR_HEIGHT - 8, 8, 0xC01A1A24);
        
        bool start_hover = (mx >= 16 && mx < 112 && my >= (int)fb_info.height - TASKBAR_HEIGHT + 6 && my < (int)fb_info.height - 6);
        draw_rounded_rect_filled(16, fb_info.height - TASKBAR_HEIGHT + 6, 96, TASKBAR_HEIGHT - 12, 6, start_menu_open ? VNL_ACCENT : (start_hover ? VNL_TITLE : 0x30FFFFFF));
        draw_ttf_string(36, fb_info.height - TASKBAR_HEIGHT + 14, "START", 13, VNL_WHITE);

        /* 3. Desktop Icons */
        for (int i = 0; i < active_count; i++) {
            if (active_icons[i].loaded_img) {
                draw_png_icon(active_icons[i].x + 8, active_icons[i].y + 8, active_icons[i].loaded_img, active_icons[i].img_w, active_icons[i].img_h);
            } else {
                bb_fill_rect(active_icons[i].x + 16, active_icons[i].y, 32, 32, active_icons[i].icon_color);
            }
            int lx = active_icons[i].x + (64 - (strlen(active_icons[i].label) * 7)) / 2;
            if (selected_icon == i) { 
                draw_rounded_rect_filled(lx - 4, active_icons[i].y + 35, (strlen(active_icons[i].label) * 7) + 8, 14, 4, VNL_ACCENT); 
            } else {
                draw_rounded_rect_filled(lx - 4, active_icons[i].y + 35, (strlen(active_icons[i].label) * 7) + 8, 14, 4, 0x40000000); 
            }
            draw_ttf_string_shadow(lx, active_icons[i].y + 36, active_icons[i].label, 12, VNL_WHITE);
        }

        /* 4. Windows */
        Window *win = windows;
        while (win) {
            if (win->minimized) { win = win->next; continue; }
            bool active = (win == focused_win);
            
            /* Window Shadow */
            draw_window_shadow(win->x, win->y - 24, win->w, win->h + 24);
            
            /* Window Body & Frame (Rounded) */
            // Title bar background
            draw_rounded_rect_filled(win->x, win->y - 24, win->w, 24, 8, active ? 0xEE0B5C97 : 0xEE2A2A2A);
            bb_fill_rect(win->x, win->y - 12, win->w, 12, active ? 0xEE0B5C97 : 0xEE2A2A2A);
            
            // Window content surface
            draw_rounded_rect_filled(win->x, win->y, win->w, win->h, 8, 0xE5F8F8FA);
            bb_fill_rect(win->x, win->y, win->w, 12, 0xE5F8F8FA);
            
            /* Removed per-frame blur — too expensive in software */
            
            // Window Title & Close Button
            draw_ttf_string_shadow(win->x + 12, win->y - 18, win->title, 12, VNL_WHITE);
            draw_rounded_rect_filled(win->x + win->w - 24, win->y - 20, 18, 16, 4, 0xFFE03030);
            draw_ttf_string(win->x + win->w - 18, win->y - 17, "x", 11, VNL_WHITE);
            
            if (win->kind == WIN_TERM) {
                bb_fill_rect(win->x, win->y, win->w, win->h, 0xFF06070A);
                for (int r = 0; r < TERM_ROWS; r++) {
                    for (int c = 0; c < TERM_COLS; c++) {
                        uint16_t entry = win->term_buffer[r * TERM_COLS + c];
                        char ch = (char)(entry & 0xFF); 
                        uint8_t color = (uint8_t)(entry >> 8);
                        if (ch != ' ') {
                            int idx = glyph_index(ch); 
                            const uint8_t *gp = g_vnl_fonts[idx]; 
                            uint32_t fg = vga_to_32bpp(color);
                            for (int yy = 0; yy < 8; yy++) { 
                                uint8_t bits = gp[yy]; 
                                for (int xx = 0; xx < 8; xx++) { 
                                    if (bits & (1 << (7 - xx))) bb_plot(win->x + 8 + c * 8 + xx, win->y + 8 + r * 11 + yy, fg); 
                                } 
                            }
                        }
                    }
                }
                if (active) {
                    bb_fill_rect(win->x + 8 + win->term_cursor_x * 8, win->y + 8 + win->term_cursor_y * 11, 8, 10, 0x80FFFFFF);
                    if (win->term_idx > 0) { 
                        bb_draw_string(win->x + 8, win->y + win->h - 15, "> ", 0xFFFFFF00); 
                        bb_draw_string(win->x + 24, win->y + win->h - 15, win->term_cmd, VNL_WHITE); 
                    }
                }
            } else if (win->kind == WIN_BROWSER) {
                bb_fill_rect(win->x, win->y, win->w, 36, 0x00000000);
                draw_rounded_rect_filled(win->x + 12, win->y + 8, win->w - 80, 20, 6, VNL_WHITE);
                draw_ttf_string(win->x + 20, win->y + 12, win->url, 11, VNL_DKSHAD);
                
                if (active) {
                    bb_fill_rect(win->x + 20 + win->url_idx * 7, win->y + 10, 2, 16, VNL_TITLE);
                }
                
                draw_rounded_rect_filled(win->x + win->w - 60, win->y + 8, 48, 20, 6, VNL_TITLE);
                draw_ttf_string(win->x + win->w - 48, win->y + 12, "GO", 11, VNL_WHITE);
                
                int cy = win->y + 45; 
                const char *p = win->content;
                while (*p) {
                    const char *ls = p; 
                    while (*p && *p != '\n') p++;
                    size_t llen = (size_t)(p - ls); 
                    char lb[128]; 
                    if(llen>=128) llen=127; 
                    memcpy(lb, ls, llen); 
                    lb[llen]='\0';
                    draw_ttf_string(win->x + 15, cy, lb, 12, (lb[0] == '-') ? VNL_TITLE : VNL_DKSHAD); 
                    cy += 15; 
                    if (*p == '\n') p++;
                }
            } else if (win->kind == WIN_EXPLORER) {
                bb_fill_rect(win->x, win->y, win->w, 24, 0x00000000);
                draw_ttf_string(win->x + 10, win->y + 5, win->path, 11, VNL_DKSHAD);
                
                char names[32][VFS_NAME_MAX]; 
                int n = vfs_readdir(win->path, names, 32);
                for(int i=0; i<n; i++) { 
                    draw_rounded_rect_filled(win->x + 15, win->y + 40 + i * 22, 18, 14, 3, 0xFFFFB900); 
                    draw_ttf_string(win->x + 40, win->y + 41, names[i], 12, VNL_DKSHAD); 
                }
            }
            win = win->next;
        }

        /* 5. Start Menu (Frosted Glass) */
        if (start_menu_open) {
            draw_rounded_rect_filled(8, fb_info.height - TASKBAR_HEIGHT - 320, 240, 310, 12, 0xEE1A1A24);
            
            draw_ttf_string_shadow(24, fb_info.height - TASKBAR_HEIGHT - 295, "VNL WORKSTATION", 13, VNL_ACCENT);
            bb_fill_rect(20, fb_info.height - TASKBAR_HEIGHT - 275, 200, 1, 0x20FFFFFF);
            
            draw_ttf_string(32, fb_info.height - TASKBAR_HEIGHT - 250, "Programs", 12, VNL_WHITE);
            draw_ttf_string(32, fb_info.height - TASKBAR_HEIGHT - 225, "File Explorer", 12, VNL_WHITE);
            draw_ttf_string(32, fb_info.height - TASKBAR_HEIGHT - 200, "Terminal", 12, VNL_WHITE);
            draw_ttf_string(32, fb_info.height - TASKBAR_HEIGHT - 175, "VibeNet Pro", 12, VNL_WHITE);
            
            bb_fill_rect(20, fb_info.height - TASKBAR_HEIGHT - 95, 200, 1, 0x20FFFFFF);
            draw_ttf_string(24, fb_info.height - TASKBAR_HEIGHT - 75, "SHUT DOWN", 12, 0xFFE03030);
        }

        /* 6. Mouse Cursor */
        bb_draw_cursor(mx, my);

        /* Blit backbuffer to real framebuffer (Optimized) */
        uint64_t phys, len; 
        uint32_t pitch, w_fb, h_fb, bpp; 
        fb_get_mmap_region(&phys, &len, &pitch, &w_fb, &h_fb, &bpp);
        uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys; 
        if (pitch == w_fb * 4) {
            memcpy(fb_ptr, backbuffer, w_fb * h_fb * 4);
        } else {
            for (uint32_t y = 0; y < h_fb; y++) {
                memcpy(fb_ptr + y * pitch, backbuffer + y * w_fb, w_fb * 4);
            }
        }

        /* Input polling & logic */
        int k = keyboard_poll(); 
        if (k == 27) break;
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

        int dx, dy; 
        uint8_t buttons = prev_buttons;
        bool polled = false;
        while (mouse_poll(&dx, &dy, &buttons)) {
            mx += dx; my -= dy; 
            polled = true;
        }
        if (polled) {
            if (mx < 0) mx = 0; 
            if (mx >= (int)fb_info.width) mx = (int)fb_info.width - 1; 
            if (my < 0) my = 0; 
            if (my >= (int)fb_info.height) my = (int)fb_info.height - 1;
            
            if ((buttons & 1) && !(prev_buttons & 1)) {
                if (start_hover) {
                    start_menu_open = !start_menu_open;
                } else if (start_menu_open && mx >= 8 && mx < 248 && my >= (int)fb_info.height - TASKBAR_HEIGHT - 320 && my < (int)fb_info.height - TASKBAR_HEIGHT) {
                    int menu_top = (int)fb_info.height - TASKBAR_HEIGHT - 320;
                    int item_y = my - menu_top;
                    if (item_y >= 60 && item_y < 85) { /* Programs */ }
                    else if (item_y >= 85 && item_y < 110) { window_create(WIN_EXPLORER, 80, 80, 350, 450, "File Explorer"); }
                    else if (item_y >= 110 && item_y < 135) { window_create(WIN_TERM, 100, 100, 500, 350, "VNL Command Prompt"); }
                    else if (item_y >= 135 && item_y < 160) { window_create(WIN_BROWSER, 150, 120, 550, 400, "VibeNet Pro"); }
                    else if (item_y >= 240 && item_y < 265) { exit_desktop = true; }
                    start_menu_open = false;
                } else {
                    start_menu_open = false; 
                    Window *clicked_win = NULL, *curr = windows;
                    while (curr) {
                        // Close button click check
                        if (mx >= curr->x + curr->w - 24 && mx < curr->x + curr->w - 6 && my >= curr->y - 20 && my < curr->y - 4) {
                            if (windows == curr) windows = curr->next; 
                            else { 
                                Window *p = windows; 
                                while (p->next != curr) p = p->next; 
                                p->next = curr->next; 
                            }
                            if (focused_win == curr) focused_win = windows; 
                            kfree(curr); 
                            break;
                        }
                        // Drag check on title bar
                        if (mx >= curr->x && mx < curr->x + curr->w && my >= curr->y - 24 && my < curr->y) { 
                            clicked_win = curr; 
                            curr->dragging = true; 
                            curr->drag_off_x = mx - curr->x; 
                            curr->drag_off_y = my - curr->y; 
                            break; 
                        }
                        // Body focus check
                        else if (mx >= curr->x && mx < curr->x + curr->w && my >= curr->y && my < curr->y + curr->h) {
                            clicked_win = curr; 
                            if (curr->kind == WIN_BROWSER && mx >= curr->x + curr->w - 56 && mx < curr->x + curr->w - 12 && my >= curr->y + 8 && my < curr->y + 28) {
                                browser_navigate(curr);
                            }
                            break;
                        }
                        curr = curr->next;
                    }
                    if (clicked_win) {
                        focused_win = clicked_win;
                        if (clicked_win->next) {
                            if (windows == clicked_win) windows = clicked_win->next; 
                            else { 
                                Window *p = windows; 
                                while (p->next != clicked_win) p = p->next; 
                                p->next = clicked_win->next; 
                            }
                            Window *t = windows; 
                            while (t->next) t = t->next; 
                            t->next = clicked_win; 
                            clicked_win->next = NULL;
                        }
                    } else {
                        selected_icon = -1;
                        for (int i = 0; i < active_count; i++) {
                            if (mx >= active_icons[i].x && mx < active_icons[i].x + 64 && my >= active_icons[i].y && my < active_icons[i].y + 64) {
                                selected_icon = i;
                                if (active_icons[i].action) { 
                                    fb_console_reset(); 
                                    active_icons[i].action(0, NULL); 
                                    mouse_flush(); 
                                }
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
                    curr->dragging = false; 
                    curr = curr->next;
                }
            }
            if (buttons & 1) { 
                Window *curr = windows; 
                while (curr) { 
                    if (curr->dragging) { 
                        curr->x = mx - curr->drag_off_x; 
                        curr->y = my - curr->drag_off_y; 
                    } 
                    curr = curr->next; 
                } 
            }
            prev_buttons = buttons;
        }
        timer_sleep(16);
    }
    
    // Free icon memory
    for (int i = 0; i < active_count; i++) {
        if (active_icons[i].loaded_img) {
            stbi_image_free(active_icons[i].loaded_img);
        }
    }

    vga_set_hook(NULL); 
    fb_console_reset(); 
    vga_fb_mirror_refresh();
}

uint32_t *desktop_get_backbuffer(void) { return backbuffer; }
void desktop_get_fb_info(FBInfo *info) { if (info) fb_get_info(info); }
void desktop_refresh(void) {
    uint64_t phys, len; 
    uint32_t pitch, w_fb, h_fb, bpp; 
    fb_get_mmap_region(&phys, &len, &pitch, &w_fb, &h_fb, &bpp);
    uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys; 
    for (uint32_t y = 0; y < h_fb; y++) {
        memcpy(fb_ptr + y * pitch, backbuffer + y * w_fb, w_fb * 4);
    }
}
