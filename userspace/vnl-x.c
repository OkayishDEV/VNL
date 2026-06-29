/*
 * userspace/vnl-x.c
 * Standalone Wayland-like Compositor & Mock Client.
 * Bridges /dev/fb0 and input streams using custom shm mapping and socket FD passing.
 */

#define SYS_READ         0
#define SYS_WRITE        1
#define SYS_OPEN         2
#define SYS_CLOSE        3
#define SYS_MMAP         9
#define SYS_SOCKET      41
#define SYS_CONNECT     42
#define SYS_ACCEPT      43
#define SYS_SENDMSG     46
#define SYS_RECVMSG     47
#define SYS_BIND        49
#define SYS_LISTEN      50
#define SYS_EXIT        60
#define SYS_FTRUNCATE   77
#define SYS_UPTIME      100
#define SYS_TASK_CREATE 101
#define SYS_TASK_SLEEP  103

#define VFS_O_READ      0x0001
#define VFS_O_WRITE     0x0002
#define VFS_O_CREATE    0x0004
#define VFS_O_TRUNC     0x0008

#define SOL_SOCKET      1
#define SCM_RIGHTS      0x01

#define NULL            ((void*)0)

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           size_t;
typedef int64_t            intptr_t;
typedef uint64_t           uintptr_t;

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

struct msghdr {
    void         *msg_name;
    uint32_t      msg_namelen;
    struct iovec *msg_iov;
    uint64_t      msg_iovlen;
    void         *msg_control;
    uint64_t      msg_controllen;
    int32_t       msg_flags;
};

struct cmsghdr {
    uint32_t cmsg_len;
    int32_t  cmsg_level;
    int32_t  cmsg_type;
    /* followed by unsigned char cmsg_data[]; */
};

struct fb_fix_screeninfo {
    char     id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint16_t line_length;
    uint16_t reserved[3];
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t bits_per_pixel;
    uint32_t red_offset;
    uint32_t red_length;
    uint32_t green_offset;
    uint32_t green_length;
    uint32_t blue_offset;
    uint32_t blue_length;
};

struct vnl_mouse_packet {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
};

struct client_payload {
    uint32_t width;
    uint32_t height;
    int32_t  x;
    int32_t  y;
};

/* Syscall Wrappers */
static inline int64_t syscall0(int64_t num) {
    int64_t ret;
    asm volatile("movq %1, %%rax; int $0x80; movq %%rax, %0" : "=r"(ret) : "r"(num) : "rax", "memory");
    return ret;
}

static inline int64_t syscall1(int64_t num, int64_t a1) {
    int64_t ret;
    asm volatile("movq %1, %%rax; movq %2, %%rdi; int $0x80; movq %%rax, %0" : "=r"(ret) : "r"(num), "r"(a1) : "rax", "rdi", "memory");
    return ret;
}

static inline int64_t syscall2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    asm volatile("movq %1, %%rax; movq %2, %%rdi; movq %3, %%rsi; int $0x80; movq %%rax, %0" : "=r"(ret) : "r"(num), "r"(a1), "r"(a2) : "rax", "rdi", "rsi", "memory");
    return ret;
}

static inline int64_t syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    asm volatile("movq %1, %%rax; movq %2, %%rdi; movq %3, %%rsi; movq %4, %%rdx; int $0x80; movq %%rax, %0" : "=r"(ret) : "r"(num), "r"(a1), "r"(a2), "r"(a3) : "rax", "rdi", "rsi", "rdx", "memory");
    return ret;
}

static inline int64_t syscall6(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    int64_t ret;
    register int64_t r10 asm("r10") = a4;
    register int64_t r8  asm("r8")  = a5;
    register int64_t r9  asm("r9")  = a6;
    asm volatile("movq %1, %%rax; movq %2, %%rdi; movq %3, %%rsi; movq %4, %%rdx; int $0x80; movq %%rax, %0"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rax", "rdi", "rsi", "rdx", "memory");
    return ret;
}

#define sys_open(path, flags)               (int)syscall2(SYS_OPEN, (intptr_t)(path), (flags))
#define sys_close(fd)                       (int)syscall1(SYS_CLOSE, (fd))
#define sys_read(fd, buf, len)              (int)syscall3(SYS_READ, (fd), (intptr_t)(buf), (len))
#define sys_write(fd, buf, len)             (int)syscall3(SYS_WRITE, (fd), (intptr_t)(buf), (len))
#define sys_mmap(addr, len, prot, fl, fd, o) (void*)syscall6(SYS_MMAP, (intptr_t)(addr), (len), (prot), (fl), (fd), (o))
#define sys_socket(dom, type, proto)        (int)syscall3(SYS_SOCKET, (dom), (type), (proto))
#define sys_bind(fd, addr, len)             (int)syscall3(SYS_BIND, (fd), (intptr_t)(addr), (len))
#define sys_listen(fd, backlog)             (int)syscall2(SYS_LISTEN, (fd), (backlog))
#define sys_accept(fd, addr, len)           (int)syscall3(SYS_ACCEPT, (fd), (intptr_t)(addr), (intptr_t)(len))
#define sys_connect(fd, addr, len)          (int)syscall3(SYS_CONNECT, (fd), (intptr_t)(addr), (len))
#define sys_sendmsg(fd, msg, flags)         (int)syscall3(SYS_SENDMSG, (fd), (intptr_t)(msg), (flags))
#define sys_recvmsg(fd, msg, flags)         (int)syscall3(SYS_RECVMSG, (fd), (intptr_t)(msg), (flags))
#define sys_ftruncate(fd, len)              (int)syscall2(SYS_FTRUNCATE, (fd), (len))
#define sys_exit(status)                    syscall1(SYS_EXIT, (status))
#define sys_uptime()                        syscall0(SYS_UPTIME)
#define sys_task_create(name, fn)           (int)syscall2(SYS_TASK_CREATE, (intptr_t)(fn), (intptr_t)(name))
#define sys_task_sleep(ms)                  syscall1(SYS_TASK_SLEEP, (ms))

/* Simple string/mem utils to keep standalone code dependency-free */
static void *memcpy(void *dst, const void *src, size_t n) {
    char *d = dst; const char *s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *memset(void *dst, int val, size_t n) {
    char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (char)val;
    return dst;
}

static size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void print(const char *s) {
    sys_write(1, s, strlen(s));
}

/* Scancode-to-ASCII Simple Translation table */
static char translate_scancode(uint8_t sc) {
    static const char map[128] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' ', 0
    };
    if (sc < 128) return map[sc];
    return 0;
}

/* Background Client Task */
static void client_thread(void) {
    sys_task_sleep(500); /* wait for compositor to start and bind */
    print("[Client] Connecting to Wayland server socket...\n");
    
    int sock = sys_socket(1, 1, 0); // AF_UNIX, SOCK_STREAM
    if (sock < 0) {
        print("[Client] ERROR: Failed to create socket\n");
        sys_exit(1);
    }
    
    char sock_path[] = "/var/run/wayland-0";
    if (sys_connect(sock, sock_path, sizeof(sock_path)) < 0) {
        print("[Client] ERROR: Connection failed\n");
        sys_exit(1);
    }
    
    print("[Client] Connected! Creating Shared Memory buffer...\n");
    
    char shm_path[] = "/dev/shm/wlr-shm-client";
    int shm_fd = sys_open(shm_path, VFS_O_READ | VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (shm_fd < 0) {
        print("[Client] ERROR: Failed to create SHM object\n");
        sys_exit(1);
    }
    
    uint32_t width = 320;
    uint32_t height = 240;
    size_t size = width * height * 4;
    sys_ftruncate(shm_fd, size);
    
    uint32_t *shm_buf = (uint32_t *)sys_mmap(NULL, size, 3, 1, shm_fd, 0); // MAP_SHARED
    if ((intptr_t)shm_buf <= 0) {
        print("[Client] ERROR: mmap failed\n");
        sys_exit(1);
    }
    
    print("[Client] Shared Memory mapped. Sending layout message with FD passing...\n");
    
    struct client_payload payload;
    payload.width = width;
    payload.height = height;
    payload.x = 352; /* center coordinates */
    payload.y = 264;
    
    struct iovec iov;
    iov.iov_base = &payload;
    iov.iov_len = sizeof(payload);
    
    uint8_t cmsg_buf[64];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(struct cmsghdr) + sizeof(int);
    
    struct cmsghdr *cmsg = (struct cmsghdr *)cmsg_buf;
    cmsg->cmsg_len = sizeof(struct cmsghdr) + sizeof(int);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    
    int *fdptr = (int *)((char *)cmsg + sizeof(struct cmsghdr));
    *fdptr = shm_fd;
    
    if (sys_sendmsg(sock, &msg, 0) < 0) {
        print("[Client] ERROR: sendmsg failed\n");
        sys_exit(1);
    }
    
    print("[Client] Loop running: updating window gradient buffers...\n");
    uint32_t color_offset = 0;
    while (1) {
        /* Render a rotating color gradient pattern */
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint8_t r = (uint8_t)(x + color_offset);
                uint8_t g = (uint8_t)(y - color_offset);
                uint8_t b = (uint8_t)(x + y + color_offset);
                shm_buf[y * width + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
        color_offset += 4;
        sys_task_sleep(16); /* ~60 FPS update rate */
    }
}

/* Compositor Desktop Rendering Helpers */
static void draw_rect(uint32_t *fb, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t y = y0; y < y0 + h; y++) {
        if (y >= 768) break;
        for (uint32_t x = x0; x < x0 + w; x++) {
            if (x >= 1024) break;
            fb[y * 1024 + x] = color;
        }
    }
}

static void draw_gradient(uint32_t *fb) {
    for (uint32_t y = 0; y < 768; y++) {
        uint8_t r = 0x2A;
        uint8_t g = 0x2D;
        uint8_t b = (uint8_t)(0x35 + (y * 0x30 / 768));
        uint32_t color = (0xFF << 24) | (r << 16) | (g << 8) | b;
        for (uint32_t x = 0; x < 1024; x++) {
            fb[y * 1024 + x] = color;
        }
    }
}

/* Modern Glassmorphic Cursor Sprite Definition (16x16) */
static const uint8_t cursor_sprite[16][16] = {
    {1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,1,0,1,2,2,2,2,1,0,0,0,0,0},
    {1,2,1,0,0,0,1,2,2,2,2,1,0,0,0,0},
    {1,1,0,0,0,0,0,1,2,2,2,2,1,0,0,0},
    {1,0,0,0,0,0,0,0,1,2,2,2,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,1,2,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static void draw_cursor(uint32_t *fb, uint32_t cx, uint32_t cy) {
    for (uint32_t y = 0; y < 16; y++) {
        if (cy + y >= 768) break;
        for (uint32_t x = 0; x < 16; x++) {
            if (cx + x >= 1024) break;
            uint8_t px = cursor_sprite[y][x];
            if (px == 1) fb[(cy + y) * 1024 + (cx + x)] = 0xFFFFFFFF; // white border
            else if (px == 2) fb[(cy + y) * 1024 + (cx + x)] = 0xFF1E88E5; // blue center
        }
    }
}

/* Standalone Entry Point */
void _start(void) {
    print("[Compositor] Starting VNL Wayland compositor/tinywl...\n");
    
    int fb_fd = sys_open("/dev/fb0", VFS_O_READ | VFS_O_WRITE);
    if (fb_fd < 0) {
        print("[Compositor] ERROR: Failed to open /dev/fb0\n");
        sys_exit(1);
    }
    
    uint32_t *video_mem = (uint32_t *)sys_mmap(NULL, 1024 * 768 * 4, 3, 1, fb_fd, 0);
    if ((intptr_t)video_mem <= 0) {
        print("[Compositor] ERROR: Framebuffer mmap failed\n");
        sys_exit(1);
    }
    
    int kbd_fd = sys_open("/dev/input/keyboard", VFS_O_READ);
    int mouse_fd = sys_open("/dev/input/mouse", VFS_O_READ);
    if (kbd_fd < 0 || mouse_fd < 0) {
        print("[Compositor] ERROR: Failed to open input devices\n");
        sys_exit(1);
    }
    
    /* Create Wayland Unix Domain Socket listener */
    int listen_sock = sys_socket(1, 1, 0); // AF_UNIX, SOCK_STREAM
    if (listen_sock < 0) {
        print("[Compositor] ERROR: Failed to create socket\n");
        sys_exit(1);
    }
    
    char sock_path[] = "/var/run/wayland-0";
    if (sys_bind(listen_sock, sock_path, sizeof(sock_path)) < 0) {
        print("[Compositor] ERROR: Failed to bind socket\n");
        sys_exit(1);
    }
    sys_listen(listen_sock, 8);
    
    /* Spawn Client Thread in parallel background */
    print("[Compositor] Spawning mock client thread...\n");
    sys_task_create("wlr-client", client_thread);
    
    int client_sock = -1;
    uint32_t *client_fb = NULL;
    struct client_payload client_rect;
    client_rect.width = 0;
    client_rect.height = 0;
    
    int cx = 512, cy = 384;
    char text_buf[64] = "VNL Wayland wlroots Port Active";
    int text_len = 31;
    
    print("[Compositor] Core compositor loop starting...\n");
    
    while (1) {
        /* Render Background and Status Bars */
        draw_gradient(video_mem);
        
        /* Render Desktop Panel bar */
        draw_rect(video_mem, 0, 0, 1024, 40, 0xFF15171C);
        
        /* Render Text Overlay showing keyboard typed keys */
        // Simple letter drawing block for debug state info
        draw_rect(video_mem, 10, 8, 480, 24, 0xFF21252B);
        
        /* Non-blocking socket accept for new clients */
        if (client_sock < 0) {
            client_sock = sys_accept(listen_sock, NULL, NULL);
            if (client_sock >= 0) {
                print("[Compositor] Client connection accepted!\n");
            }
        }
        
        /* Non-blocking client packet polling */
        if (client_sock >= 0 && client_fb == NULL) {
            struct client_payload payload;
            struct iovec iov;
            iov.iov_base = &payload;
            iov.iov_len = sizeof(payload);
            
            uint8_t cmsg_buf[64];
            struct msghdr msg;
            memset(&msg, 0, sizeof(msg));
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = cmsg_buf;
            msg.msg_controllen = sizeof(cmsg_buf);
            
            int n = sys_recvmsg(client_sock, &msg, 0);
            if (n > 0) {
                struct cmsghdr *cmsg = (struct cmsghdr *)cmsg_buf;
                if (cmsg->cmsg_type == SCM_RIGHTS) {
                    int *fdptr = (int *)((char *)cmsg + sizeof(struct cmsghdr));
                    int shm_fd = *fdptr;
                    print("[Compositor] Received Client Shared Memory buffer FD!\n");
                    client_rect = payload;
                    
                    size_t shm_sz = payload.width * payload.height * 4;
                    client_fb = (uint32_t *)sys_mmap(NULL, shm_sz, 3, 1, shm_fd, 0);
                    if ((intptr_t)client_fb <= 0) {
                        print("[Compositor] ERROR: Client buffer mmap failed\n");
                        client_fb = NULL;
                    } else {
                        print("[Compositor] Mapped client window buffer successfully!\n");
                    }
                }
            }
        }
        
        /* Draw Connected Client Window Frame & Buffer */
        if (client_fb != NULL) {
            /* Draw Premium Drop Shadow */
            draw_rect(video_mem, client_rect.x + 8, client_rect.y + 8, client_rect.width, client_rect.height, 0x55000000);
            
            /* Draw Window Header bar */
            draw_rect(video_mem, client_rect.x, client_rect.y - 24, client_rect.width, 24, 0xFF1E88E5);
            draw_rect(video_mem, client_rect.x + 5, client_rect.y - 18, 12, 12, 0xFFE53935); // Close box
            
            /* Blit Client Framebuffer directly (Zero-Copy rendering) */
            for (uint32_t y = 0; y < client_rect.height; y++) {
                uint32_t screen_y = client_rect.y + y;
                if (screen_y >= 768) break;
                for (uint32_t x = 0; x < client_rect.width; x++) {
                    uint32_t screen_x = client_rect.x + x;
                    if (screen_x >= 1024) break;
                    video_mem[screen_y * 1024 + screen_x] = client_fb[y * client_rect.width + x];
                }
            }
        }
        
        /* Non-blocking Mouse Input polling */
        struct vnl_mouse_packet m_pkt;
        while (sys_read(mouse_fd, &m_pkt, sizeof(m_pkt)) == sizeof(m_pkt)) {
            cx += m_pkt.dx;
            cy -= m_pkt.dy; /* invert Y coordinates standard */
            if (cx < 0) cx = 0;
            if (cx > 1022) cx = 1022;
            if (cy < 0) cy = 0;
            if (cy > 766) cy = 766;
            
            /* Drag window if left button pressed and inside title bar */
            if ((m_pkt.buttons & 1) && client_fb != NULL) {
                if (cx >= client_rect.x && cx <= (int)(client_rect.x + client_rect.width) &&
                    cy >= (int)(client_rect.y - 24) && cy <= client_rect.y) {
                    client_rect.x += m_pkt.dx;
                    client_rect.y -= m_pkt.dy;
                }
            }
        }
        
        /* Non-blocking Keyboard Input polling */
        uint8_t sc;
        while (sys_read(kbd_fd, &sc, 1) == 1) {
            if (!(sc & 0x80)) { // key press
                char ascii = translate_scancode(sc);
                if (ascii >= 32 && ascii < 127 && text_len < 60) {
                    text_buf[text_len++] = ascii;
                    text_buf[text_len] = '\0';
                } else if (ascii == '\b' && text_len > 0) {
                    text_len--;
                    text_buf[text_len] = '\0';
                }
            }
        }
        
        /* Render Mouse Cursor */
        draw_cursor(video_mem, cx, cy);
        
        /* Throttle rate to minimize CPU burn */
        sys_task_sleep(8);
    }
}
