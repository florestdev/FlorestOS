#pragma once
#include "types.h"

/* Resolve hostname via DNS A-query. dns_server host-order IP (0 = 10.0.2.3 QEMU DNS).
 * On success writes IPv4 host-order to out_ip and returns 0. */
int dns_resolve(const char* hostname, uint32_t dns_server, uint32_t* out_ip);
