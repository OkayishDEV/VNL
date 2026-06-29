/*
 * wtf is this vibe? not linux apparently.
 * just boot the damn thing.
 */
#include "types.h"
#include "vga.h"
#include "serial.h"
#include "printf.h"
#include "gdt.h"
#include "idt.h"
#include "cpu.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "fb.h"
#include "syscall.h"
#include "panic.h"
#include "vfs.h"
#include "pci.h"
#include "acpi.h"
#include "sched.h"
#include "devfs.h"
#include "unix_socket.h"
#include "gui_env.h"
#include "mouse.h"
#include "rtl8139.h"
#include "shm.h"

extern uint8_t kernel_end[];
void shell_run(void);
void irq_init(void);

/* multiboot2 garbage parsing */
#define MB2_MAGIC_VAL 0x36D76289   /* bootloader → kernel magic value */
#define MB2_TAG_END   0
#define MB2_TAG_MEM   4    /* Basic memory information */
#define MB2_TAG_MMAP  6    /* Memory map */

typedef struct PACKED { uint32_t type; uint32_t size; } MB2Tag;
typedef struct PACKED {
    MB2Tag hdr;
    uint32_t mem_lower;   /* KiB below 1 MiB */
    uint32_t mem_upper;   /* KiB above 1 MiB */
} MB2TagMem;

typedef struct PACKED {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint8_t  reserved;
} MB2TagFB;

uint64_t phys_framebuffer_addr = 0;
uint32_t fb_width = 0;
uint32_t fb_height = 0;
uint32_t fb_pitch = 0;
uint8_t fb_bpp = 0;

uint32_t* video_memory = NULL;

static void fill_screen(uint32_t color) {
    if (!video_memory) return;
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t *row = (uint32_t *)((uintptr_t)video_memory + y * fb_pitch);
        for (uint32_t x = 0; x < fb_width; x++) {
            row[x] = color;
        }
    }
}

static void parse_mb2_mmap(uint64_t mb_info_phys) {
    uint8_t *ptr = (uint8_t *)mb_info_phys;
    uint32_t total_size = *(uint32_t *)ptr;
    uint8_t *end = ptr + total_size;
    ptr += 8;

    while (ptr < end) {
        MB2Tag *tag = (MB2Tag *)ptr;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MMAP) {
            struct {
                MB2Tag hdr;
                uint32_t entry_size;
                uint32_t entry_version;
                struct {
                    uint64_t addr;
                    uint64_t len;
                    uint32_t type;
                    uint32_t zero;
                } entries[];
            } *mmap = (void *)tag;
            int num_entries = (tag->size - 16) / mmap->entry_size;
            for (int i = 0; i < num_entries; i++) {
                /* Type 1 is Usable RAM */
                if (mmap->entries[i].type != 1) {
                    pmm_reserve(mmap->entries[i].addr, mmap->entries[i].len);
                }
            }
        }
        ptr += ALIGN_UP(tag->size, 8);
    }
}

static uint64_t parse_mb2_memory(uint64_t mb_info_phys)
{
    /* mb_info_phys is still identity-mapped (below 4 GiB) */
    uint8_t *ptr = (uint8_t *)mb_info_phys;
    uint32_t total_size = *(uint32_t *)ptr;
    uint8_t *end = ptr + total_size;
    ptr += 8; /* skip fixed part (total_size + reserved) */

    while (ptr < end) {
        MB2Tag *tag = (MB2Tag *)ptr;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MEM) {
            MB2TagMem *m = (MB2TagMem *)tag;
            /* mem_upper is in KiB above 1 MiB; add 1024 for the first MiB */
            return (uint64_t)m->mem_upper + 1024;
        }
        ptr += ALIGN_UP(tag->size, 8);
    }
    /* Fallback: assume 128 MiB */
    return 128 * 1024;
}

/* shitty logo */
static void print_banner(void)
{
    vga_set_color(VGA_LGREEN, VGA_BLACK);
    kprintf(
        "\n"
        "  ____   ____  _   _   _  __  __\n"
        " / ___| / ___|| \\ | | | ||  \\/  |\n"
        " \\___ \\| |    |  \\| | | || |\\/| |\n"
        "  ___) | |___ | |\\  | | || |  | |\n"
        " |____/ \\____||_| \\_| |_||_|  |_|\n"
        "\n"
    );
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("  Vibe Not Linux v0.3.0 - 64-bit kernel\n");
    kprintf("  Built: " __DATE__ " " __TIME__ "\n\n");
    vga_set_color(VGA_LGREEN, VGA_BLACK);
}

/* point of no return */
void kernel_main(uint64_t mb_info)
{
    /* Step 1: Early output */
    serial_init();
    vga_init();

    /* Parse the Framebuffer Tag */
    extern uint32_t mb2_save_magic;
    if (mb_info) {
        if (mb2_save_magic == 0x2BADB002) {
            Multiboot1Info *m1 = (Multiboot1Info *)mb_info;
            if (m1 && (m1->flags & (1 << 12))) {
                phys_framebuffer_addr = m1->framebuffer_addr;
                fb_width = m1->framebuffer_width;
                fb_height = m1->framebuffer_height;
                fb_pitch = m1->framebuffer_pitch;
                fb_bpp = m1->framebuffer_bpp;
            }
        } else {
            uint8_t *ptr = (uint8_t *)mb_info;
            uint32_t total_size = *(uint32_t *)ptr;
            uint8_t *end = ptr + total_size;
            ptr += 8;
            while (ptr < end) {
                MB2Tag *tag = (MB2Tag *)ptr;
                if (tag->type == 0) {
                    break;
                }
                if (tag->type == 8) {
                    MB2TagFB *t = (MB2TagFB *)ptr;
                    phys_framebuffer_addr = t->addr;
                    fb_width = t->width;
                    fb_height = t->height;
                    fb_pitch = t->pitch;
                    fb_bpp = t->bpp;
                }
                ptr = (uint8_t *)(((uintptr_t)ptr + tag->size + 7) & ~7);
            }
        }
    }

    /* Step 2: GDT */
    kprintf("[INIT] GDT...\n");
    gdt_init();

    /* Step 3: IDT */
    kprintf("[INIT] IDT...\n");
    idt_init();

    /* Step 4: IRQ (PIC remapping) */
    kprintf("[INIT] IRQ/PIC...\n");
    irq_init();

    /* Step 5: Physical memory manager */
    kprintf("[INIT] PMM...\n");
    uint64_t mem_kb = 128 * 1024;
    if (mb_info) {
        if (mb2_save_magic == 0x2BADB002) {
            Multiboot1Info *m1 = (Multiboot1Info *)mb_info;
            if (m1 && (m1->flags & 1)) {
                mem_kb = m1->mem_lower + m1->mem_upper;
            }
        } else {
            mem_kb = parse_mb2_memory(mb_info);
        }
    }
    pmm_init(mem_kb);
    if (mb_info && mb2_save_magic != 0x2BADB002) parse_mb2_mmap(mb_info);

    /* hide from vmm so it doesn't break everything */
    uint64_t kern_phys_end = (uint64_t)kernel_end - 0xFFFFFFFF80000000ULL;
    pmm_reserve(0x100000, kern_phys_end - 0x100000);

    kprintf("       %llu MiB RAM detected (%llu free pages)\n",
            (pmm_total_pages() * PAGE_SIZE) / (1024*1024),
            pmm_free_pages());

    kprintf("[INIT] Framebuffer...\n");
    if (mb_info && fb_multiboot_init(mb_info)) {
        fb_console_reset();
        vga_export_fb_mirror_once();
        kprintf("       linear framebuffer OK (mirrored for VNC)\n");
    } else
        kprintf("       (none - text console only)\n");

    print_banner();

    /* Step 6: Virtual memory manager */
    kprintf("[INIT] VMM...\n");
    vmm_init();

    if (phys_framebuffer_addr != 0 && fb_height > 0 && fb_pitch > 0) {
        uint64_t fb_size = (uint64_t)fb_height * fb_pitch;
        uint64_t phys_start = ALIGN_DOWN(phys_framebuffer_addr, PAGE_SIZE);
        uint64_t phys_end = ALIGN_UP(phys_framebuffer_addr + fb_size, PAGE_SIZE);
        uint64_t map_size = phys_end - phys_start;
        uint64_t virt_start = 0xFFFFFFFFD0000000ULL;

        for (uint64_t offset = 0; offset < map_size; offset += PAGE_SIZE) {
            vmm_map(virt_start + offset, phys_start + offset, VMM_FLAG_WRITE);
        }
        video_memory = (uint32_t *)(virt_start + (phys_framebuffer_addr - phys_start));

        extern uint8_t *s_fb_virt;
        s_fb_virt = (uint8_t *)video_memory;

        /* Test mapping: fill screen with blue (0x000000FF) */
        fill_screen(0x000000FF);
    }

    /* Step 7: Kernel heap */
    kprintf("[INIT] Heap...\n");
    /* heap somewhere up there who cares */
    heap_init(0xFFFFC00000000000ULL, 4 * PAGE_SIZE);

    /* Step 8: Timer */
    kprintf("[INIT] Timer (1000 Hz)...\n");
    timer_init(1000);

    /* Step 9: Keyboard */
    kprintf("[INIT] Keyboard...\n");
    keyboard_init();

    /* Step 9b: Mouse */
    kprintf("[INIT] Mouse...\n");
    mouse_init();

    /* Step 10: VFS */
    kprintf("[INIT] VFS (ramfs)...\n");
    vfs_init();

    kprintf("[INIT] devfs (/dev/fb0, /dev/null)...\n");
    devfs_init();

    /* Open standard streams for serial output */
    vfs_open_std("/dev/ttyS0", VFS_O_READ, 0);
    vfs_open_std("/dev/ttyS0", VFS_O_WRITE, 1);
    vfs_open_std("/dev/ttyS0", VFS_O_WRITE, 2);



    kprintf("[INIT] AF_UNIX sockets + GUI environment layout...\n");
    shm_init();
    unix_socket_init();
    gui_environment_init();

    /* Step 11: PCI */
    kprintf("[INIT] PCI...\n");
    pci_init();

    /* Step 12: ACPI */
    kprintf("[INIT] ACPI...\n");
    acpi_init();

    /* Step 12b: RTL8139 Network */
    kprintf("[INIT] Networking (RTL8139)...\n");
    for (int i = 0; i < pci_dev_count(); i++) {
        const PCIDevice *d = pci_get_dev(i);
        if (d->vendor == 0x10EC && d->device_id == 0x8139) {
            rtl8139_init(d->bus, d->dev, d->func);
            break;
        }
    }

    /* Step 13: Syscall */
    kprintf("[INIT] Syscall (INT 0x80)...\n");
    syscall_init();

    /* Step 14: Scheduler (installs sched_timer_stub at IRQ0) */
    kprintf("[INIT] Scheduler...\n");
    sched_init();

    /* Step 15: Enable interrupts */
    kprintf("[INIT] Enabling interrupts...\n");
    sti_asm();

    kprintf("[INIT] All subsystems ready.\n\n");

    /* Step 16: Drop into shell (runs as task 0) */
    shell_run();

    /* Should never reach here */
    kpanic("kernel_main returned!");
}
