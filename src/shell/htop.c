#include "htop.h"
#include "vga.h"
#include "printf.h"
#include "sched.h"
#include "pmm.h"
#include "timer.h"
#include "keyboard.h"
#include "vfs.h"

void cmd_htop_gui(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Check if installed via vpkg */
    if (vfs_resolve("/usr/bin/htop-gui") < 0) {
        kprintf("htop-gui: Application not installed.\n");
        kprintf("Please run 'vpkg install htop-gui' first to deploy native telemetry hooks.\n");
        return;
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    while (1) {
        /* Check for exit */
        int ch = keyboard_poll();
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            break;
        }

        /* Draw Gauges Header */
        vga_set_cursor(1, 1);
        vga_set_color(VGA_LCYAN, VGA_BLACK);
        kprintf("1  ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("[");

        /* Simulate a fluctuating CPU load gauge based on timer ticks */
        uint64_t t = timer_ticks();
        int cpu_bars = 10 + (int)((t / 100) % 25);
        for (int b = 0; b < 35; b++) {
            if (b < cpu_bars) {
                if (b > 28) vga_set_color(VGA_LRED, VGA_BLACK);
                else if (b > 20) vga_set_color(VGA_YELLOW, VGA_BLACK);
                else vga_set_color(VGA_LGREEN, VGA_BLACK);
                kprintf("|");
            } else {
                kprintf(" ");
            }
        }
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("] %3d%%\n", (cpu_bars * 100) / 35);

        /* Memory Gauge */
        uint64_t total_pg = pmm_total_pages();
        uint64_t free_pg  = pmm_free_pages();
        uint64_t used_pg  = total_pg > free_pg ? total_pg - free_pg : 0;
        
        int mem_bars = 0;
        if (total_pg > 0) mem_bars = (int)((used_pg * 35) / total_pg);

        vga_set_cursor(2, 1);
        vga_set_color(VGA_LCYAN, VGA_BLACK);
        kprintf("Mem");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("[");

        for (int b = 0; b < 35; b++) {
            if (b < mem_bars) {
                if (b > 28) vga_set_color(VGA_LRED, VGA_BLACK);
                else if (b > 20) vga_set_color(VGA_YELLOW, VGA_BLACK);
                else vga_set_color(VGA_LGREEN, VGA_BLACK);
                kprintf("|");
            } else {
                kprintf(" ");
            }
        }
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("] %4lluM/%4lluM\n", (used_pg * 4) / 1024, (total_pg * 4) / 1024);

        /* Header Info Info */
        vga_set_cursor(4, 1);
        int ntasks = sched_task_count();
        int running_cnt = 0;
        for (int i = 0; i < ntasks; i++) {
            Task *tk = sched_get_task(i);
            if (tk && tk->state == TASK_RUNNING) running_cnt++;
        }
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("Tasks: %2d, %2d running   Load average: 0.05 0.02 0.01\n", ntasks, running_cnt);
        kprintf("Uptime: %llu seconds\n", t / 1000);

        /* Columns Header */
        vga_set_cursor(6, 1);
        vga_set_color(VGA_BLACK, VGA_LGREEN);
        kprintf("  PID USER     PRI  NI  VIRT   RES S CPU%% MEM%%   TIME+  Command                  ");
        for (int p = 0; p < 80 - 79; p++) kprintf(" ");

        /* Enumerate real Tasks */
        vga_set_color(VGA_WHITE, VGA_BLACK);
        int print_r = 7;
        for (int i = 0; i < ntasks; i++) {
            Task *tk = sched_get_task(i);
            if (!tk) continue;

            vga_set_cursor(print_r++, 1);
            
            const char *st_str = "S";
            if (tk->state == TASK_RUNNING) st_str = "R";
            else if (tk->state == TASK_READY) st_str = "S";
            else if (tk->state == TASK_DEAD) st_str = "Z";

            uint64_t virt_sz = (tk->brk_end > tk->brk_start) ? (tk->brk_end - tk->brk_start) / 1024 : 32;
            uint64_t res_sz  = virt_sz > 4 ? virt_sz - 4 : virt_sz;

            int cpu_pct = (tk->state == TASK_RUNNING) ? 25 : 0;
            if (tk->pid == 0) cpu_pct = 5; /* idle */

            kprintf("%5d root      20   0 %5llu %5llu %s %4d  0.5  0:00.02 %-25s",
                    tk->pid, virt_sz, res_sz, st_str, cpu_pct, tk->name[0] ? tk->name : "sys_idle");
            
            /* pad line */
            for (int p = 0; p < 8; p++) kprintf(" ");
        }

        /* Clear stale trailing rows */
        for (int r = print_r; r <= 23; r++) {
            vga_set_cursor(r, 1);
            for (int c = 0; c < 80; c++) kprintf(" ");
        }

        /* Footer Help */
        vga_set_cursor(24, 1);
        vga_set_color(VGA_BLACK, VGA_WHITE);
        kprintf("F1");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("Help  ");
        vga_set_color(VGA_BLACK, VGA_WHITE);
        kprintf("F9");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("Kill  ");
        vga_set_color(VGA_BLACK, VGA_WHITE);
        kprintf("F10");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("Quit");

        timer_sleep(500);
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}
