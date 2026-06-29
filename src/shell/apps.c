#include "printf.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"

/* fake htop shit */
void cmd_htop_gui(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
    kprintf("VNL HTOP - System Resource Monitor\n");
    kprintf("CPU: 99%% [||||||||||||||||||||||||||||||]\n");
    kprintf("MEM: 12MB / 64MB\n");
    timer_sleep(2000);
}

/* fake vim shit */
void cmd_neovim_vnl(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
    kprintf("NeoVim VNL (Professional Edition)\n");
    kprintf("~ \n~ \n~ [VNL Professional Edit]\n");
    timer_sleep(2000);
}
