#pragma once
#include "types.h"

/* Intel 82540EM / e1000 as emulated by QEMU */

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

bool e1000_init(void);
bool e1000_send(const void* data, size_t len);
/* Non-blocking receive. Returns length or 0 if no packet. */
size_t e1000_recv(void* buf, size_t max_len);

void e1000_get_mac(uint8_t mac[6]);
bool e1000_link_up(void);
