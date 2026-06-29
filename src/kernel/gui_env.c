#include "gui_env.h"
#include "vfs.h"
#include "string.h"

extern const unsigned char res_font_ttf[];
extern const unsigned int res_font_ttf_len;
extern const unsigned char res_wallpaper_png[];
extern const unsigned int res_wallpaper_png_len;
extern const unsigned char res_icon_files_png[];
extern const unsigned int res_icon_files_png_len;
extern const unsigned char res_icon_shell_png[];
extern const unsigned int res_icon_shell_png_len;
extern const unsigned char res_icon_vibenet_png[];
extern const unsigned int res_icon_vibenet_png_len;
extern const unsigned char res_icon_snake_png[];
extern const unsigned int res_icon_snake_png_len;
extern const unsigned char res_icon_neovim_png[];
extern const unsigned int res_icon_neovim_png_len;

void gui_environment_init(void)
{
    /* setup some directories because apps are picky as fuck */
    if (vfs_resolve("/tmp") < 0) vfs_mkdir("/tmp");
    if (vfs_resolve("/var") < 0) vfs_mkdir("/var");
    if (vfs_resolve("/var/log") < 0) vfs_mkdir("/var/log");
    if (vfs_resolve("/etc") < 0) vfs_mkdir("/etc");
    if (vfs_resolve("/usr") < 0) vfs_mkdir("/usr");
    if (vfs_resolve("/usr/bin") < 0) vfs_mkdir("/usr/bin");
    if (vfs_resolve("/run") < 0) vfs_mkdir("/run");

    extern const uint8_t vnl_x_elf[];
    extern const uint8_t vnl_x_elf_end[];
    int fd = vfs_open("/usr/bin/tinywl", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        vfs_write(fd, vnl_x_elf, (size_t)(vnl_x_elf_end - vnl_x_elf));
        vfs_close(fd);
    }

    fd = vfs_open("/etc/vnl-gui.env", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        static const char env[] =
            "VNL_GUI_ACTIVE=1\n"
            "FRAMEBUFFER=/dev/fb0\n";
        vfs_write(fd, env, strlen(env));
        vfs_close(fd);
    }

    fd = vfs_open("/usr/bin/gui", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        static const char sx[] =
            "#!/bin/sh\n"
            "guiinfo\n";
        vfs_write(fd, sx, sizeof(sx) - 1);
        vfs_close(fd);
    }

    // Write TrueType font
    fd = vfs_open("/etc/font.ttf", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        vfs_write(fd, res_font_ttf, res_font_ttf_len);
        vfs_close(fd);
    }

    // Write Wallpaper PNG
    fd = vfs_open("/etc/wallpaper.png", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd >= 0) {
        vfs_write(fd, res_wallpaper_png, res_wallpaper_png_len);
        vfs_close(fd);
    }

    // Write PNG Icons
    struct { const char *path; const unsigned char *data; unsigned int len; } icons[] = {
        {"/etc/icon_files.png", res_icon_files_png, res_icon_files_png_len},
        {"/etc/icon_shell.png", res_icon_shell_png, res_icon_shell_png_len},
        {"/etc/icon_vibenet.png", res_icon_vibenet_png, res_icon_vibenet_png_len},
        {"/etc/icon_snake.png", res_icon_snake_png, res_icon_snake_png_len},
        {"/etc/icon_neovim.png", res_icon_neovim_png, res_icon_neovim_png_len}
    };
    for (int i = 0; i < 5; i++) {
        fd = vfs_open(icons[i].path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
        if (fd >= 0) {
            vfs_write(fd, icons[i].data, icons[i].len);
            vfs_close(fd);
        }
    }
}
