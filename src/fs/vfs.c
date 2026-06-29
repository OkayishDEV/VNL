#include "vfs.h"
#include "devfs.h"
#include "heap.h"
#include "string.h"
#include "printf.h"
#include "cpu.h"
#include "errno.h"
#include "shm.h"

/* where the fuck do we store these nodes? */
static VFSNode nodes[VFS_MAX_NODES];
static uint32_t next_inode = 1;   /* zero is root i guess who cares */

uint32_t vfs_cwd = VFS_INODE_ROOT;
void vfs_set_cwd(uint32_t inode) { vfs_cwd = inode; }

/* fd table... another mess */
typedef struct {
    bool     open;
    uint32_t inode;
    size_t   offset;
    int      flags;
} FDEntry;

static FDEntry fds[VFS_MAX_FDS];
/* standard fds... syscall layer handles this crap */

/* node helpers... wtf is a helper? */
static VFSNode *node_get(uint32_t inode)
{
    for (int i = 0; i < VFS_MAX_NODES; i++)
        if (nodes[i].used && nodes[i].inode == inode) return &nodes[i];
    return NULL;
}

static VFSNode *node_alloc(void)
{
    for (int i = 0; i < VFS_MAX_NODES; i++)
        if (!nodes[i].used) {
            memset(&nodes[i], 0, sizeof(nodes[i]));
            return &nodes[i];
        }
    return NULL;
}

/* walk the tree... or just get lost */
int vfs_resolve(const char *path)
{
    if (!path || !*path) return (int)vfs_cwd;

    uint32_t cur = (*path == '/') ? VFS_INODE_ROOT : vfs_cwd;
    if (*path == '/') path++;
    if (!*path) return (int)cur;   /* root */

    char component[VFS_NAME_MAX];
    while (*path) {
        /* Extract next component */
        int ci = 0;
        while (*path && *path != '/' && ci < VFS_NAME_MAX - 1)
            component[ci++] = *path++;
        component[ci] = '\0';
        if (*path == '/') path++;

        if (strcmp(component, ".") == 0) continue;
        if (strcmp(component, "..") == 0) {
            VFSNode *n = node_get(cur);
            if (n) cur = n->parent;
            continue;
        }

        /* Find child with this name */
        bool found = false;
        for (int i = 0; i < VFS_MAX_NODES; i++) {
            if (nodes[i].used && nodes[i].parent == cur &&
                strcmp(nodes[i].name, component) == 0) {
                cur = nodes[i].inode;
                found = true;
                break;
            }
        }
        if (!found) return -1;
    }
    return (int)cur;
}

/* find the parent... like looking for a needle in a haystack */
static int resolve_parent(const char *path, char *leaf_out)
{
    /* Find last '/' */
    const char *slash = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '/') slash = p;

    if (!slash) {
        strncpy(leaf_out, path, VFS_NAME_MAX - 1);
        leaf_out[VFS_NAME_MAX - 1] = '\0';
        return (int)vfs_cwd;
    }

    /* everything before slash is parent... duh */
    char parent_path[256];
    size_t plen = (size_t)(slash - path);
    if (plen == 0) {
        parent_path[0] = '/'; parent_path[1] = '\0';
    } else {
        if (plen >= sizeof(parent_path)) plen = sizeof(parent_path) - 1;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';
    }
    strncpy(leaf_out, slash + 1, VFS_NAME_MAX - 1);
    leaf_out[VFS_NAME_MAX - 1] = '\0';
    return vfs_resolve(parent_path);
}

/* public api... for everyone to break */

void vfs_init(void)
{
    memset(nodes, 0, sizeof(nodes));
    memset(fds,   0, sizeof(fds));
    next_inode = 1;

    /* root... the start of all problems */
    nodes[0].used   = true;
    nodes[0].type   = VFS_DIR;
    nodes[0].inode  = VFS_INODE_ROOT;
    nodes[0].parent = VFS_INODE_ROOT;
    nodes[0].name[0] = '/';
    nodes[0].name[1] = '\0';

    /* starter dirs... hope they're enough */
    vfs_mkdir("/bin");
    vfs_mkdir("/etc");
    vfs_mkdir("/tmp");
    vfs_mkdir("/home");
    vfs_mkdir("/usr");
    vfs_mkdir("/usr/bin");
    vfs_mkdir("/var");
    vfs_mkdir("/var/cache");
}

int vfs_open(const char *path, int flags)
{
    /* Find a free fd (start from 3 to leave 0/1/2 for std streams) */
    int fd = -1;
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (!fds[i].open) { fd = i; break; }
    }
    if (fd < 0) return -1;

    int inode = -1;
    if (strncmp(path, "/dev/shm/", 9) == 0) {
        const char *name = path + 9;
        int shm_idx = shm_open_object(name, flags);
        if (shm_idx < 0) return shm_idx;

        inode = vfs_resolve(path);
        if (inode < 0) {
            if (!(flags & VFS_O_CREATE)) return -1;
            if (vfs_mknod_chr(path, DEV_SHM_MAJOR, shm_idx) < 0)
                return -1;
            inode = vfs_resolve(path);
        }
    } else {
        inode = vfs_resolve(path);
    }

    if (inode < 0) {
        if (!(flags & VFS_O_CREATE)) return -1;
        /* Create the file */
        char leaf[VFS_NAME_MAX];
        int parent = resolve_parent(path, leaf);
        if (parent < 0) return -1;
        VFSNode *pn = node_get((uint32_t)parent);
        if (!pn || pn->type != VFS_DIR) return -1;
        VFSNode *nn = node_alloc();
        if (!nn) return -1;
        nn->used   = true;
        nn->type   = VFS_FILE;
        nn->inode  = next_inode++;
        nn->parent = (uint32_t)parent;
        strncpy(nn->name, leaf, VFS_NAME_MAX - 1);
        nn->data = NULL;
        nn->size = nn->capacity = 0;
        inode = (int)nn->inode;
    }

    VFSNode *n = node_get((uint32_t)inode);
    if (!n || (n->type != VFS_FILE && n->type != VFS_CHR)) return -1;

    if ((flags & VFS_O_TRUNC) && n->type == VFS_FILE) {
        kfree(n->data);
        n->data = NULL;
        n->size = n->capacity = 0;
    }

    fds[fd].open   = true;
    fds[fd].inode  = (uint32_t)inode;
    fds[fd].offset = (flags & VFS_O_WRITE) && !(flags & VFS_O_TRUNC) ? n->size : 0;
    fds[fd].flags  = flags;
    return fd;
}

int vfs_open_std(const char *path, int flags, int target_fd)
{
    if (target_fd < 0 || target_fd >= 3) return -1;
    if (fds[target_fd].open) return -1;

    int inode = vfs_resolve(path);
    if (inode < 0) return -1;

    VFSNode *n = node_get((uint32_t)inode);
    if (!n || (n->type != VFS_FILE && n->type != VFS_CHR)) return -1;

    fds[target_fd].open   = true;
    fds[target_fd].inode  = (uint32_t)inode;
    fds[target_fd].offset = 0;
    fds[target_fd].flags  = flags;
    return target_fd;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -1;
    fds[fd].open = false;
    return 0;
}

int vfs_dup(int oldfd)
{
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || !fds[oldfd].open) return -EBADF;
    int newfd = -1;
    for (int i = 3; i < VFS_MAX_FDS; i++) {
        if (!fds[i].open) { newfd = i; break; }
    }
    if (newfd < 0) return -EMFILE;
    fds[newfd] = fds[oldfd];
    return newfd;
}

int vfs_dup2(int oldfd, int newfd)
{
    if (oldfd < 0 || oldfd >= VFS_MAX_FDS || !fds[oldfd].open) return -EBADF;
    if (newfd < 0 || newfd >= VFS_MAX_FDS) return -EBADF;
    if (oldfd == newfd) return newfd;
    fds[newfd] = fds[oldfd];
    return newfd;
}

int vfs_ftruncate(int fd, int64_t length)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -EBADF;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n) return -EBADF;

    if (n->type == VFS_CHR && n->dev_major == DEV_SHM_MAJOR) {
        return shm_truncate(n->dev_minor, (size_t)length);
    }

    if (n->type == VFS_FILE) {
        if (length < 0) return -EINVAL;
        if ((size_t)length > n->capacity) {
            size_t new_cap = ALIGN_UP(length + 256, PAGE_SIZE);
            uint8_t *new_data = krealloc(n->data, new_cap);
            if (!new_data) return -ENOMEM;
            n->data = new_data;
            n->capacity = new_cap;
        }
        n->size = length;
        return 0;
    }

    return -EINVAL;
}

int vfs_read(int fd, void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -1;
    if (!(fds[fd].flags & VFS_O_READ)) return -1;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n) return -1;
    if (n->type == VFS_CHR)
        return devfs_chr_read(n, buf, len, &fds[fd].offset);
    size_t avail = n->size - fds[fd].offset;
    if (avail == 0) return 0;
    size_t to_read = (len < avail) ? len : avail;
    memcpy(buf, n->data + fds[fd].offset, to_read);
    fds[fd].offset += to_read;
    return (int)to_read;
}

int vfs_write(int fd, const void *buf, size_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -1;
    if (!(fds[fd].flags & VFS_O_WRITE)) return -1;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n) return -1;
    if (n->type == VFS_CHR)
        return devfs_chr_write(n, buf, len, &fds[fd].offset);
    if (n->type != VFS_FILE) return -1;

    size_t new_end = fds[fd].offset + len;
    if (new_end > n->capacity) {
        size_t new_cap = new_end + 256;
        uint8_t *new_data = (uint8_t *)krealloc(n->data, new_cap);
        if (!new_data) return -1;
        n->data     = new_data;
        n->capacity = new_cap;
    }
    memcpy(n->data + fds[fd].offset, buf, len);
    fds[fd].offset += len;
    if (fds[fd].offset > n->size) n->size = fds[fd].offset;
    return (int)len;
}

int vfs_mkdir(const char *path)
{
    if (vfs_resolve(path) >= 0) return -1;  /* already exists */
    char leaf[VFS_NAME_MAX];
    int parent = resolve_parent(path, leaf);
    if (parent < 0 || !leaf[0]) return -1;
    VFSNode *pn = node_get((uint32_t)parent);
    if (!pn || pn->type != VFS_DIR) return -1;
    VFSNode *nn = node_alloc();
    if (!nn) return -1;
    nn->used   = true;
    nn->type   = VFS_DIR;
    nn->inode  = next_inode++;
    nn->parent = (uint32_t)parent;
    strncpy(nn->name, leaf, VFS_NAME_MAX - 1);
    nn->data = NULL;
    nn->size = nn->capacity = 0;
    return 0;
}

int vfs_unlink(const char *path)
{
    int inode = vfs_resolve(path);
    if (inode < 0) return -1;
    if ((uint32_t)inode == VFS_INODE_ROOT) return -1;
    VFSNode *n = node_get((uint32_t)inode);
    if (!n) return -1;
    /* Refuse to remove non-empty directory */
    if (n->type == VFS_DIR) {
        for (int i = 0; i < VFS_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == (uint32_t)inode)
                return -1;
    }
    if (n->type == VFS_CHR && n->dev_major == DEV_SHM_MAJOR) {
        shm_destroy(n->dev_minor);
    }
    kfree(n->data);
    memset(n, 0, sizeof(*n));
    return 0;
}

int vfs_readdir(const char *path, char (*names)[VFS_NAME_MAX], int max)
{
    int inode = vfs_resolve(path);
    if (inode < 0) return -1;
    VFSNode *dir = node_get((uint32_t)inode);
    if (!dir || dir->type != VFS_DIR) return -1;
    int count = 0;
    for (int i = 0; i < VFS_MAX_NODES && count < max; i++) {
        if (nodes[i].used && nodes[i].parent == (uint32_t)inode &&
            nodes[i].inode != (uint32_t)inode) {
            strncpy(names[count], nodes[i].name, VFS_NAME_MAX - 1);
            names[count][VFS_NAME_MAX - 1] = '\0';
            /* Append '/' for dirs */
            if (nodes[i].type == VFS_DIR) {
                size_t l = strlen(names[count]);
                if (l < VFS_NAME_MAX - 1) { names[count][l] = '/'; names[count][l+1] = '\0'; }
            }
            count++;
        }
    }
    return count;
}

int vfs_getdents64(int fd, void *dirp, size_t count)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -EBADF;
    VFSNode *dir = node_get(fds[fd].inode);
    if (!dir || dir->type != VFS_DIR) return -ENOTDIR;

    size_t start_index = fds[fd].offset;
    uint8_t *buf = (uint8_t *)dirp;
    size_t bytes_written = 0;

    if (start_index == 0) {
        size_t reclen = (19 + 2 + 7) & ~7ULL;
        if (bytes_written + reclen > count) return (int)bytes_written;

        struct {
            uint64_t d_ino;
            int64_t  d_off;
            uint16_t d_reclen;
            uint8_t  d_type;
            char     d_name[2];
            char     pad[6];
        } __attribute__((packed)) ent;
        memset(&ent, 0, sizeof(ent));
        ent.d_ino = dir->inode;
        ent.d_off = 1;
        ent.d_reclen = (uint16_t)reclen;
        ent.d_type = 4;
        ent.d_name[0] = '.';
        ent.d_name[1] = '\0';
        memcpy(buf + bytes_written, &ent, reclen);
        bytes_written += reclen;
        fds[fd].offset++;
        start_index = 1;
    }

    if (start_index == 1) {
        size_t reclen = (19 + 3 + 7) & ~7ULL;
        if (bytes_written + reclen > count) return (int)bytes_written;

        struct {
            uint64_t d_ino;
            int64_t  d_off;
            uint16_t d_reclen;
            uint8_t  d_type;
            char     d_name[3];
            char     pad[5];
        } __attribute__((packed)) ent;
        memset(&ent, 0, sizeof(ent));
        ent.d_ino = dir->parent;
        ent.d_off = 2;
        ent.d_reclen = (uint16_t)reclen;
        ent.d_type = 4;
        ent.d_name[0] = '.';
        ent.d_name[1] = '.';
        ent.d_name[2] = '\0';
        memcpy(buf + bytes_written, &ent, reclen);
        bytes_written += reclen;
        fds[fd].offset++;
        start_index = 2;
    }

    size_t node_offset = start_index - 2;
    size_t passed_nodes = 0;

    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].parent == dir->inode && nodes[i].inode != dir->inode) {
            if (passed_nodes < node_offset) {
                passed_nodes++;
                continue;
            }

            size_t name_len = strlen(nodes[i].name);
            size_t reclen = (19 + name_len + 1 + 7) & ~7ULL;
            if (bytes_written + reclen > count) {
                break;
            }

            uint64_t ino = nodes[i].inode;
            int64_t next_off = (int64_t)(fds[fd].offset + 1);
            uint16_t rec_len = (uint16_t)reclen;
            uint8_t type = (nodes[i].type == VFS_DIR) ? 4 : ((nodes[i].type == VFS_CHR) ? 2 : 8);

            memset(buf + bytes_written, 0, reclen);
            memcpy(buf + bytes_written, &ino, 8);
            memcpy(buf + bytes_written + 8, &next_off, 8);
            memcpy(buf + bytes_written + 16, &rec_len, 2);
            memcpy(buf + bytes_written + 18, &type, 1);
            memcpy(buf + bytes_written + 19, nodes[i].name, name_len + 1);

            bytes_written += reclen;
            fds[fd].offset++;
            passed_nodes++;
        }
    }

    return (int)bytes_written;
}

int vfs_stat(const char *path, VFSNodeType *type, size_t *size)
{
    int inode = vfs_resolve(path);
    if (inode < 0) return -1;
    VFSNode *n = node_get((uint32_t)inode);
    if (!n) return -1;
    if (type) *type = n->type;
    if (size) *size = n->size;
    return 0;
}

int vfs_ioctl(int fd, uint64_t request, void *arg)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -EBADF;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n || n->type != VFS_CHR) return -ENOTTY;
    return devfs_chr_ioctl(n, request, arg);
}

VFSNode *vfs_node_from_fd(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return NULL;
    return node_get(fds[fd].inode);
}

int vfs_mknod_chr(const char *path, uint16_t major, uint16_t minor)
{
    if (vfs_resolve(path) >= 0) return -1;
    char leaf[VFS_NAME_MAX];
    int parent = resolve_parent(path, leaf);
    if (parent < 0 || !leaf[0]) return -1;
    VFSNode *pn = node_get((uint32_t)parent);
    if (!pn || pn->type != VFS_DIR) return -1;
    VFSNode *nn = node_alloc();
    if (!nn) return -1;
    nn->used       = true;
    nn->type       = VFS_CHR;
    nn->inode      = next_inode++;
    nn->parent     = (uint32_t)parent;
    nn->dev_major  = major;
    nn->dev_minor  = minor;
    nn->data       = NULL;
    nn->size       = nn->capacity = 0;
    strncpy(nn->name, leaf, VFS_NAME_MAX - 1);
    return 0;
}

char *vfs_getcwd(char *buf, size_t bufsz)
{
    if (!buf || bufsz == 0) return NULL;
    if (vfs_cwd == VFS_INODE_ROOT) { strncpy(buf, "/", bufsz); return buf; }

    /* Build path by walking to root */
    char tmp[1024];
    int pos = (int)sizeof(tmp) - 1;
    tmp[pos] = '\0';
    uint32_t cur = vfs_cwd;
    while (cur != VFS_INODE_ROOT) {
        VFSNode *n = node_get(cur);
        if (!n) break;
        size_t l = strlen(n->name);
        pos -= (int)l;
        if (pos < 1) break;
        memcpy(tmp + pos, n->name, l);
        tmp[--pos] = '/';
        cur = n->parent;
    }
    if (tmp[pos] == '\0') tmp[--pos] = '/';
    strncpy(buf, tmp + pos, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return buf;
}

int64_t vfs_lseek(int fd, int64_t offset, int whence)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !fds[fd].open) return -EBADF;
    VFSNode *n = node_get(fds[fd].inode);
    if (!n) return -EBADF;
    if (n->type == VFS_CHR) {
        /* Character devices return 0 or do not support seek */
        return 0;
    }
    int64_t new_offset = (int64_t)fds[fd].offset;
    switch (whence) {
        case 0: /* SEEK_SET */
            new_offset = offset;
            break;
        case 1: /* SEEK_CUR */
            new_offset += offset;
            break;
        case 2: /* SEEK_END */
            new_offset = (int64_t)n->size + offset;
            break;
        default:
            return -EINVAL;
    }
    if (new_offset < 0) return -EINVAL;
    fds[fd].offset = (size_t)new_offset;
    return new_offset;
}

int vfs_fcntl(int fd, int cmd, int64_t arg)
{
    if (fd < 0 || fd >= VFS_MAX_FDS) return -EBADF;
    if (!fds[fd].open) return -EBADF;
    if (cmd == 3) { // F_GETFL
        return fds[fd].flags;
    } else if (cmd == 4) { // F_SETFL
        fds[fd].flags = (int)arg;
        return 0;
    }
    return -EINVAL;
}
