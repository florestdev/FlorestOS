#include "dns.h"
#include "netstack.h"
#include "string.h"
#include "vga.h"

/* Minimal DNS A-record client (UDP/53) */

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static uint16_t htons16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static uint16_t ntohs16(uint16_t x) { return htons16(x); }
static uint32_t ntohl32(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x & 0xFF0000) >> 8) | ((x >> 24) & 0xFF);
}

/* Encode "www.example.com" -> 3www7example3com0 */
static int encode_name(const char* host, uint8_t* out, int max_out) {
    int pos = 0;
    const char* p = host;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int lab = (int)(dot - p);
        if (lab <= 0 || lab > 63 || pos + 1 + lab + 1 >= max_out) return -1;
        out[pos++] = (uint8_t)lab;
        for (int i = 0; i < lab; i++) out[pos++] = (uint8_t)p[i];
        if (*dot == '.') p = dot + 1;
        else { p = dot; break; }
    }
    out[pos++] = 0;
    return pos;
}

static int skip_name(const uint8_t* msg, int len, int off) {
    int jumps = 0;
    while (off < len) {
        uint8_t l = msg[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) { /* pointer */
            if (off + 1 >= len) return -1;
            return off + 2;
        }
        off += 1 + l;
        if (++jumps > 20) return -1;
    }
    return -1;
}

int dns_resolve(const char* hostname, uint32_t dns_server, uint32_t* out_ip) {
    if (!hostname || !out_ip || !hostname[0]) return -1;
    if (dns_server == 0) dns_server = 0x0A000203; /* 10.0.2.3 QEMU */

    uint8_t query[512];
    dns_header* hdr = (dns_header*)query;
    hdr->id = htons16(0x1337);
    hdr->flags = htons16(0x0100); /* RD */
    hdr->qdcount = htons16(1);
    hdr->ancount = 0;
    hdr->nscount = 0;
    hdr->arcount = 0;

    int nlen = encode_name(hostname, query + sizeof(dns_header), 256);
    if (nlen < 0) return -1;
    int qend = (int)sizeof(dns_header) + nlen;
    query[qend++] = 0x00; query[qend++] = 0x01; /* TYPE A */
    query[qend++] = 0x00; query[qend++] = 0x01; /* CLASS IN */

    vga_print("[DNS] query ");
    vga_print(hostname);
    vga_print(" via ");
    char ipbuf[16];
    format_ipv4(dns_server, ipbuf);
    vga_print(ipbuf);
    vga_print("\n");

    uint8_t reply[512];
    int n = net_udp_request(dns_server, 53, query, (size_t)qend,
                            reply, sizeof(reply), 3000);
    if (n < (int)sizeof(dns_header)) {
        vga_print_color("[DNS] no response\n", 0x0C, 0x00);
        return -1;
    }

    dns_header* rh = (dns_header*)reply;
    uint16_t flags = ntohs16(rh->flags);
    if ((flags & 0x000F) != 0) {
        vga_print_color("[DNS] server error\n", 0x0C, 0x00);
        return -1;
    }
    uint16_t ancount = ntohs16(rh->ancount);
    if (ancount == 0) {
        vga_print_color("[DNS] no A records\n", 0x0C, 0x00);
        return -1;
    }

    int off = sizeof(dns_header);
    /* skip question */
    off = skip_name(reply, n, off);
    if (off < 0 || off + 4 > n) return -1;
    off += 4;

    for (uint16_t i = 0; i < ancount && off < n; i++) {
        off = skip_name(reply, n, off);
        if (off < 0 || off + 10 > n) return -1;
        uint16_t atype = ((uint16_t)reply[off] << 8) | reply[off + 1];
        uint16_t rdlen = ((uint16_t)reply[off + 8] << 8) | reply[off + 9];
        off += 10;
        if (off + rdlen > n) return -1;
        if (atype == 1 && rdlen == 4) {
            uint32_t ip = ((uint32_t)reply[off] << 24) | ((uint32_t)reply[off + 1] << 16) |
                          ((uint32_t)reply[off + 2] << 8) | reply[off + 3];
            *out_ip = ip;
            format_ipv4(ip, ipbuf);
            vga_print_color("[DNS] A ", 0x0A, 0x00);
            vga_print(ipbuf);
            vga_print("\n");
            return 0;
        }
        off += rdlen;
    }
    vga_print_color("[DNS] no A in answers\n", 0x0C, 0x00);
    return -1;
}
