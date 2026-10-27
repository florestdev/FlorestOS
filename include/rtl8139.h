#pragma once
#include "types.h"

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

bool rtl8139_init(void);
bool rtl8139_send(const void* data, size_t len);
size_t rtl8139_recv(void* buf, size_t max_len);
void rtl8139_get_mac(uint8_t mac[6]);
bool rtl8139_link_up(void);
