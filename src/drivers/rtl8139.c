#include "rtl8139.h"
#include "pci.h"
#include "printf.h"
#include "cpu.h"
#include "heap.h"
#include "string.h"
#include "idt.h"
#include "irq.h"
#include "vnet.h"

/* RTL8139 Registers */
#define RTL8139_MAC0 0x00
#define RTL8139_RBSTART 0x30
#define RTL8139_CR 0x37
#define RTL8139_CAPR 0x38
#define RTL8139_IMR 0x3C
#define RTL8139_ISR 0x3E
#define RTL8139_TCR 0x40
#define RTL8139_RCR 0x44
#define RTL8139_CONFIG1 0x52

#define RX_BUF_SIZE 8192
#define RX_BUF_TOTAL (RX_BUF_SIZE + 16 + 1500)

static uint32_t io_base = 0;
static uint8_t mac_addr[6];
static uint8_t *rx_buffer = NULL;
static uint32_t current_packet_ptr = 0;

static uint8_t *tx_buffers[4];
static int next_tx = 0;

static void rtl8139_handler(Registers *regs) {
    (void)regs;
    uint16_t status = inw(io_base + RTL8139_ISR);
    outw(io_base + RTL8139_ISR, status);

    if (status & 0x01) { /* ROK: Receive OK */
        while (!(inb(io_base + RTL8139_CR) & 0x01)) {
            uint16_t *header = (uint16_t *)(rx_buffer + current_packet_ptr);
            uint16_t packet_len = header[1];
            current_packet_ptr = (current_packet_ptr + packet_len + 4 + 3) & ~3;
            if (current_packet_ptr >= RX_BUF_SIZE) current_packet_ptr %= RX_BUF_SIZE;
            outw(io_base + RTL8139_CAPR, (uint16_t)(current_packet_ptr - 16));
        }
    }
    irq_eoi(11); /* Hardcoded IRQ 11 for QEMU default, should use dynamic IRQ from PCI */
}

void rtl8139_init(uint8_t bus, uint8_t dev, uint8_t func) {
    io_base = pci_read32(bus, dev, func, 0x10) & ~0x1;
    uint8_t irq = (uint8_t)(pci_read32(bus, dev, func, 0x3C) & 0xFF);

    kprintf("rtl8139: base=0x%x irq=%d\n", io_base, irq);

    /* Power on */
    outb(io_base + RTL8139_CONFIG1, 0x00);

    /* Software Reset */
    outb(io_base + RTL8139_CR, 0x10);
    while (inb(io_base + RTL8139_CR) & 0x10);

    /* Get MAC */
    for (int i = 0; i < 6; i++) mac_addr[i] = inb(io_base + RTL8139_MAC0 + i);
    kprintf("rtl8139: MAC %02x:%02x:%02x:%02x:%02x:%02x\n", 
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    vnet_set_mac(mac_addr);

    /* Init RX Buffer */
    rx_buffer = (uint8_t *)kmalloc(RX_BUF_TOTAL);
    memset(rx_buffer, 0, RX_BUF_TOTAL);
    outl(io_base + RTL8139_RBSTART, (uint32_t)(uintptr_t)rx_buffer);

    /* Set IMR + ISR */
    outw(io_base + RTL8139_IMR, 0x0005); /* ROK + TOK */

    /* Config Receive */
    outl(io_base + RTL8139_RCR, 0x0000008F); /* AB + AM + APM + AAP */

    /* Enable TX/RX */
    outb(io_base + RTL8139_CR, 0x0C);

    /* Init TX Buffers */
    for (int i = 0; i < 4; i++) tx_buffers[i] = (uint8_t *)kmalloc(1536);

    idt_set_handler(32 + irq, rtl8139_handler);
    irq_unmask(irq);
}

void rtl8139_send_packet(void *data, uint32_t len) {
    if (!io_base) return;
    memcpy(tx_buffers[next_tx], data, len);
    outl(io_base + 0x20 + (next_tx * 4), (uint32_t)(uintptr_t)tx_buffers[next_tx]);
    outl(io_base + 0x10 + (next_tx * 4), len);
    next_tx = (next_tx + 1) % 4;
}
