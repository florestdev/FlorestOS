#pragma once
#include "types.h"

/* Unified NIC API — e1000 / RTL8139 / VirtIO-net */

bool        nic_init(void);
bool        nic_send(const void* data, size_t len);
size_t      nic_recv(void* buf, size_t max_len);
void        nic_get_mac(uint8_t mac[6]);
bool        nic_link_up(void);
const char* nic_name(void);   /* "e1000" / "rtl8139" / "virtio-net" / "none" */
