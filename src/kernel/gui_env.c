#include "gui_env.h"
#include "vfs.h"
#include "string.h"

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

    int fd = vfs_open("/etc/vnl-gui.env", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
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
}
