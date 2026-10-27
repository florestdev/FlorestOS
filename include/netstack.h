#pragma once
#include "types.h"

/* ============================================================
 * FlorestOS real network stack (minimal)
 * Works with QEMU -netdev user + e1000
 * Guest IP: 10.0.2.15  Gateway: 10.0.2.2
 * ============================================================ */

#define NET_MTU 1500

/* Our addresses (QEMU user networking defaults) */
#define OUR_IP      0x0A00020F   /* 10.0.2.15 */
#define GATEWAY_IP  0x0A000202   /* 10.0.2.2  */
#define NETMASK     0xFFFFFF00   /* 255.255.255.0 */

void net_init(void);
void net_poll(void);   /* call periodically to process RX */

/* UDP: send datagram, wait up to timeout_ms for reply into reply_buf.
 * Returns bytes received or -1 on error / timeout. */
int net_udp_request(uint32_t dst_ip, uint16_t dst_port,
                    const void* payload, size_t payload_len,
                    void* reply_buf, size_t reply_max,
                    uint32_t timeout_ms);

/* TCP client (minimal): connect, send data, read response until timeout or close.
 * Returns bytes received or -1. */
int net_tcp_request(uint32_t dst_ip, uint16_t dst_port,
                    const void* payload, size_t payload_len,
                    void* reply_buf, size_t reply_max,
                    uint32_t timeout_ms);

void net_print_status(void);

/* TCP port probe: 1=open (SYN-ACK), 0=closed (RST), -1=filtered/timeout */
int net_tcp_probe(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms);
