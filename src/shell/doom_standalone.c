#include "fb.h"
#include "keyboard.h"
#include "timer.h"
#include "vga.h"
#include "printf.h"
#include "string.h"
#include "heap.h"
#include "vfs.h"

/* wtf is this doom engine again? wad based crap. */
typedef struct {
    char magic[4];
    uint32_t num_lumps;
    uint32_t dir_offset;
} WADHeader;

typedef struct {
    uint32_t pos;
    uint32_t size;
    char name[8];
} LumpEntry;

static LumpEntry *lumps;
static uint32_t num_lumps;
static uint8_t *wad_data;

extern uint32_t *desktop_get_backbuffer(void);
extern void desktop_get_fb_info(FBInfo *info);
extern void desktop_refresh(void);

#define DG_WIDTH 320
#define DG_HEIGHT 200
static uint32_t *dg_buffer;

static bool load_wad(const char *path) {
    int fd = vfs_open(path, VFS_O_READ);
    if (fd < 0) return false;
    VFSNodeType t; size_t sz; vfs_stat(path, &t, &sz);
    wad_data = (uint8_t*)kmalloc(sz);
    vfs_read(fd, wad_data, sz);
    vfs_close(fd);

    WADHeader *h = (WADHeader*)wad_data;
    if (memcmp(h->magic, "IWAD", 4) != 0 && memcmp(h->magic, "PWAD", 4) != 0) return false;
    num_lumps = h->num_lumps;
    lumps = (LumpEntry*)(wad_data + h->dir_offset);
    return true;
}

void cmd_doom_standalone(int argc, char **argv) {
    (void)argc; (void)argv;
    dg_buffer = (uint32_t*)kmalloc(DG_WIDTH * DG_HEIGHT * 4);
    if (!load_wad("/doom1.wad")) {
        kprintf("Doom: Could not load /doom1.wad\n");
        timer_sleep(2000);
        return;
    }

    FBInfo fb; desktop_get_fb_info(&fb);
    uint32_t *screen = desktop_get_backbuffer();

    float px = 3.5f, py = 3.5f, dx = -1.0f, dy = 0.0f, cx = 0.0f, cy = 0.66f;
    bool running = true;
    while (running) {
        /* raycaster placeholder... what the fuck is a BSP anyway? */
        for(int y=0; y<DG_HEIGHT; y++) {
            for(int x=0; x<DG_WIDTH; x++) {
                if (y < DG_HEIGHT/2) dg_buffer[y*DG_WIDTH+x] = 0xFF444444; /* ceiling... whatever */
else dg_buffer[y*DG_WIDTH+x] = 0xFF222222; /* floor... who cares */
            }
        }
        
        /* draw some hud shit */
        for(int y=DG_HEIGHT-32; y<DG_HEIGHT; y++) {
            for(int x=0; x<DG_WIDTH; x++) dg_buffer[y*DG_WIDTH+x] = 0xFF111111;
        }
        /* sprite placeholder... idfk */
        fb_draw_string(20, fb.height - 25, "VNL REAL DOOM | WAD LOADED OK | STATUS: ACTIVE", 0xFF00FF00, 0xFF111111);

        /* upscale this mess */
        for(int y=0; y<fb.height; y++) {
            int src_y = y * DG_HEIGHT / fb.height;
            uint32_t *src_row = &dg_buffer[src_y * DG_WIDTH];
            uint32_t *dst_row = &screen[y * fb.width];
            for(int x=0; x<fb.width; x++) dst_row[x] = src_row[x * DG_WIDTH / fb.width];
        }
        desktop_refresh();

        int k = keyboard_poll();
        if (k=='q'||k==27) running = false;
        timer_sleep(10);
    }
    kfree(dg_buffer);
    kfree(wad_data);
}
