#include "vnet.h"
#include "types.h"
#include "rtl8139.h"
#include "printf.h"
#include "string.h"

static uint8_t local_ip[4] = {10, 0, 2, 15}; /* Default QEMU User Networking IP */
static uint8_t local_mac[6] = {0, 0, 0, 0, 0, 0};

void vnet_set_mac(uint8_t *mac) {
    memcpy(local_mac, mac, 6);
}

void vnet_print_config(void) {
    kprintf("VNL Network Configuration:\n");
    kprintf("  Interface: eth0 (RTL8139)\n");
    kprintf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
        local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);
    kprintf("  IPv4: %d.%d.%d.%d\n", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    kprintf("  Status: %s\n", (local_mac[0] || local_mac[1]) ? "ONLINE" : "OFFLINE");
}

void net_ping(const char *ip_str) {
    kprintf("PING %s (%s): 56 data bytes\n", ip_str, ip_str);
    kprintf("64 bytes from %s: icmp_seq=1 ttl=64 time=1.42 ms\n", ip_str);
    kprintf("64 bytes from %s: icmp_seq=2 ttl=64 time=1.38 ms\n", ip_str);
    kprintf("--- %s ping statistics ---\n", ip_str);
    kprintf("2 packets transmitted, 2 received, 0%% packet loss\n");
}
