#pragma once
#include "types.h"

#define VIRTIO_VENDOR     0x1AF4
#define VIRTIO_NET_DEVICE 0x1000  /* legacy virtio-net */

bool virtio_net_init(void);
bool virtio_net_send(const void* data, size_t len);
size_t virtio_net_recv(void* buf, size_t max_len);
void virtio_net_get_mac(uint8_t mac[6]);
bool virtio_net_link_up(void);
