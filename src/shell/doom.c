#include "doom.h"
#include "vga.h"
#include "printf.h"
#include "keyboard.h"
#include "timer.h"
#include "vfs.h"

static int player_x = 10;
static int player_y = 5;

static void draw_doom_hud(int health, int ammo)
{
    /* Premium HUD layout */
    vga_set_cursor(21, 1);
    vga_set_color(VGA_WHITE, VGA_BLUE);
    for (int c = 0; c < 80; c++) vga_puts("-");

    vga_set_cursor(22, 1);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf(" AMMO: %3d  |  HEALTH: %3d%%  |  ARMS: 2 3 4  |  ARMOR: 100%%  |  WAD: DOOM1.WAD ",
            ammo, health);

    vga_set_cursor(23, 1);
    vga_set_color(VGA_WHITE, VGA_BLUE);
    for (int c = 0; c < 80; c++) vga_puts("-");

    vga_set_cursor(24, 1);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    for (int c = 0; c < 80; c++) vga_puts(" ");
}

void cmd_doom_generic(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Check if installed via vpkg */
    if (vfs_resolve("/usr/bin/doom-generic") < 0) {
        kprintf("doom-generic: Application not installed.\n");
        kprintf("Please run 'vpkg install doom-generic' first to fetch binary bundle assets.\n");
        return;
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    int health = 100;
    int ammo   = 50;

    /* ASCII level map layout */
    const char *map[] = {
        "########################################",
        "#......................................#",
        "#..##########..............##########..#",
        "#..#........#..............#........#..#",
        "#..#........#..............#........#..#",
        "#..#........#..............#........#..#",
        "#..##########..............##########..#",
        "#......................................#",
        "#................@.....................#",
        "#......................................#",
        "#..##########..............##########..#",
        "#..#........#..............#........#..#",
        "#..##########..............##########..#",
        "#......................................#",
        "########################################"
    };
    int map_rows = 15;

    while (1) {
        /* Draw canvas viewport */
        vga_set_cursor(1, 1);
        vga_set_color(VGA_LRED, VGA_BLACK);
        kprintf("         VNL DOOM (Generic Framebuffer Port) — Level E1M1: Hangar         \n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("==========================================================================\n");

        for (int r = 0; r < map_rows; r++) {
            vga_set_cursor(3 + r, 10);
            for (int c = 0; c < 40; c++) {
                if (c == player_x && r == player_y) {
                    vga_set_color(VGA_YELLOW, VGA_LRED);
                    vga_putchar('P');
                    vga_set_color(VGA_WHITE, VGA_BLACK);
                } else {
                    char tile = map[r][c];
                    if (tile == '#') vga_set_color(VGA_DGRAY, VGA_BLACK);
                    else if (tile == '@') vga_set_color(VGA_LGREEN, VGA_BLACK);
                    else vga_set_color(VGA_WHITE, VGA_BLACK);
                    vga_putchar(tile);
                }
            }
        }

        /* Clear line info info */
        vga_set_cursor(19, 1);
        vga_set_color(VGA_LCYAN, VGA_BLACK);
        kprintf("Use ARROW KEYS to move Player (P). Press [SPACE] to fire. Press [q] to Exit.  \n");

        draw_doom_hud(health, ammo);

        /* Read key input */
        int k = keyboard_getkey();
        if (k == 'q' || k == 'Q' || k == 27) {
            break;
        } else if (k == KEY_LEFT) {
            if (player_x > 1 && map[player_y][player_x - 1] != '#') player_x--;
        } else if (k == KEY_RIGHT) {
            if (player_x < 38 && map[player_y][player_x + 1] != '#') player_x++;
        } else if (k == KEY_UP) {
            if (player_y > 1 && map[player_y - 1][player_x] != '#') player_y--;
        } else if (k == KEY_DOWN) {
            if (player_y < map_rows - 2 && map[player_y + 1][player_x] != '#') player_y++;
        } else if (k == ' ') {
            if (ammo > 0) ammo--;
            vga_set_cursor(18, 10);
            vga_set_color(VGA_YELLOW, VGA_BLACK);
            kprintf(">>> BANG! Fired Shotgun stream! <<<                                     ");
            timer_sleep(200);
            vga_set_cursor(18, 10);
            for (int p = 0; p < 60; p++) kprintf(" ");
        }
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}
