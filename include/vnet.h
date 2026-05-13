#ifndef VNET_H
#define VNET_H

#include "types.h"

void vnet_set_mac(uint8_t *mac);
void vnet_print_config(void);
void net_ping(const char *ip_str);

#endif
