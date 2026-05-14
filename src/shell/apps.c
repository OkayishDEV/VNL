#include "printf.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"

extern void cmd_doom_standalone(int argc, char **argv);
extern int vnl_spawn_elf_path(const char *path);
/* run doom or just blow up i guess */
void cmd_doom_generic(int argc, char **argv) {
    (void)argc; (void)argv;
    if (vnl_spawn_elf_path("/bin/doom") >= 0) return;
    cmd_doom_standalone(argc, argv);
}

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
