#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"

void rtl8139_init(uint8_t bus, uint8_t dev, uint8_t func);
void rtl8139_send_packet(void *data, uint32_t len);

#endif
