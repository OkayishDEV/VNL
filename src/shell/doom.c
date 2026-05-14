#include "fb.h"
#include "keyboard.h"
#include "timer.h"
#include "vga.h"
#include "printf.h"
#include "string.h"
#include "heap.h"


#define MAP_WIDTH  16
#define MAP_HEIGHT 16

static int world_map[MAP_WIDTH][MAP_HEIGHT] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,0,0,0,0,1,1,1,0,0,0,1},
    {1,0,1,1,0,0,0,0,0,1,0,1,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,1},
    {1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,1,0,1,0,0,0,0,1,1,0,1},
    {1,0,0,0,0,1,1,1,0,0,0,0,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,0,1,0,0,0,1,1,1,0,0,0,0,1},
    {1,0,1,1,1,0,0,0,1,0,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

static float player_x = 2.0f, player_y = 2.0f;
static float player_dir_x = -1.0f, player_dir_y = 0.0f;
static float plane_x = 0.0f, plane_y = 0.66f;

void cmd_doom_real(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!fb_is_available()) { kprintf("Doom requires linear framebuffer.\n"); return; }
    
    FBInfo info; fb_get_info(&info);
    uint32_t *fb = (uint32_t *)kmalloc(info.width * info.height * 4);
    if (!fb) return;

    bool running = true;
    while (running) {
        /* Render */
        for (uint32_t x = 0; x < info.width; x++) {
            float camera_x = 2 * x / (float)info.width - 1;
            float ray_dir_x = player_dir_x + plane_x * camera_x;
            float ray_dir_y = player_dir_y + plane_y * camera_x;

            int map_x = (int)player_x, map_y = (int)player_y;
            float side_dist_x, side_dist_y;
            float delta_dist_x = (ray_dir_x == 0) ? 1e30f : (1.0f / (ray_dir_x < 0 ? -ray_dir_x : ray_dir_x));
            float delta_dist_y = (ray_dir_y == 0) ? 1e30f : (1.0f / (ray_dir_y < 0 ? -ray_dir_y : ray_dir_y));
            float perp_wall_dist;

            int step_x, step_y;
            int hit = 0, side;

            if (ray_dir_x < 0) { step_x = -1; side_dist_x = (player_x - map_x) * delta_dist_x; }
            else { step_x = 1; side_dist_x = (map_x + 1.0f - player_x) * delta_dist_x; }
            if (ray_dir_y < 0) { step_y = -1; side_dist_y = (player_y - map_y) * delta_dist_y; }
            else { step_y = 1; side_dist_y = (map_y + 1.0f - player_y) * delta_dist_y; }

            while (hit == 0) {
                if (side_dist_x < side_dist_y) { side_dist_x += delta_dist_x; map_x += step_x; side = 0; }
                else { side_dist_y += delta_dist_y; map_y += step_y; side = 1; }
                if (world_map[map_x][map_y] > 0) hit = 1;
            }

            if (side == 0) perp_wall_dist = (side_dist_x - delta_dist_x);
            else           perp_wall_dist = (side_dist_y - delta_dist_y);

            int line_height = (int)(info.height / perp_wall_dist);
            int draw_start = -line_height / 2 + info.height / 2;
            if (draw_start < 0) draw_start = 0;
            int draw_end = line_height / 2 + info.height / 2;
            if (draw_end >= (int)info.height) draw_end = info.height - 1;

            uint32_t color = (side == 1) ? 0xFFAAAAAA : 0xFFDDDDDD;
            if (world_map[map_x][map_y] == 1) { /* wall color */ }

            for (int y = 0; y < (int)info.height; y++) {
                if (y < draw_start) fb[y * info.width + x] = 0xFF333333;
                else if (y <= draw_end) fb[y * info.width + x] = color;
                else fb[y * info.width + x] = 0xFF111111;
            }
        }

        /* Draw FB */
        uint64_t phys, len; uint32_t pitch, w, h, bpp; fb_get_mmap_region(&phys, &len, &pitch, &w, &h, &bpp);
        uint8_t *fb_ptr = (uint8_t *)(uintptr_t)phys;
        for (uint32_t y = 0; y < info.height; y++) memcpy(fb_ptr + y * pitch, fb + y * info.width, info.width * 4);

        /* Input */
        int k = keyboard_poll();
        if (k == 'q' || k == 27) running = false;
        float move_speed = 0.1f, rot_speed = 0.05f;
        if (k == 'w') { if (world_map[(int)(player_x + player_dir_x * move_speed)][(int)player_y] == 0) player_x += player_dir_x * move_speed; if (world_map[(int)player_x][(int)(player_y + player_dir_y * move_speed)] == 0) player_y += player_dir_y * move_speed; }
        if (k == 's') { if (world_map[(int)(player_x - player_dir_x * move_speed)][(int)player_y] == 0) player_x -= player_dir_x * move_speed; if (world_map[(int)player_x][(int)(player_y - player_dir_y * move_speed)] == 0) player_y -= player_dir_y * move_speed; }
        if (k == 'd') {
            float old_dir_x = player_dir_x;
            player_dir_x = player_dir_x * 0.9987f - player_dir_y * -0.0499f;
            player_dir_y = old_dir_x * -0.0499f + player_dir_y * 0.9987f;
            float old_plane_x = plane_x;
            plane_x = plane_x * 0.9987f - plane_y * -0.0499f;
            plane_y = old_plane_x * -0.0499f + plane_y * 0.9987f;
        }
        if (k == 'a') {
            float old_dir_x = player_dir_x;
            player_dir_x = player_dir_x * 0.9987f - player_dir_y * 0.0499f;
            player_dir_y = old_dir_x * 0.0499f + player_dir_y * 0.9987f;
            float old_plane_x = plane_x;
            plane_x = plane_x * 0.9987f - plane_y * 0.0499f;
            plane_y = old_plane_x * 0.0499f + plane_y * 0.9987f;
        }
        timer_sleep(10);
    }
    kfree(fb);
}
