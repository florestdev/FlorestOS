#include "netstack.h"
#include "nic.h"
#include "string.h"
#include "timer.h"
#include "vga.h"
#include "types.h"

/* ---- Protocol headers (network byte order fields) ---- */

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;          /* 0x0800 IP, 0x0806 ARP */
} __attribute__((packed));

struct arp_pkt {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src;
    uint32_t dst;
} __attribute__((packed));

struct udp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_hdr {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;      /* high 4 bits = size in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* ---- Byte order ---- */
static uint16_t htons(uint16_t x) { return (x << 8) | (x >> 8); }
static uint16_t ntohs(uint16_t x) { return htons(x); }
static uint32_t htonl(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x & 0xFF0000) >> 8) | ((x >> 24) & 0xFF);
}
static uint32_t ntohl(uint32_t x) { return htonl(x); }

static uint16_t checksum(const void* data, size_t len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len) sum += *(const uint8_t*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* Pseudo-header checksum for TCP/UDP */
static uint16_t transport_checksum(uint32_t src, uint32_t dst, uint8_t proto,
                                   const void* hdr, size_t hdr_len,
                                   const void* payload, size_t payload_len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += htons(proto);
    sum += htons((uint16_t)(hdr_len + payload_len));

    const uint16_t* p = (const uint16_t*)hdr;
    size_t len = hdr_len;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t*)p;

    p = (const uint16_t*)payload;
    len = payload_len;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t*)p;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ---- ARP cache (tiny) ---- */
#define ARP_CACHE_SIZE 8
struct arp_entry {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
};
static arp_entry arp_cache[ARP_CACHE_SIZE];
static uint8_t our_mac[6];

static void arp_cache_put(uint32_t ip, const uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            return;
        }
    }
    /* overwrite 0 */
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = true;
}

static bool arp_cache_get(uint32_t ip, uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

static void arp_send_request(uint32_t target_ip) {
    uint8_t frame[sizeof(eth_hdr) + sizeof(arp_pkt)];
    eth_hdr* eth = (eth_hdr*)frame;
    arp_pkt* arp = (arp_pkt*)(frame + sizeof(eth_hdr));

    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, our_mac, 6);
    eth->type = htons(ETH_TYPE_ARP);

    arp->htype = htons(1);
    arp->ptype = htons(ETH_TYPE_IP);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(ARP_OP_REQUEST);
    memcpy(arp->sha, our_mac, 6);
    arp->spa = htonl(OUR_IP);
    memset(arp->tha, 0, 6);
    arp->tpa = htonl(target_ip);

    nic_send(frame, sizeof(frame));
}

static bool arp_resolve(uint32_t ip, uint8_t mac[6], uint32_t timeout_ms) {
    if (arp_cache_get(ip, mac)) return true;

    /* Same subnet? else use gateway */
    uint32_t target = ip;
    if ((ip & NETMASK) != (OUR_IP & NETMASK))
        target = GATEWAY_IP;

    if (arp_cache_get(target, mac)) return true;

    arp_send_request(target);

    uint32_t waited = 0;
    uint8_t buf[2048];
    while (waited < timeout_ms) {
        net_poll(); /* process any ARP replies */
        if (arp_cache_get(target, mac)) return true;
        timer_sleep_ms(10);
        waited += 10;
        /* retransmit occasionally */
        if (waited % 200 == 0) arp_send_request(target);
        (void)buf;
    }
    return false;
}

/* ---- RX processing ---- */
static uint8_t rx_packet[2048];
static size_t  rx_packet_len = 0;
static bool    rx_has_udp = false;
static bool    rx_has_tcp = false;
static uint16_t rx_udp_dst_port = 0;
static uint16_t rx_tcp_dst_port = 0;
static uint32_t rx_tcp_seq = 0;
static uint32_t rx_tcp_ack = 0;
static uint8_t  rx_tcp_flags = 0;
static uint8_t  rx_payload[1500];
static size_t   rx_payload_len = 0;
static uint32_t rx_src_ip = 0;
static uint16_t rx_src_port = 0;

void net_poll(void) {
    for (int round = 0; round < 16; round++) {
        size_t len = nic_recv(rx_packet, sizeof(rx_packet));
        if (len < sizeof(eth_hdr)) return;

        eth_hdr* eth = (eth_hdr*)rx_packet;
        uint16_t type = ntohs(eth->type);

        if (type == ETH_TYPE_ARP && len >= sizeof(eth_hdr) + sizeof(arp_pkt)) {
            arp_pkt* arp = (arp_pkt*)(rx_packet + sizeof(eth_hdr));
            if (ntohs(arp->oper) == ARP_OP_REPLY) {
                arp_cache_put(ntohl(arp->spa), arp->sha);
            } else if (ntohs(arp->oper) == ARP_OP_REQUEST &&
                       ntohl(arp->tpa) == OUR_IP) {
                uint8_t frame[sizeof(eth_hdr) + sizeof(arp_pkt)];
                eth_hdr* e = (eth_hdr*)frame;
                arp_pkt* a = (arp_pkt*)(frame + sizeof(eth_hdr));
                memcpy(e->dst, eth->src, 6);
                memcpy(e->src, our_mac, 6);
                e->type = htons(ETH_TYPE_ARP);
                a->htype = htons(1);
                a->ptype = htons(ETH_TYPE_IP);
                a->hlen = 6;
                a->plen = 4;
                a->oper = htons(ARP_OP_REPLY);
                memcpy(a->sha, our_mac, 6);
                a->spa = htonl(OUR_IP);
                memcpy(a->tha, arp->sha, 6);
                a->tpa = arp->spa;
                nic_send(frame, sizeof(frame));
                arp_cache_put(ntohl(arp->spa), arp->sha);
            }
            continue;
        }

        if (type != ETH_TYPE_IP || len < sizeof(eth_hdr) + sizeof(ip_hdr))
            continue;

        ip_hdr* ip = (ip_hdr*)(rx_packet + sizeof(eth_hdr));
        if ((ip->ver_ihl >> 4) != 4) continue;
        size_t ihl = (ip->ver_ihl & 0xF) * 4;
        if (len < sizeof(eth_hdr) + ihl) continue;

        uint32_t src = ntohl(ip->src);
        uint32_t dst = ntohl(ip->dst);
        if (dst != OUR_IP && dst != 0xFFFFFFFF)
            continue;
        arp_cache_put(src, eth->src);

        if (ip->protocol == IP_PROTO_UDP) {
            if (len < sizeof(eth_hdr) + ihl + sizeof(udp_hdr)) continue;
            udp_hdr* udp = (udp_hdr*)(rx_packet + sizeof(eth_hdr) + ihl);
            size_t plen = ntohs(udp->length);
            if (plen < sizeof(udp_hdr)) continue;
            plen -= sizeof(udp_hdr);
            const uint8_t* payload = rx_packet + sizeof(eth_hdr) + ihl + sizeof(udp_hdr);
            if (sizeof(eth_hdr) + ihl + sizeof(udp_hdr) + plen > len)
                plen = len - (sizeof(eth_hdr) + ihl + sizeof(udp_hdr));

            rx_has_udp = true;
            rx_udp_dst_port = ntohs(udp->dst_port);
            rx_src_ip = src;
            rx_src_port = ntohs(udp->src_port);
            rx_payload_len = plen > sizeof(rx_payload) ? sizeof(rx_payload) : plen;
            memcpy(rx_payload, payload, rx_payload_len);
        } else if (ip->protocol == IP_PROTO_TCP) {
            if (len < sizeof(eth_hdr) + ihl + sizeof(tcp_hdr)) continue;
            tcp_hdr* tcp = (tcp_hdr*)(rx_packet + sizeof(eth_hdr) + ihl);
            size_t doff = (tcp->data_off >> 4) * 4;
            if (doff < sizeof(tcp_hdr)) continue;
            size_t total_ip = ntohs(ip->total_len);
            size_t plen = 0;
            if (total_ip > ihl + doff)
                plen = total_ip - ihl - doff;
            const uint8_t* payload = rx_packet + sizeof(eth_hdr) + ihl + doff;

            rx_has_tcp = true;
            rx_tcp_dst_port = ntohs(tcp->dst_port);
            rx_tcp_seq = ntohl(tcp->seq);
            rx_tcp_ack = ntohl(tcp->ack);
            rx_tcp_flags = tcp->flags;
            rx_src_ip = src;
            rx_src_port = ntohs(tcp->src_port);
            rx_payload_len = plen > sizeof(rx_payload) ? sizeof(rx_payload) : plen;
            if (rx_payload_len)
                memcpy(rx_payload, payload, rx_payload_len);
        }
    }
}

/* ---- Send helpers ---- */
static bool send_ip_packet(uint32_t dst_ip, uint8_t proto,
                           const void* transport, size_t tlen,
                           const void* payload, size_t plen) {
    uint8_t dst_mac[6];
    if (!arp_resolve(dst_ip, dst_mac, 2000)) {
        vga_print_color("[NET] ARP failed\n", 0x0C, 0x00);
        return false;
    }

    uint8_t frame[1518];
    eth_hdr* eth = (eth_hdr*)frame;
    ip_hdr* ip = (ip_hdr*)(frame + sizeof(eth_hdr));

    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, our_mac, 6);
    eth->type = htons(ETH_TYPE_IP);

    size_t ip_total = sizeof(ip_hdr) + tlen + plen;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons((uint16_t)ip_total);
    ip->id = htons(1);
    ip->frag = 0;
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    ip->src = htonl(OUR_IP);
    ip->dst = htonl(dst_ip);
    ip->checksum = checksum(ip, sizeof(ip_hdr));

    memcpy(frame + sizeof(eth_hdr) + sizeof(ip_hdr), transport, tlen);
    if (plen)
        memcpy(frame + sizeof(eth_hdr) + sizeof(ip_hdr) + tlen, payload, plen);

    return nic_send(frame, sizeof(eth_hdr) + ip_total);
}

/* ---- Public API ---- */

void net_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    if (!nic_init()) {
        vga_print_color("[NET] No NIC, network disabled\n", 0x0C, 0x00);
        return;
    }
    nic_get_mac(our_mac);

    /* Seed gateway ARP via request */
    arp_send_request(GATEWAY_IP);

    vga_print_color("[NET] Stack ready  IP=10.0.2.15  GW=10.0.2.2\n", 0x0A, 0x00);
}

void net_print_status(void) {
    vga_print("NIC: ");
    vga_print(nic_name());
    vga_print("\n");
    vga_print("IP: 10.0.2.15  Gateway: 10.0.2.2  (QEMU user net)\n");
    vga_print("Link: ");
    vga_print(nic_link_up() ? "UP" : "DOWN");
    vga_print("\n");
}


int net_udp_request(uint32_t dst_ip, uint16_t dst_port,
                    const void* payload, size_t payload_len,
                    void* reply_buf, size_t reply_max,
                    uint32_t timeout_ms) {
    static uint16_t local_port = 40000;
    local_port++;
    if (local_port < 40000) local_port = 40000;

    udp_hdr udp;
    udp.src_port = htons(local_port);
    udp.dst_port = htons(dst_port);
    udp.length = htons((uint16_t)(sizeof(udp_hdr) + payload_len));
    udp.checksum = 0;
    /* optional checksum */
    udp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_UDP,
                                      &udp, sizeof(udp), payload, payload_len);
    if (udp.checksum == 0) udp.checksum = 0xFFFF;

    rx_has_udp = false;
    if (!send_ip_packet(dst_ip, IP_PROTO_UDP, &udp, sizeof(udp), payload, payload_len))
        return -1;

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        net_poll();
        if (rx_has_udp && rx_udp_dst_port == local_port) {
            size_t n = rx_payload_len;
            if (n > reply_max) n = reply_max;
            memcpy(reply_buf, rx_payload, n);
            rx_has_udp = false;
            return (int)n;
        }
        timer_sleep_ms(10);
        waited += 10;
    }
    return -1; /* timeout */
}

/* Minimal TCP client: SYN -> SYN-ACK -> ACK -> send data -> read until FIN/timeout */
int net_tcp_request(uint32_t dst_ip, uint16_t dst_port,
                    const void* payload, size_t payload_len,
                    void* reply_buf, size_t reply_max,
                    uint32_t timeout_ms) {
    static uint16_t local_port = 50000;
    local_port++;
    if (local_port < 50000) local_port = 50000;

    uint32_t seq = 1000;
    uint32_t ack = 0;

    vga_print("[TCP] ARP...\n");
    /* SYN */
    {
        tcp_hdr tcp;
        memset(&tcp, 0, sizeof(tcp));
        tcp.src_port = htons(local_port);
        tcp.dst_port = htons(dst_port);
        tcp.seq = htonl(seq);
        tcp.ack = 0;
        tcp.data_off = (5 << 4);
        tcp.flags = TCP_SYN;
        tcp.window = htons(8192);
        tcp.checksum = 0;
        tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                          &tcp, sizeof(tcp), nullptr, 0);

        rx_has_tcp = false;
        vga_print("[TCP] SYN...\n");
        if (!send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0))
            return -1;
    }

    /* Wait SYN-ACK */
    vga_print("[TCP] wait SYN-ACK...\n");
    uint32_t waited = 0;
    bool got_synack = false;
    while (waited < timeout_ms) {
        net_poll();
        if (rx_has_tcp && rx_tcp_dst_port == local_port &&
            rx_src_ip == dst_ip && rx_src_port == dst_port &&
            (rx_tcp_flags & TCP_SYN) && (rx_tcp_flags & TCP_ACK) &&
            rx_tcp_ack == seq + 1) {
            ack = rx_tcp_seq + 1;
            seq = rx_tcp_ack;
            got_synack = true;
            rx_has_tcp = false;
            break;
        }
        if (rx_has_tcp && rx_tcp_dst_port == local_port &&
            rx_src_ip == dst_ip && rx_src_port == dst_port &&
            (rx_tcp_flags & TCP_RST)) {
            rx_has_tcp = false;
            return -1; /* refused */
        }
        timer_sleep_ms(10);
        waited += 10;
    }
    if (!got_synack) {
        vga_print_color("[TCP] SYN-ACK timeout\n", 0x0C, 0x00);
        return -1;
    }

    /* ACK */
    {
        tcp_hdr tcp;
        memset(&tcp, 0, sizeof(tcp));
        tcp.src_port = htons(local_port);
        tcp.dst_port = htons(dst_port);
        tcp.seq = htonl(seq);
        tcp.ack = htonl(ack);
        tcp.data_off = (5 << 4);
        tcp.flags = TCP_ACK;
        tcp.window = htons(8192);
        tcp.checksum = 0;
        tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                          &tcp, sizeof(tcp), nullptr, 0);
        send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0);
    }

    /* Send data + PSH+ACK */
    if (payload_len > 0) {
        tcp_hdr tcp;
        memset(&tcp, 0, sizeof(tcp));
        tcp.src_port = htons(local_port);
        tcp.dst_port = htons(dst_port);
        tcp.seq = htonl(seq);
        tcp.ack = htonl(ack);
        tcp.data_off = (5 << 4);
        tcp.flags = TCP_PSH | TCP_ACK;
        tcp.window = htons(8192);
        tcp.checksum = 0;
        tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                          &tcp, sizeof(tcp), payload, payload_len);
        if (!send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), payload, payload_len))
            return -1;
        seq += (uint32_t)payload_len;
    }

    /* Collect response */
    vga_print("[TCP] waiting data...\n");
    size_t total = 0;
    waited = 0;
    uint32_t idle = 0;
    while (waited < timeout_ms && total < reply_max) {
        net_poll();
        if (rx_has_tcp && rx_tcp_dst_port == local_port && rx_src_ip == dst_ip && rx_src_port == dst_port) {
            if (rx_payload_len > 0) {
                size_t n = rx_payload_len;
                if (total + n > reply_max) n = reply_max - total;
                memcpy((char*)reply_buf + total, rx_payload, n);
                total += n;
                ack = rx_tcp_seq + (uint32_t)rx_payload_len;
                /* ACK it */
                tcp_hdr tcp;
                memset(&tcp, 0, sizeof(tcp));
                tcp.src_port = htons(local_port);
                tcp.dst_port = htons(dst_port);
                tcp.seq = htonl(seq);
                tcp.ack = htonl(ack);
                tcp.data_off = (5 << 4);
                tcp.flags = TCP_ACK;
                tcp.window = htons(8192);
                tcp.checksum = 0;
                tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                                  &tcp, sizeof(tcp), nullptr, 0);
                send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0);
                idle = 0;
            }
            if (rx_tcp_flags & (TCP_FIN | TCP_RST)) {
                rx_has_tcp = false;
                /* FIN-ACK */
                if (rx_tcp_flags & TCP_FIN) {
                    tcp_hdr tcp;
                    memset(&tcp, 0, sizeof(tcp));
                    tcp.src_port = htons(local_port);
                    tcp.dst_port = htons(dst_port);
                    tcp.seq = htonl(seq);
                    tcp.ack = htonl(ack + 1);
                    tcp.data_off = (5 << 4);
                    tcp.flags = TCP_FIN | TCP_ACK;
                    tcp.window = htons(8192);
                    tcp.checksum = 0;
                    tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                                      &tcp, sizeof(tcp), nullptr, 0);
                    send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0);
                }
                break;
            }
            rx_has_tcp = false;
        } else {
            idle += 10;
            if (total > 0 && idle > 500) break; /* got some data, quiet for 0.5s */
        }
        timer_sleep_ms(10);
        waited += 10;
    }

    return (int)total;
}


/* True port probe: SYN only; OPEN only on SYN+ACK with matching ports + ack */

/* Port probe: OPEN only after valid SYN-ACK from exact 4-tuple;
 * if RST arrives for SYN or right after our ACK -> closed */
int net_tcp_probe(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ms) {
    static uint16_t local_port = 40000;
    local_port++;
    if (local_port < 40000 || local_port > 48000) local_port = 40000;

    uint32_t seq = 0xA5000000u + (uint32_t)local_port + ((uint32_t)dst_port << 16);

    rx_has_tcp = false;
    rx_tcp_flags = 0;
    rx_tcp_dst_port = 0;
    rx_src_port = 0;
    rx_src_ip = 0;
    rx_tcp_seq = 0;
    rx_tcp_ack = 0;
    rx_payload_len = 0;
    for (int i = 0; i < 16; i++) net_poll();
    rx_has_tcp = false;

    tcp_hdr tcp;
    memset(&tcp, 0, sizeof(tcp));
    tcp.src_port = htons(local_port);
    tcp.dst_port = htons(dst_port);
    tcp.seq = htonl(seq);
    tcp.ack = 0;
    tcp.data_off = (5 << 4);
    tcp.flags = TCP_SYN;
    tcp.window = htons(8192);
    tcp.checksum = 0;
    tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                      &tcp, sizeof(tcp), nullptr, 0);

    if (!send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0))
        return -1;

    uint32_t waited = 0;
    int saw_synack = 0;
    uint32_t rseq = 0;

    while (waited < timeout_ms) {
        net_poll();
        if (rx_has_tcp && rx_tcp_dst_port == local_port &&
            rx_src_ip == dst_ip && rx_src_port == dst_port) {
            uint8_t f = rx_tcp_flags;
            uint32_t rack = rx_tcp_ack;
            rseq = rx_tcp_seq;
            rx_has_tcp = false;

            if (f & TCP_RST)
                return 0;

            if (!saw_synack && (f & TCP_SYN) && (f & TCP_ACK) && rack == seq + 1) {
                saw_synack = 1;
                /* complete handshake with ACK, then see if RST follows (closed half-open) */
                memset(&tcp, 0, sizeof(tcp));
                tcp.src_port = htons(local_port);
                tcp.dst_port = htons(dst_port);
                tcp.seq = htonl(seq + 1);
                tcp.ack = htonl(rseq + 1);
                tcp.data_off = (5 << 4);
                tcp.flags = TCP_ACK;
                tcp.window = htons(8192);
                tcp.checksum = 0;
                tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                                  &tcp, sizeof(tcp), nullptr, 0);
                send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0);

                /* short window for RST after ACK */
                for (uint32_t w = 0; w < 300; w += 10) {
                    net_poll();
                    if (rx_has_tcp && rx_tcp_dst_port == local_port &&
                        rx_src_ip == dst_ip && rx_src_port == dst_port) {
                        uint8_t f2 = rx_tcp_flags;
                        rx_has_tcp = false;
                        if (f2 & TCP_RST) {
                            return 0;
                        }
                    }
                    timer_sleep_ms(10);
                }

                /* tear down */
                memset(&tcp, 0, sizeof(tcp));
                tcp.src_port = htons(local_port);
                tcp.dst_port = htons(dst_port);
                tcp.seq = htonl(seq + 1);
                tcp.ack = htonl(rseq + 1);
                tcp.data_off = (5 << 4);
                tcp.flags = TCP_RST | TCP_ACK;
                tcp.window = 0;
                tcp.checksum = 0;
                tcp.checksum = transport_checksum(htonl(OUR_IP), htonl(dst_ip), IP_PROTO_TCP,
                                                  &tcp, sizeof(tcp), nullptr, 0);
                send_ip_packet(dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp), nullptr, 0);
                return 1;
            }
        }
        timer_sleep_ms(10);
        waited += 10;
    }
    return -1;
}


