/*
 * userspace/tinywl.c
 * The VNL Desktop Compositor utilizing Wayland/wlroots API structures.
 */

#include "wayland-server.h"
#include "wlr/backend.h"
#include "wlr/render/wlr_renderer.h"
#include "wlr/types/wlr_output.h"
#include "wlr/types/wlr_keyboard.h"
#include "wlr/types/wlr_pointer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#define VFS_O_READ      0x0001
#define VFS_O_WRITE     0x0002
#define VFS_O_CREATE    0x0004
#define VFS_O_TRUNC     0x0008

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

static inline int sys_task_create(const char *name, void (*fn)(void)) {
    int64_t ret;
    asm volatile(
        "movq %1, %%rax\n\t"
        "movq %2, %%rdi\n\t"
        "movq %3, %%rsi\n\t"
        "int $0x80\n\t"
        "movq %%rax, %0"
        : "=r"(ret)
        : "r"((int64_t)101), "r"((int64_t)fn), "r"((int64_t)name)
        : "rax", "rdi", "rsi", "memory"
    );
    return (int)ret;
}

static inline void sys_task_sleep(uint64_t ms) {
    asm volatile(
        "movq %0, %%rax\n\t"
        "movq %1, %%rdi\n\t"
        "int $0x80"
        :: "r"((int64_t)103), "r"((int64_t)ms)
        : "rax", "rdi", "memory"
    );
}

#define sys_open(path, flags) \
    open(path, \
         (((flags) & VFS_O_READ) && ((flags) & VFS_O_WRITE)) ? O_RDWR : \
         ((flags) & VFS_O_WRITE) ? O_WRONLY : O_RDONLY | \
         (((flags) & VFS_O_CREATE) ? O_CREAT : 0) | \
         (((flags) & VFS_O_TRUNC) ? O_TRUNC : 0), 0666)

#define sys_close(fd) close(fd)
#define sys_read(fd, buf, len) read(fd, buf, len)
#define sys_write(fd, buf, len) write(fd, buf, len)
#define sys_mmap(addr, len, prot, fl, fd, o) mmap(addr, len, prot, fl, fd, o)
#define sys_socket(dom, type, proto) socket(dom, type, proto)
#define sys_bind(fd, addr, len) bind(fd, (const struct sockaddr *)(addr), len)
#define sys_listen(fd, backlog) listen(fd, backlog)
#define sys_accept(fd, addr, len) accept(fd, (struct sockaddr *)(addr), (socklen_t *)(len))
#define sys_connect(fd, addr, len) connect(fd, (const struct sockaddr *)(addr), len)
#define sys_sendmsg(fd, msg, flags) sendmsg(fd, msg, flags)
#define sys_recvmsg(fd, msg, flags) recvmsg(fd, msg, flags)
#define sys_ftruncate(fd, len) ftruncate(fd, len)
#define sys_exit(status) exit(status)

#define print(s) printf("%s", s)

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

/* Background Client Task simulating a Wayland Client */
static void client_thread(void) {
    sys_task_sleep(400); /* wait for tinywl compositor socket to bind */
    print("[Wayland-Client] Connecting to tinywl socket /var/run/wayland-0...\n");
    
    int sock = sys_socket(1, 1, 0); // AF_UNIX, SOCK_STREAM
    if (sock < 0) {
        print("[Wayland-Client] ERROR: Socket creation failed\n");
        sys_exit(1);
    }
    
    char sock_path[] = "/var/run/wayland-0";
    if (sys_connect(sock, sock_path, sizeof(sock_path)) < 0) {
        print("[Wayland-Client] ERROR: Connection failed\n");
        sys_exit(1);
    }
    
    print("[Wayland-Client] Connected! Initializing Shared Memory Segment...\n");
    char shm_path[] = "/dev/shm/wlr-client-shm";
    int shm_fd = sys_open(shm_path, VFS_O_READ | VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (shm_fd < 0) {
        print("[Wayland-Client] ERROR: Shared Memory creation failed\n");
        sys_exit(1);
    }
    
    uint32_t width = 360;
    uint32_t height = 270;
    size_t size = width * height * 4;
    sys_ftruncate(shm_fd, size);
    
    uint32_t *shm_buf = (uint32_t *)sys_mmap(NULL, size, 3, 1, shm_fd, 0);
    if ((intptr_t)shm_buf <= 0) {
        print("[Wayland-Client] ERROR: Client buffer mmap failed\n");
        sys_exit(1);
    }
    
    print("[Wayland-Client] Mapped layout. Sending buffer FD over Unix socket...\n");
    struct client_payload payload;
    payload.width = width;
    payload.height = height;
    payload.x = 332;
    payload.y = 249;
    
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
        print("[Wayland-Client] ERROR: sendmsg failed\n");
        sys_exit(1);
    }
    
    print("[Wayland-Client] Client active. Blitting rotating color wheel gradient...\n");
    uint32_t tick = 0;
    while (1) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint8_t r = (uint8_t)(x + tick);
                uint8_t g = (uint8_t)(y - tick);
                uint8_t b = (uint8_t)(x * y + tick);
                shm_buf[y * width + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
        tick += 3;
        sys_task_sleep(16);
    }
}

/* GUI rendering helpers */
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
        uint8_t r = 0x1E;
        uint8_t g = 0x22;
        uint8_t b = (uint8_t)(0x2B + (y * 0x3E / 768));
        uint32_t color = (0xFF << 24) | (r << 16) | (g << 8) | b;
        for (uint32_t x = 0; x < 1024; x++) {
            fb[y * 1024 + x] = color;
        }
    }
}

/* Sleek hardware-like pointer sprite representation (16x16) */
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
            if (px == 1) fb[(cy + y) * 1024 + (cx + x)] = 0xFFFFFFFF;
            else if (px == 2) fb[(cy + y) * 1024 + (cx + x)] = 0xFF4CAF50; // Neon green pointer
        }
    }
}

/* Compositor Entrypoint */
void _start(void) {
    print("[tinywl] Initializing wlroots-based wayland compositor...\n");
    
    struct wl_display *display = wl_display_create();
    if (!display) {
        print("[tinywl] ERROR: Failed to create display\n");
        sys_exit(1);
    }
    
    struct wlr_backend *backend = wlr_backend_autocreate(display);
    if (!backend) {
        print("[tinywl] ERROR: Failed to create VNL hardware backend\n");
        sys_exit(1);
    }
    
    struct wlr_renderer *renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        print("[tinywl] ERROR: Failed to create wlroots renderer\n");
        sys_exit(1);
    }
    
    wlr_backend_start(backend);
    print("[tinywl] VNL Hardware Backend started successfully\n");
    
    /* Bind Unix domain socket listener */
    int listen_sock = sys_socket(1, 1, 0); // AF_UNIX, SOCK_STREAM
    if (listen_sock < 0) {
        print("[tinywl] ERROR: Socket bind failed\n");
        sys_exit(1);
    }
    
    char sock_path[] = "/var/run/wayland-0";
    if (sys_bind(listen_sock, sock_path, sizeof(sock_path)) < 0) {
        print("[tinywl] ERROR: Socket bind address failed\n");
        sys_exit(1);
    }
    sys_listen(listen_sock, 8);
    
    /* Spin up client task thread */
    sys_task_create("wlr-test-client", client_thread);
    
    int client_sock = -1;
    uint32_t *client_fb = NULL;
    struct client_payload client_rect;
    client_rect.width = 0;
    client_rect.height = 0;
    
    int cx = 512, cy = 384;
    
    while (1) {
        /* Clear canvas using wlr_renderer clear interface */
        wlr_renderer_begin(renderer, 1024, 768);
        float clear_color[4] = { 0.11f, 0.13f, 0.17f, 1.0f };
        wlr_renderer_clear(renderer, clear_color);
        wlr_renderer_end(renderer);
        
        /* Render Background and desktop widgets */
        draw_gradient(backend->fb_mem);
        draw_rect(backend->fb_mem, 0, 0, 1024, 42, 0xFF181A1F); // header panel
        draw_rect(backend->fb_mem, 12, 10, 240, 22, 0xFF282C34); // text background
        
        /* Accept incoming wayland client connections */
        if (client_sock < 0) {
            client_sock = sys_accept(listen_sock, NULL, NULL);
        }
        
        /* Poll and read client buffer updates and layouts */
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
                    client_rect = payload;
                    
                    size_t shm_sz = payload.width * payload.height * 4;
                    client_fb = (uint32_t *)sys_mmap(NULL, shm_sz, 3, 1, shm_fd, 0);
                    if ((intptr_t)client_fb <= 0) {
                        client_fb = NULL;
                    }
                }
            }
        }
        
        /* Composite client buffer window (Zero-copy directly to framebuffer) */
        if (client_fb != NULL) {
            draw_rect(backend->fb_mem, client_rect.x + 8, client_rect.y + 8, client_rect.width, client_rect.height, 0x44000000); // Shadow
            draw_rect(backend->fb_mem, client_rect.x, client_rect.y - 24, client_rect.width, 24, 0xFF4CAF50); // Header
            draw_rect(backend->fb_mem, client_rect.x + 6, client_rect.y - 18, 12, 12, 0xFFF44336); // Close button
            
            for (uint32_t y = 0; y < client_rect.height; y++) {
                uint32_t sy = client_rect.y + y;
                if (sy >= 768) break;
                for (uint32_t x = 0; x < client_rect.width; x++) {
                    uint32_t sx = client_rect.x + x;
                    if (sx >= 1024) break;
                    backend->fb_mem[sy * 1024 + sx] = client_fb[y * client_rect.width + x];
                }
            }
        }
        
        /* Poll and inject hardware mouse movement events */
        struct wlr_pointer_motion_event motion;
        struct vnl_mouse_packet m_pkt;
        while (sys_read(backend->mouse_fd, &m_pkt, sizeof(m_pkt)) == sizeof(m_pkt)) {
            motion.delta_x = m_pkt.dx;
            motion.delta_y = -m_pkt.dy;
            
            cx += (int)motion.delta_x;
            cy += (int)motion.delta_y;
            if (cx < 0) cx = 0;
            if (cx > 1022) cx = 1022;
            if (cy < 0) cy = 0;
            if (cy > 766) cy = 766;
            
            /* Handle window dragging with mouse click */
            if ((m_pkt.buttons & 1) && client_fb != NULL) {
                if (cx >= client_rect.x && cx <= (int)(client_rect.x + client_rect.width) &&
                    cy >= (int)(client_rect.y - 24) && cy <= client_rect.y) {
                    client_rect.x += (int)motion.delta_x;
                    client_rect.y += (int)motion.delta_y;
                }
            }
        }
        
        /* Poll and inject keyboard key press events */
        uint8_t sc;
        struct wlr_keyboard_key_event key_ev;
        while (sys_read(backend->kbd_fd, &sc, 1) == 1) {
            key_ev.state = (sc & 0x80) ? 0 : 1;
            key_ev.keycode = sc & 0x7F;
            (void)key_ev;
        }
        
        /* Render cursor sprite */
        draw_cursor(backend->fb_mem, cx, cy);
        
        sys_task_sleep(8);
    }
}
