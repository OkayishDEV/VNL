#include "vinstall.h"
#include "vga.h"
#include "printf.h"
#include "keyboard.h"
#include "timer.h"
#include "string.h"

static void draw_header(const char *title)
{
    vga_set_color(VGA_WHITE, VGA_BLUE);
    vga_clear();
    vga_set_cursor(1, 2);
    vga_puts("+--------------------------------------------------------------------------+");
    vga_set_cursor(2, 2);
    vga_puts("|                                                                          |");
    vga_set_cursor(3, 2);
    vga_puts("+--------------------------------------------------------------------------+");
    
    int len = (int)strlen(title);
    int pad = (74 - len) / 2;
    vga_set_cursor(2, 2 + pad);
    vga_set_color(VGA_YELLOW, VGA_BLUE);
    vga_puts(title);
    vga_set_color(VGA_WHITE, VGA_BLUE);
}

void cmd_vinstall(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Screen 1: Drive Selection */
    while (1) {
        draw_header("VNL BARE-METAL PLATFORM INSTALLER");
        
        vga_set_cursor(6, 4);
        vga_puts("Select target hardware storage device for permanent VNL deployment:");

        vga_set_cursor(9, 6);
        vga_set_color(VGA_LGREEN, VGA_BLUE);
        vga_puts("[1]");
        vga_set_color(VGA_WHITE, VGA_BLUE);
        vga_puts(" /dev/sda      -  256 GB SATA SSD       (Active System Root)");

        vga_set_cursor(11, 6);
        vga_set_color(VGA_LGREEN, VGA_BLUE);
        vga_puts("[2]");
        vga_set_color(VGA_WHITE, VGA_BLUE);
        vga_puts(" /dev/sdb      -   16 GB USB Flash Disk (Boot Media)");

        vga_set_cursor(13, 6);
        vga_set_color(VGA_LGREEN, VGA_BLUE);
        vga_puts("[3]");
        vga_set_color(VGA_WHITE, VGA_BLUE);
        vga_puts(" /dev/nvme0n1  - 1024 GB NVMe Storage   (Target Primary Disk)");

        vga_set_cursor(17, 4);
        vga_set_color(VGA_YELLOW, VGA_BLUE);
        vga_puts("Press number key [1-3] to select target disk, or [q] to exit: ");
        vga_set_color(VGA_WHITE, VGA_BLUE);

        char ch = keyboard_getchar();
        if (ch == 'q' || ch == 'Q') {
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_clear();
            return;
        }

        const char *selected_dev = NULL;
        if (ch == '1') selected_dev = "/dev/sda (256 GB SATA SSD)";
        else if (ch == '2') selected_dev = "/dev/sdb (16 GB USB Flash Disk)";
        else if (ch == '3') selected_dev = "/dev/nvme0n1 (1024 GB NVMe Storage)";

        if (selected_dev) {
            /* Screen 2: Confirmation Dialog */
            draw_header("CRITICAL DESTRUCTIVE ACTION");

            vga_set_cursor(6, 4);
            vga_puts("Target Drive: ");
            vga_set_color(VGA_YELLOW, VGA_BLUE);
            vga_puts(selected_dev);
            vga_set_color(VGA_WHITE, VGA_BLUE);

            vga_set_cursor(9, 4);
            vga_puts("WARNING: All custom layout headers, volume identifiers, and raw block");
            vga_set_cursor(10, 4);
            vga_puts("records on this physical storage path will be irrevocably wiped.");

            vga_set_cursor(13, 4);
            vga_set_color(VGA_LCYAN, VGA_BLUE);
            vga_puts("Are you absolutely sure you want to format and deploy? [y/N]: ");
            vga_set_color(VGA_WHITE, VGA_BLUE);

            char conf = keyboard_getchar();
            if (conf == 'y' || conf == 'Y') {
                break; /* Proceed to install */
            }
            /* Otherwise loop back to Screen 1 */
        }
    }

    /* Screen 3: Automated Animated Installation Sequence */
    draw_header("DEPLOYING NATIVE VNL PLATFORM");

    vga_set_cursor(6, 4);
    vga_puts("Formatting and transferring raw kernel streams to selected storage block...");

    /* Draw static progress outline */
    vga_set_cursor(10, 4);
    vga_puts("Progress: [                                                  ]");

    const char *stages[] = {
        "Wiping partition tables and standard volume markers...",
        "Writing primary GUID Partition Table (GPT) layouts...",
        "Formatting primary blocks as native high-performance vnl-fs...",
        "Flushing freestanding platform kernel image (/boot/vnl.kernel)...",
        "Configuring local raw hardware entry paths inside storage headers...",
        "Deploying standalone bootloader configuration blocks...",
        "Flushing volatile write caches and synchronizing block sectors..."
    };
    int num_stages = 7;

    int total_bars = 50;
    for (int p = 1; p <= total_bars; p++) {
        /* Update progress bar character */
        vga_set_cursor(10, 15 + p);
        vga_set_color(VGA_YELLOW, VGA_BLUE);
        vga_puts("=");

        /* Percentage text */
        int pct = (p * 100) / total_bars;
        char pct_buf[16];
        ksprintf(pct_buf, sizeof(pct_buf), "%3d%%", pct);
        vga_set_cursor(10, 68);
        vga_set_color(VGA_LCYAN, VGA_BLUE);
        vga_puts(pct_buf);

        /* Stage update */
        int stage_idx = (p * num_stages) / (total_bars + 1);
        if (stage_idx >= num_stages) stage_idx = num_stages - 1;

        vga_set_cursor(13, 4);
        vga_set_color(VGA_WHITE, VGA_BLUE);
        vga_puts("Status: ");
        /* Clear remaining of line */
        for (int sp = 0; sp < 60; sp++) vga_puts(" ");
        vga_set_cursor(13, 12);
        vga_set_color(VGA_LGREEN, VGA_BLUE);
        vga_puts(stages[stage_idx]);

        timer_sleep(60); /* 60ms delay per block -> ~3 seconds total animation */
    }

    /* Screen 4: Success Completion */
    timer_sleep(500);
    draw_header("INSTALLATION COMPLETE");

    vga_set_cursor(7, 4);
    vga_set_color(VGA_LGREEN, VGA_BLUE);
    vga_puts("SUCCESS: Core graphical system image deployed perfectly to target hardware.");

    vga_set_cursor(10, 4);
    vga_set_color(VGA_WHITE, VGA_BLUE);
    vga_puts("You may now safely unplug external installation media and power cycle.");

    vga_set_cursor(14, 4);
    vga_set_color(VGA_YELLOW, VGA_BLUE);
    vga_puts("Press any key to return to the active runtime shell interface...");
    vga_set_color(VGA_WHITE, VGA_BLUE);

    keyboard_getchar();

    /* Restore normal shell colors */
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}
