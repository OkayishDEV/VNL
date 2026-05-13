#include "neovim.h"
#include "vga.h"
#include "printf.h"
#include "keyboard.h"
#include "vfs.h"
#include "string.h"

#define NVIM_BUF_SZ 4096

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND
} NvimMode;

static char nbuf[NVIM_BUF_SZ];
static int  nlen = 0;
static int  ncur = 0;
static char nfile[128] = "";
static char ncmd[128] = "";
static int  ncmd_len = 0;
static char nmsg[128] = "";

static void nvim_draw(NvimMode mode)
{
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    /* Draw text buffer line by line */
    int row = 1;
    int col = 1;
    vga_set_cursor(row, col);

    for (int i = 0; i < nlen; i++) {
        if (i == ncur) {
            /* Highlight cursor position */
            vga_set_color(VGA_BLACK, VGA_WHITE);
        } else {
            vga_set_color(VGA_WHITE, VGA_BLACK);
        }

        if (nbuf[i] == '\n') {
            vga_putchar(' '); /* display newline cursor box */
            vga_set_color(VGA_WHITE, VGA_BLACK);
            row++;
            col = 1;
            vga_set_cursor(row, col);
        } else {
            vga_putchar(nbuf[i]);
            col++;
            if (col > 79) {
                row++;
                col = 1;
                vga_set_cursor(row, col);
            }
        }
    }

    /* End of buffer cursor position */
    if (ncur == nlen) {
        vga_set_color(VGA_BLACK, VGA_WHITE);
        vga_putchar(' ');
    }

    /* Draw empty lines with ~ */
    vga_set_color(VGA_BLUE, VGA_BLACK);
    for (int r = row + 1; r <= 22; r++) {
        vga_set_cursor(r, 1);
        vga_putchar('~');
    }

    /* Draw Statusline */
    vga_set_cursor(23, 1);
    vga_set_color(VGA_BLACK, VGA_LGREEN);
    for (int c = 0; c < 80; c++) vga_puts(" ");
    vga_set_cursor(23, 2);
    
    const char *modestr = "NORMAL";
    if (mode == MODE_INSERT) modestr = "INSERT";
    else if (mode == MODE_COMMAND) modestr = "COMMAND";

    char statbuf[128];
    ksprintf(statbuf, sizeof(statbuf), " VNL Neovim [%s] | File: %s | Char: %d/%d ",
             modestr, nfile[0] ? nfile : "[No Name]", ncur, nlen);
    vga_puts(statbuf);

    /* Draw Command/Message line */
    vga_set_cursor(24, 1);
    vga_set_color(VGA_WHITE, VGA_BLACK);
    for (int c = 0; c < 80; c++) vga_puts(" ");
    vga_set_cursor(24, 1);

    if (mode == MODE_COMMAND) {
        vga_putchar(':');
        vga_puts(ncmd);
        vga_set_color(VGA_BLACK, VGA_WHITE);
        vga_putchar(' ');
    } else if (nmsg[0]) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts(nmsg);
    }
}

void cmd_neovim_vnl(int argc, char **argv)
{
    /* Check if installed via vpkg */
    if (vfs_resolve("/usr/bin/neovim-vnl") < 0) {
        kprintf("neovim-vnl: Application not installed.\n");
        kprintf("Please run 'vpkg install neovim-vnl' first to deploy native binaries.\n");
        return;
    }

    nlen = 0;
    ncur = 0;
    nfile[0] = '\0';
    nmsg[0] = '\0';
    ncmd[0] = '\0';
    ncmd_len = 0;

    /* If file argument passed, load it */
    if (argc > 1) {
        strncpy(nfile, argv[1], sizeof(nfile)-1);
        int fd = vfs_open(nfile, VFS_O_READ);
        if (fd >= 0) {
            nlen = vfs_read(fd, nbuf, NVIM_BUF_SZ - 1);
            if (nlen < 0) nlen = 0;
            vfs_close(fd);
            ksprintf(nmsg, sizeof(nmsg), "\"%s\" %d bytes loaded", nfile, nlen);
        } else {
            ksprintf(nmsg, sizeof(nmsg), "\"%s\" [New File]", nfile);
        }
    }

    NvimMode mode = MODE_NORMAL;

    while (1) {
        nvim_draw(mode);
        int k = keyboard_getkey();

        if (mode == MODE_NORMAL) {
            nmsg[0] = '\0';
            if (k == 'i' || k == 'I') {
                mode = MODE_INSERT;
            } else if (k == ':') {
                mode = MODE_COMMAND;
                ncmd[0] = '\0';
                ncmd_len = 0;
            } else if (k == KEY_LEFT && ncur > 0) {
                ncur--;
            } else if (k == KEY_RIGHT && ncur < nlen) {
                ncur++;
            } else if (k == KEY_UP) {
                /* go back 40 chars */
                ncur -= 40;
                if (ncur < 0) ncur = 0;
            } else if (k == KEY_DOWN) {
                ncur += 40;
                if (ncur > nlen) ncur = nlen;
            }
        } else if (mode == MODE_INSERT) {
            if (k == 27) { /* ESC */
                mode = MODE_NORMAL;
            } else if (k == '\b') {
                if (ncur > 0) {
                    memmove(&nbuf[ncur - 1], &nbuf[ncur], nlen - ncur);
                    ncur--;
                    nlen--;
                }
            } else if (k == KEY_LEFT && ncur > 0) {
                ncur--;
            } else if (k == KEY_RIGHT && ncur < nlen) {
                ncur++;
            } else if (k < 128 && k != '\r') {
                char ch = (char)k;
                if (ch == '\n') ch = '\n';
                if (nlen < NVIM_BUF_SZ - 1) {
                    memmove(&nbuf[ncur + 1], &nbuf[ncur], nlen - ncur);
                    nbuf[ncur] = ch;
                    ncur++;
                    nlen++;
                }
            } else if (k == '\r') {
                if (nlen < NVIM_BUF_SZ - 1) {
                    memmove(&nbuf[ncur + 1], &nbuf[ncur], nlen - ncur);
                    nbuf[ncur] = '\n';
                    ncur++;
                    nlen++;
                }
            }
        } else if (mode == MODE_COMMAND) {
            if (k == 27) { /* ESC */
                mode = MODE_NORMAL;
            } else if (k == '\b') {
                if (ncmd_len > 0) ncmd[--ncmd_len] = '\0';
                else mode = MODE_NORMAL;
            } else if (k == '\n' || k == '\r') {
                /* Execute command */
                char *cmd_p = ncmd;
                while (*cmd_p == ' ') cmd_p++;

                if (strcmp(cmd_p, "q") == 0 || strcmp(cmd_p, "q!") == 0) {
                    break; /* Exit editor */
                } else if (cmd_p[0] == 'w' && (cmd_p[1] == ' ' || cmd_p[1] == '\0' || cmd_p[1] == 'q')) {
                    bool quit_after = (cmd_p[1] == 'q');
                    char *fname = nfile;
                    
                    char *arg_p = strchr(cmd_p, ' ');
                    if (arg_p) {
                        while (*arg_p == ' ') arg_p++;
                        if (*arg_p) {
                            strncpy(nfile, arg_p, sizeof(nfile)-1);
                            fname = nfile;
                        }
                    }

                    if (!fname[0]) {
                        strncpy(nmsg, "E32: No file name", sizeof(nmsg)-1);
                    } else {
                        int fd = vfs_open(fname, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
                        if (fd >= 0) {
                            vfs_write(fd, nbuf, nlen);
                            vfs_close(fd);
                            ksprintf(nmsg, sizeof(nmsg), "\"%s\" %dL, %dC written", fname, nlen, nlen);
                            if (quit_after) break;
                        } else {
                            ksprintf(nmsg, sizeof(nmsg), "E212: Can't open file for writing");
                        }
                    }
                } else {
                    ksprintf(nmsg, sizeof(nmsg), "E492: Not an editor command: %s", cmd_p);
                }
                mode = MODE_NORMAL;
            } else if (k < 128 && ncmd_len < 127) {
                ncmd[ncmd_len++] = (char)k;
                ncmd[ncmd_len] = '\0';
            }
        }
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
}
