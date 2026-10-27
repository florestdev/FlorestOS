/*
 * FlorestOS-1.0 (FlorestDevelopment)
 * Study kernel — DNS, scan, mcstatus, PUSH
 */

#include "vga.h"
#include "netstack.h"
#include "keyboard.h"
#include "timer.h"
#include "string.h"
#include "types.h"
#include "dns.h"
#include "results.h"
#include "fln.h"
#include "gui.h"
#include "ata.h"
#include "io.h"
#include "mouse.h"

static void print_num(int n) {
    if (n == 0) { vga_print("0"); return; }
    if (n < 0) { vga_putchar('-'); n = -n; }
    char t[16]; int i = 0;
    while (n) { t[i++] = '0' + (n % 10); n /= 10; }
    while (i--) vga_putchar(t[i]);
}

static void print_u32(uint32_t n) {
    if (n == 0) { vga_print("0"); return; }
    char t[12]; int i = 0;
    while (n) { t[i++] = '0' + (n % 10); n /= 10; }
    while (i--) vga_putchar(t[i]);
}

static void cute_line(const char* msg) {
    vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
    vga_print("  * ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print(msg);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cute_welcome() {
    vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
    vga_print("\n");
    vga_print("      .  *  .   \\|/   .  *  .\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("   ~~~ soft packet forest ~~~\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("     FlorestOS-1.0\n");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("     (FlorestDevelopment)\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("   ~~~ be kind to packets :3 ~~~\n");
    vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
    vga_print("      .  *  .   /|\\   .  *  .\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}


static void splash_screen(void) {
    /* Long boot screen: logo + Made by FlorestDev + progress bar ~8s */
    vga_clear(VGA_BLACK);
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n\n");
    vga_print("          ################################\n");
    vga_print("          #                              #\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("          #          FlorestOS           #\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("          #                              #\n");
    vga_print("          ################################\n");
    vga_print("\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("                   Loading...\n\n");
    /* progress bar at row 12 */
    vga_print("          [");
    for (int i = 0; i < 40; i++) vga_putchar(' ');
    vga_print("]\n\n\n\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("                Made by FlorestDev\n");

    const uint32_t total_ms = 15000;
    const uint32_t step = 100;
    uint32_t elapsed = 0;
    while (elapsed <= total_ms) {
        int filled = (int)((elapsed * 40) / total_ms);
        if (filled > 40) filled = 40;
        vga_gotoxy(11, 12);
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        for (int i = 0; i < filled; i++) vga_putchar('#');
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        for (int i = filled; i < 40; i++) vga_putchar('-');
        vga_gotoxy(11, 13);
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        int pct = (int)((elapsed * 100) / total_ms);
        if (pct > 100) pct = 100;
        char msg[24];
        /* simple percent print */
        vga_print("          ");
        if (pct >= 100) vga_print("100");
        else if (pct >= 10) {
            vga_putchar((char)('0' + pct / 10));
            vga_putchar((char)('0' + pct % 10));
        } else {
            vga_putchar(' ');
            vga_putchar((char)('0' + pct));
        }
        vga_print("%  ");
        timer_sleep_ms(step);
        elapsed += step;
    }
    timer_sleep_ms(400);
    vga_clear(VGA_BLACK);
}

/* After splash: user picks gui or console */
static int choose_mode(void) {
    vga_clear(VGA_BLACK);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("\n  Boot mode\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Type:  gui      — compact GUI + mouse\n");
    vga_print("         console — classic shell\n\n");
    vga_print("  (default: console if empty)\n\n");
    for (;;) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("  mode> ");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        char line[32];
        keyboard_readline(line, sizeof(line));
        if (!line[0] || strcmp(line, "console") == 0) return 0;
        if (strcmp(line, "gui") == 0) return 1;
        vga_print("  use: gui  or  console\n");
    }
}

static void print_banner() {
    cute_welcome();
    cute_line("NIC e1000  |  DNS  |  TCP scan  |  mcstatus  |  PUSH\n");
    cute_line("IP 10.0.2.15  GW 10.0.2.2  DNS 10.0.2.3\n");
    vga_print("\n");
}

static void print_help() {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  little commands for a little OS :3\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  help / status / clear\n");
    vga_print("  dns <host> [dns_ip]\n");
    vga_print("  udp <ip> <port> <data...>\n");
    vga_print("  tcp <ip> <port> <data...>\n");
    vga_print("  http <host|ip> [port] [path]\n");
    vga_print("  tls <ip|host> [port]          (TCP probe only, no crypto)\n");
    vga_print("  scan <ip|host> <port> [end]   honest SYN/RST scan\n");
    vga_print("  mcstatus <ip|host> [port]     status + UDP query\n");
    vga_print("  push <ip|host> <port> [path]  HTTP POST results (no TLS)\n");
    vga_print("  results | results clear\n");
    vga_print("  edit / run / script / fln <code>  FlorestNET++\n");
    vga_print("  install                   write OS to ATA disk0\n");
    vga_print("  stop                      power off VM\n\n");
}

static int split_args(char* line, char* argv[], int max_args) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

static void unescape(char* s) {
    char* r = s; char* w = s;
    while (*r) {
        if (r[0] == '\\' && r[1] == 'r') { *w++ = '\r'; r += 2; }
        else if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r += 2; }
        else *w++ = *r++;
    }
    *w = 0;
}

static bool resolve_target(const char* host, uint32_t* out_ip) {
    if (parse_ipv4(host, out_ip)) return true;
    return dns_resolve(host, 0, out_ip) == 0;
}

static void log_line(const char* a, const char* b) {
    char buf[192];
    buf[0] = 0;
    strcat(buf, a);
    if (b) strcat(buf, b);
    results_append(buf);
}

/* ---- tiny JSON helpers (string search, no full parser) ---- */
static const char* json_find_key(const char* js, int len, const char* key) {
    int klen = (int)strlen(key);
    for (int i = 0; i + klen + 3 < len; i++) {
        if (js[i] != '"') continue;
        int ok = 1;
        for (int k = 0; k < klen; k++) {
            if (js[i + 1 + k] != key[k]) { ok = 0; break; }
        }
        if (!ok || js[i + 1 + klen] != '"') continue;
        const char* p = js + i + 1 + klen + 1;
        while (p < js + len && (*p == ' ' || *p == '\t' || *p == ':')) p++;
        return p;
    }
    return nullptr;
}

static int json_read_int(const char* p, int* out) {
    if (!p) return -1;
    while (*p == ' ' || *p == '"') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (*p < '0' || *p > '9') return -1;
    int v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    *out = v * sign;
    return 0;
}

static int json_read_string(const char* p, char* out, int max_out) {
    if (!p || max_out < 2) return -1;
    while (*p == ' ') p++;
    if (*p != '"') {
        /* unquoted token */
        int i = 0;
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && i + 1 < max_out)
            out[i++] = *p++;
        out[i] = 0;
        return i > 0 ? 0 : -1;
    }
    p++;
    int i = 0;
    while (*p && *p != '"' && i + 1 < max_out) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[i++] = '\n';
            else if (*p == 't') out[i++] = '\t';
            else if (*p == '"') out[i++] = '"';
            else if (*p == '\\') out[i++] = '\\';
            else if (*p == 'u') {
                /* skip \uXXXX */
                for (int k = 0; k < 4 && p[1]; k++) p++;
                out[i++] = '?';
            } else out[i++] = *p;
            p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = 0;
    return 0;
}

/* description can be string or {text: "..."} */
static void json_read_description(const char* js, int len, char* out, int max_out) {
    out[0] = 0;
    const char* d = json_find_key(js, len, "description");
    if (!d) return;
    while (*d == ' ') d++;
    if (*d == '"') {
        json_read_string(d, out, max_out);
        return;
    }
    if (*d == '{') {
        /* search text inside object — limited window */
        int remain = len - (int)(d - js);
        if (remain > 512) remain = 512;
        const char* t = json_find_key(d, remain, "text");
        if (t) json_read_string(t, out, max_out);
    }
}


static void cmd_udp(int argc, char* argv[]) {
    if (argc < 4) { cute_line("usage: udp <ip> <port> <data...>\n"); return; }
    uint32_t ip, port;
    if (!parse_ipv4(argv[1], &ip) || parse_uint(argv[2], &port) < 0) {
        cute_line("bad ip/port ;-;\n"); return;
    }
    char payload[512]; payload[0] = 0;
    for (int i = 3; i < argc; i++) {
        if (i > 3) strcat(payload, " ");
        strcat(payload, argv[i]);
    }
    unescape(payload);
    char reply[1024];
    int n = net_udp_request(ip, (uint16_t)port, payload, strlen(payload),
                            reply, sizeof(reply) - 1, 3000);
    if (n < 0) {
        vga_print_color("  [udp] no reply ;-;\n", VGA_LIGHT_RED, VGA_BLACK);
        log_line("UDP FAIL ", argv[1]);
    } else {
        reply[n] = 0;
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("  [udp] "); print_num(n); vga_print(" bytes :3\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        for (int i = 0; i < n; i++) {
            char c = reply[i];
            vga_putchar((c >= 32 && c < 127) ? c : '.');
        }
        vga_print("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        log_line("UDP OK ", argv[1]);
    }
}

static void cmd_tcp(int argc, char* argv[]) {
    if (argc < 4) { cute_line("usage: tcp <ip> <port> <data...>\n"); return; }
    uint32_t ip, port;
    if (!parse_ipv4(argv[1], &ip) || parse_uint(argv[2], &port) < 0) {
        cute_line("bad ip/port ;-;\n"); return;
    }
    char payload[512]; payload[0] = 0;
    for (int i = 3; i < argc; i++) {
        if (i > 3) strcat(payload, " ");
        strcat(payload, argv[i]);
    }
    unescape(payload);
    char reply[2048];
    int n = net_tcp_request(ip, (uint16_t)port, payload, strlen(payload),
                            reply, sizeof(reply) - 1, 5000);
    if (n < 0) {
        vga_print_color("  [tcp] failed ;-;\n", VGA_LIGHT_RED, VGA_BLACK);
        log_line("TCP FAIL ", argv[1]);
    } else {
        reply[n] = 0;
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_print("  [tcp] "); print_num(n); vga_print(" bytes :3\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        for (int i = 0; i < n; i++) {
            char c = reply[i];
            vga_putchar((c >= 32 && c < 127) ? c : (c == '\n' || c == '\r' ? c : '.'));
        }
        vga_print("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        log_line("TCP OK ", argv[1]);
    }
}

static void cmd_http(int argc, char* argv[]) {
    if (argc < 2) { cute_line("usage: http <host|ip> [port] [path]\n"); return; }
    uint32_t ip;
    if (!resolve_target(argv[1], &ip)) { cute_line("resolve failed ;-;\n"); return; }
    uint32_t port = 80;
    const char* path = "/";
    if (argc >= 3) {
        if (parse_uint(argv[2], &port) < 0) path = argv[2];
        else if (argc >= 4) path = argv[3];
    }
    char req[256];
    strcpy(req, "GET ");
    strcat(req, path);
    strcat(req, " HTTP/1.0\r\nHost: ");
    strcat(req, argv[1]);
    strcat(req, "\r\nConnection: close\r\n\r\n");
    cute_line("http get ");
    vga_print(path);
    vga_print("\n");
    char reply[2048];
    int n = net_tcp_request(ip, (uint16_t)port, req, strlen(req),
                            reply, sizeof(reply) - 1, 5000);
    if (n < 0) {
        vga_print_color("  [http] failed ;-;\n", VGA_LIGHT_RED, VGA_BLACK);
        log_line("HTTP FAIL ", argv[1]);
    } else {
        reply[n] = 0;
        vga_set_color(VGA_WHITE, VGA_BLACK);
        for (int i = 0; i < n; i++) {
            char c = reply[i];
            vga_putchar((c >= 32 && c < 127) ? c : (c == '\n' || c == '\r' ? c : '.'));
        }
        vga_print("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        log_line("HTTP OK ", argv[1]);
    }
}

static void cmd_dns(int argc, char* argv[]) {
    if (argc < 2) { cute_line("usage: dns <hostname> [dns_ip]\n"); return; }
    uint32_t server = 0;
    if (argc >= 3 && !parse_ipv4(argv[2], &server)) {
        cute_line("bad dns ip\n"); return;
    }
    uint32_t ip = 0;
    if (dns_resolve(argv[1], server, &ip) == 0) {
        char ipb[16]; format_ipv4(ip, ipb);
        char buf[96];
        strcpy(buf, "DNS ");
        strcat(buf, argv[1]);
        strcat(buf, " -> ");
        strcat(buf, ipb);
        results_append(buf);
        cute_line("resolved :3\n");
    }
}

static void cmd_tls(int argc, char* argv[]) {
    if (argc < 2) { cute_line("usage: tls <ip|host> [port]\n"); return; }
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  [tls] no crypto in kernel — TCP probe only :3\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    uint32_t ip;
    if (!resolve_target(argv[1], &ip)) { cute_line("resolve failed\n"); return; }
    uint32_t port = 443;
    if (argc >= 3) parse_uint(argv[2], &port);
    int r = net_tcp_probe(ip, (uint16_t)port, 2000);
    if (r == 1) {
        vga_print_color("  [tls] port OPEN (still not HTTPS decrypt)\n", VGA_LIGHT_GREEN, VGA_BLACK);
        log_line("TLS OPEN ", argv[1]);
    } else if (r == 0) {
        vga_print_color("  [tls] port CLOSED\n", VGA_LIGHT_RED, VGA_BLACK);
        log_line("TLS CLOSED ", argv[1]);
    } else {
        vga_print_color("  [tls] filtered/timeout\n", VGA_YELLOW, VGA_BLACK);
        log_line("TLS FILTER ", argv[1]);
    }
}

static void cmd_scan(int argc, char* argv[]) {
    if (argc < 3) {
        cute_line("usage: scan <ip|host> <port> [end_port]\n");
        return;
    }
    uint32_t ip;
    if (!resolve_target(argv[1], &ip)) { cute_line("resolve failed ;-;\n"); return; }
    uint32_t p0 = 0, p1 = 0;
    if (parse_uint(argv[2], &p0) < 0 || p0 > 65535) { cute_line("bad port\n"); return; }
    p1 = p0;
    if (argc >= 4) {
        if (parse_uint(argv[3], &p1) < 0 || p1 > 65535) { cute_line("bad end\n"); return; }
    }
    if (p1 < p0) { uint32_t t = p0; p0 = p1; p1 = t; }
    if (p1 - p0 > 128) {
        cute_line("max 128 ports (study OS) — truncating :3\n");
        p1 = p0 + 128;
    }

    char ipb[16]; format_ipv4(ip, ipb);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  [scan] ");
    vga_print(ipb);
    vga_print("  ");
    print_u32(p0);
    vga_print("-");
    print_u32(p1);
    vga_print("  (SYN/RST)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int open_n = 0, closed_n = 0, filt_n = 0;
    for (uint32_t p = p0; p <= p1; p++) {
        vga_print("  ");
        print_u32(p);
        vga_print(" ... ");
        int r = net_tcp_probe(ip, (uint16_t)p, 1200);
        if (r == 1) {
            vga_print_color("OPEN :3\n", VGA_LIGHT_GREEN, VGA_BLACK);
            open_n++;
            char line[64];
            strcpy(line, "SCAN OPEN ");
            strcat(line, ipb);
            strcat(line, ":");
            char pt[8];
            uint32_t x = p; int i = 0; char tmp[8];
            if (x == 0) { pt[0] = '0'; pt[1] = 0; }
            else {
                while (x) { tmp[i++] = '0' + (x % 10); x /= 10; }
                int j = 0; while (i--) pt[j++] = tmp[i]; pt[j] = 0;
            }
            strcat(line, pt);
            results_append(line);
        } else if (r == 0) {
            vga_print_color("closed\n", VGA_DARK_GREY, VGA_BLACK);
            closed_n++;
        } else {
            vga_print_color("filtered\n", VGA_YELLOW, VGA_BLACK);
            filt_n++;
        }
    }
    vga_print("  [scan] open=");
    print_num(open_n);
    vga_print(" closed=");
    print_num(closed_n);
    vga_print(" filtered=");
    print_num(filt_n);
    vga_print("\n");
}


/* ---- Minecraft status (mcstatus JavaServer + LegacyClient) ---- */

static int write_varint(uint8_t* out, int32_t val) {
    int n = 0;
    uint32_t v = (uint32_t)val;
    while (1) {
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v) b |= 0x80;
        out[n++] = b;
        if (!v) break;
    }
    return n;
}

static int read_varint_buf(const uint8_t* in, int len, int* consumed) {
    int32_t val = 0;
    int shift = 0;
    int i = 0;
    while (i < len && i < 5) {
        uint8_t b = in[i++];
        val |= (int32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *consumed = i; return val; }
        shift += 7;
    }
    *consumed = -1;
    return -1;
}

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static int mc_frame(uint8_t* out, const uint8_t* payload, int plen) {
    int n = write_varint(out, plen);
    for (int i = 0; i < plen; i++) out[n++] = payload[i];
    return n;
}

/* Decode UTF-16BE into ASCII-ish out (Minecraft legacy MOTD) */
static int utf16be_to_ascii(const uint8_t* data, int nbytes, char* out, int max_out) {
    int oi = 0;
    for (int i = 0; i + 1 < nbytes && oi + 1 < max_out; i += 2) {
        int ch = (data[i] << 8) | data[i + 1];
        if (ch == 0) out[oi++] = '\0';
        else if (ch >= 32 && ch < 127) out[oi++] = (char)ch;
        else if (ch == 0xA7) out[oi++] = 0xA7; /* section sign marker as 0xA7 */
        else out[oi++] = '?';
    }
    out[oi] = 0;
    return oi;
}

/*
 * LegacyClient.read_status (mcstatus):
 *   write fe01fa
 *   read id must be 0xFF
 *   read ushort length (num of UTF-16 chars)
 *   read length*2 bytes UTF-16BE
 *   parse §1 \\0 protocol \\0 version \\0 motd \\0 online \\0 max
 */
static int mc_status_legacy(uint32_t ip, uint16_t port) {
    cute_line("mc legacy (FE 01 FA like LegacyClient)...\n");
    const uint8_t req[3] = { 0xFE, 0x01, 0xFA };
    static uint8_t reply[4096];
    int n = net_tcp_request(ip, port, req, 3, reply, sizeof(reply), 6000);
    if (n < 4) {
        vga_print_color("  [mc/legacy] no data\n", VGA_YELLOW, VGA_BLACK);
        return -1;
    }

    if (reply[0] != 0xFF) {
        vga_print_color("  [mc/legacy] not FF kick packet\n", VGA_YELLOW, VGA_BLACK);
        return -1;
    }
    int char_count = (reply[1] << 8) | reply[2];
    int byte_count = char_count * 2;
    if (byte_count <= 0 || 3 + byte_count > n) {
        /* take what we have */
        byte_count = n - 3;
        if (byte_count < 2) return -1;
    }

        char decoded[2048];
    int dlen = utf16be_to_ascii(reply + 3, byte_count, decoded, sizeof(decoded));

    /* split by embedded NUL (UTF-16 null code units) */
    char* parts[8];
    int nparts = 0;
    parts[nparts++] = decoded;
    for (int i = 0; i < dlen && nparts < 8; i++) {
        if (decoded[i] == 0 && i + 1 < dlen) {
            parts[nparts++] = &decoded[i + 1];
        }
    }
    /* also split on 0xA7 sequences for very old format — handled below */

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("  [mc/legacy] ok :3\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    if (nparts >= 6 && parts[0][0] == (char)0xA7 && parts[0][1] == '1') {
        vga_print("    Protocol: "); vga_print(parts[1]); vga_print("\n");
        vga_print("    Version:  "); vga_print(parts[2]); vga_print("\n");
        vga_print("    MOTD:     ");
        vga_set_color(VGA_WHITE, VGA_BLACK); vga_print(parts[3]);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); vga_print("\n");
        vga_print("    Players:  "); vga_print(parts[4]); vga_print(" / "); vga_print(parts[5]); vga_print("\n");
        results_append("MC LEGACY OK");
        return 0;
    }

    /* dump decoded printable */
    vga_print("    Raw: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    for (int i = 0; decoded[i] && i < 200; i++) {
        char c = decoded[i];
        if (c == '\0') vga_print(" | ");
        else if (c >= 32 && c < 127) vga_putchar(c);
        else vga_putchar('?');
    }
    vga_print("\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    results_append("MC LEGACY RAW");
    return 0;
}

/*
 * JavaClient.read_status / JavaServer.status (version default 47)
 */
static int mc_status_modern(uint32_t ip, uint16_t port, const char* host) {
    cute_line("mc modern (JavaClient status, protocol 47)...\n");
    static uint8_t reply[12288];

    uint8_t body[320];
    int b = 0;
    b += write_varint(body + b, 0);
    b += write_varint(body + b, 47);
    int alen = (int)strlen(host);
    if (alen > 200) alen = 200;
    b += write_varint(body + b, alen);
    for (int i = 0; i < alen; i++) body[b++] = (uint8_t)host[i];
    body[b++] = (uint8_t)((port >> 8) & 0xFF);
    body[b++] = (uint8_t)(port & 0xFF);
    b += write_varint(body + b, 1);

    uint8_t packet[400];
    int pos = 0;
    pos += mc_frame(packet + pos, body, b);
    uint8_t reqb[4];
    int rb = write_varint(reqb, 0);
    pos += mc_frame(packet + pos, reqb, rb);

    int n = net_tcp_request(ip, port, packet, (size_t)pos, reply, sizeof(reply) - 1, 10000);
    if (n < 5) {
        vga_print_color("  [mc/modern] no data\n", VGA_YELLOW, VGA_BLACK);
        return -1;
    }

    /* Find JSON: search '{' after optional framing */
    int js_off = -1;
    for (int i = 0; i < n; i++) {
        if (reply[i] == '{') { js_off = i; break; }
    }

    if (js_off < 0) {
        /* try parse frame then look again */
        int cons = 0;
        int plen = read_varint_buf(reply, n, &cons);
        (void)plen;
        if (cons > 0) {
            for (int i = cons; i < n; i++) {
                if (reply[i] == '{') { js_off = i; break; }
            }
        }
    }

    if (js_off < 0) {
        vga_print_color("  [mc/modern] no JSON in response\n", VGA_YELLOW, VGA_BLACK);
        vga_print("  hex:");
        for (int i = 0; i < n && i < 20; i++) {
            char hx[4] = { ' ', "0123456789ABCDEF"[reply[i] >> 4], "0123456789ABCDEF"[reply[i] & 0xF], 0 };
            vga_print(hx);
        }
        vga_print("\n  ascii:");
        for (int i = 0; i < n && i < 40; i++) {
            char c = (char)reply[i];
            vga_putchar((c >= 32 && c < 127) ? c : '.');
        }
        vga_print("\n");
        return -1;
    }

    const char* js = (const char*)reply + js_off;
    int slen = n - js_off;
    /* trim to matching brace if possible */
    int depth = 0, end = -1;
    for (int i = 0; i < slen; i++) {
        if (js[i] == '{') depth++;
        else if (js[i] == '}') {
            depth--;
            if (depth == 0) { end = i + 1; break; }
        }
    }
    if (end > 0) slen = end;

    char desc[256]; char ver[64];
    desc[0] = ver[0] = 0;
    json_read_description(js, slen, desc, sizeof(desc));
    const char* verobj = json_find_key(js, slen, "version");
    if (verobj) {
        int rem = slen - (int)(verobj - js);
        if (rem > 300) rem = 300;
        const char* nm = json_find_key(verobj, rem, "name");
        if (nm) json_read_string(nm, ver, sizeof(ver));
    }
    int online = -1, maxp = -1, proto = -1;
    const char* pl = json_find_key(js, slen, "players");
    if (pl) {
        int rem = slen - (int)(pl - js);
        if (rem > 500) rem = 500;
        const char* o = json_find_key(pl, rem, "online");
        const char* m = json_find_key(pl, rem, "max");
        if (o) json_read_int(o, &online);
        if (m) json_read_int(m, &maxp);
    }
    if (verobj) {
        int rem = slen - (int)(verobj - js);
        if (rem > 200) rem = 200;
        const char* pr = json_find_key(verobj, rem, "protocol");
        if (pr) json_read_int(pr, &proto);
    }
    int has_fav = json_find_key(js, slen, "favicon") ? 1 : 0;

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("  [mc/modern] ok :3\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("    MOTD: ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print(desc[0] ? desc : "(none)");
    vga_print("\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("    Version: ");
    vga_print(ver[0] ? ver : "?");
    if (proto >= 0) { vga_print(" protocol="); print_num(proto); }
    vga_print("\n    Players: ");
    if (online >= 0) print_num(online); else vga_print("?");
    vga_print(" / ");
    if (maxp >= 0) print_num(maxp); else vga_print("?");
    vga_print("\n    Favicon: ");
    vga_print(has_fav ? "yes" : "no");
    vga_print("\n");
    results_append("MC MODERN OK");
    return 0;
}

static void mc_query_udp(uint32_t ip, uint16_t port) {
    cute_line("mc query UDP (QueryClient)...\n");
    uint32_t session = 0x01010101u & 0x0F0F0F0Fu;
    uint8_t req[32];
    req[0] = 0xFE; req[1] = 0xFD; req[2] = 0x09;
    write_be32(req + 3, session);

    uint8_t reply[1500];
    int n = net_udp_request(ip, port, req, 7, reply, sizeof(reply), 2500);
    if (n < 6 || reply[0] != 0x09) {
        vga_print_color("  [mc/query] no challenge (enable-query?)\n", VGA_YELLOW, VGA_BLACK);
        return;
    }
    int off = 5;
    char tok[32];
    int ti = 0;
    while (off < n && reply[off] && ti + 1 < (int)sizeof(tok))
        tok[ti++] = (char)reply[off++];
    tok[ti] = 0;
    int challenge = 0;
    for (int i = 0; tok[i]; i++) {
        if (tok[i] < '0' || tok[i] > '9') break;
        challenge = challenge * 10 + (tok[i] - '0');
    }
    req[0] = 0xFE; req[1] = 0xFD; req[2] = 0x00;
    write_be32(req + 3, session);
    write_be32(req + 7, (uint32_t)challenge);
    req[11] = req[12] = req[13] = req[14] = 0;

    n = net_udp_request(ip, port, req, 15, reply, sizeof(reply), 3000);
    if (n < 16 || reply[0] != 0x00) {
        vga_print_color("  [mc/query] full stat fail\n", VGA_YELLOW, VGA_BLACK);
        return;
    }
    off = 5;
    if (off < n && reply[off] == 's') {
        while (off < n && !(reply[off] == 0 && off + 1 < n && (uint8_t)reply[off + 1] == 0x80))
            off++;
        if (off + 3 < n) off += 3;
    }
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("  [mc/query] ok :3\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    while (off < n) {
        char key[64]; int ki = 0;
        while (off < n && reply[off] && ki + 1 < (int)sizeof(key))
            key[ki++] = (char)reply[off++];
        key[ki] = 0;
        if (off < n && reply[off] == 0) off++;
        if (ki == 0) {
            if (off < n && reply[off] == 0) off++;
            vga_print("    players:\n");
            while (off < n) {
                char pn[32]; int pi = 0;
                while (off < n && reply[off] && pi + 1 < (int)sizeof(pn))
                    pn[pi++] = (char)reply[off++];
                pn[pi] = 0;
                if (off < n && reply[off] == 0) off++;
                if (pi == 0) break;
                vga_print("      - ");
                vga_set_color(VGA_WHITE, VGA_BLACK);
                vga_print(pn);
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                vga_print("\n");
            }
            break;
        }
        char val[128]; int vi = 0;
        while (off < n && reply[off] && vi + 1 < (int)sizeof(val))
            val[vi++] = (char)reply[off++];
        val[vi] = 0;
        if (off < n && reply[off] == 0) off++;
        vga_print("    "); vga_print(key); vga_print("=");
        vga_set_color(VGA_WHITE, VGA_BLACK); vga_print(val);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK); vga_print("\n");
    }
    results_append("MC QUERY OK");
}

static void cmd_mcstatus(int argc, char* argv[]) {
    if (argc < 2) {
        cute_line("usage: mcstatus <ip|host> [port]\n");
        return;
    }
    uint32_t ip;
    if (!resolve_target(argv[1], &ip)) { cute_line("resolve failed\n"); return; }
    uint32_t port = 25565;
    if (argc >= 3) parse_uint(argv[2], &port);

    vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
    vga_print("  * mcstatus ");
    vga_print(argv[1]);
    vga_print(":");
    print_u32(port);
    vga_print(" *\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Order like a practical client: modern JSON first, then legacy, then query */
    int ok = mc_status_modern(ip, (uint16_t)port, argv[1]);
    if (ok != 0)
        ok = mc_status_legacy(ip, (uint16_t)port);
    mc_query_udp(ip, (uint16_t)port);
    if (ok != 0)
        cute_line("mcstatus: no usable status (server offline or blocked?)\n");
}

static void cmd_push(int argc, char* argv[]) {
    if (argc < 3) {
        cute_line("usage: push <ip|host> <port> [path]\n");
        cute_line("sends application/json: {os,org,operations:[...]}\n");
        return;
    }
    uint32_t ip;
    if (!resolve_target(argv[1], &ip)) { cute_line("resolve failed\n"); return; }
    uint32_t port = 80;
    if (parse_uint(argv[2], &port) < 0) { cute_line("bad port\n"); return; }
    const char* path = "/";
    if (argc >= 4) path = argv[3];

    /* Build JSON body from results lines */
    const char* raw = results_get();
    char json[RESULT_LOG_SIZE + 256];
    strcpy(json, "{\"os\":\"FlorestOS-1.0\",\"org\":\"FlorestDevelopment\",\"operations\":[");

    /* split raw by newlines into JSON strings */
    int first = 1;
    const char* line = raw;
    while (*line) {
        const char* nl = line;
        while (*nl && *nl != '\n') nl++;
        int len = (int)(nl - line);
        if (len > 0) {
            if (!first) strcat(json, ",");
            first = 0;
            strcat(json, "\"");
            /* escape " and \\ in line */
            char esc[256];
            int ei = 0;
            for (int i = 0; i < len && ei + 2 < (int)sizeof(esc); i++) {
                char c = line[i];
                if (c == '"' || c == '\\') { esc[ei++] = '\\'; esc[ei++] = c; }
                else if (c >= 32 && c < 127) esc[ei++] = c;
                else esc[ei++] = '?';
            }
            esc[ei] = 0;
            strcat(json, esc);
            strcat(json, "\"");
        }
        if (*nl == '\n') line = nl + 1;
        else break;
    }
    if (first) {
        strcat(json, "\"(empty)\"");
    }
    strcat(json, "]}");

    size_t blen = strlen(json);
    char lenbuf[12];
    {
        int x = (int)blen, i = 0; char tmp[12];
        if (x == 0) { lenbuf[0] = '0'; lenbuf[1] = 0; }
        else {
            while (x) { tmp[i++] = '0' + (x % 10); x /= 10; }
            int j = 0; while (i--) lenbuf[j++] = tmp[i]; lenbuf[j] = 0;
        }
    }

    char packet[RESULT_LOG_SIZE + 600];
    strcpy(packet, "POST ");
    strcat(packet, path);
    strcat(packet, " HTTP/1.0\r\nHost: ");
    strcat(packet, argv[1]);
    strcat(packet, "\r\nUser-Agent: FlorestOS-1.0/FlorestDevelopment\r\n");
    strcat(packet, "Content-Type: application/json\r\nContent-Length: ");
    strcat(packet, lenbuf);
    strcat(packet, "\r\nConnection: close\r\n\r\n");
    strcat(packet, json);

    cute_line("push JSON POST (HTTP only) :3\n");
    vga_print("  body: ");
    /* show truncated json */
    for (size_t i = 0; i < blen && i < 80; i++) vga_putchar(json[i]);
    if (blen > 80) vga_print("...");
    vga_print("\n");

    char reply[1024];
    int n = net_tcp_request(ip, (uint16_t)port, packet, strlen(packet),
                            reply, sizeof(reply) - 1, 5000);
    if (n < 0) {
        vga_print_color("  [push] failed ;-;\n", VGA_LIGHT_RED, VGA_BLACK);
    } else {
        reply[n] = 0;
        vga_print_color("  [push] response:\n", VGA_LIGHT_CYAN, VGA_BLACK);
        vga_set_color(VGA_WHITE, VGA_BLACK);
        for (int i = 0; i < n && i < 400; i++) {
            char c = reply[i];
            vga_putchar((c >= 32 && c < 127) ? c : (c == '\n' || c == '\r' ? c : '.'));
        }
        vga_print("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

static void cmd_results(int argc, char* argv[]) {
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        results_clear();
        cute_line("results cleared :3\n");
        return;
    }
    const char* r = results_get();
    if (!r[0]) { cute_line("(results empty)\n"); return; }
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  --- results log ---\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_print(r);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  -------------------\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}


/* Install running image to ATA primary master (LBA 0 = MBR bootloader, LBA1+ = kernel) */
extern "C" {
    extern unsigned char bootloader_embed[];
    extern unsigned int bootloader_embed_len;
}

static void cmd_install(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_print("  Write FlorestOS to ALL IDE disks. Type YES:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    char conf[16];
    keyboard_readline(conf, sizeof(conf));
    if (strcmp(conf, "YES") != 0) { vga_print("  aborted\n"); return; }

    int n = ata_scan();
    if (n <= 0) {
        vga_print_color("  no IDE disk — use PIIX4, VDI on IDE, no SATA\n", VGA_LIGHT_RED, VGA_BLACK);
        return;
    }
    vga_print("  disks: ");
    vga_putchar((char)('0' + n));
    vga_print("\n");

    uint8_t sec[512];
    for (int i = 0; i < 512; i++) sec[i] = 0;
    unsigned int bl = bootloader_embed_len;
    if (bl > 512) bl = 512;
    for (unsigned int i = 0; i < bl; i++) sec[i] = bootloader_embed[i];
    sec[510] = 0x55;
    sec[511] = 0xAA;

    const uint8_t* ker = (const uint8_t*)0x10000;
    int ok = 0;
    for (int di = 0; di < n; di++) {
        if (!ata_select_index(di)) continue;
        vga_print("  disk#"); vga_putchar((char)('0'+di)); vga_print(" MBR...");
        if (!ata_write_sectors(0, sec, 1)) { vga_print(" FAIL\n"); continue; }
        uint8_t chk[512];
        if (!ata_read_sectors(0, chk, 1) || chk[510] != 0x55 || chk[511] != 0xAA) {
            vga_print(" VERIFY FAIL\n"); continue;
        }
        vga_print(" OK, kernel...");
        if (!ata_write_sectors(1, ker, 256)) { vga_print(" FAIL\n"); continue; }
        vga_print(" OK\n");
        ok++;
    }
    if (!ok) { vga_print_color("  FAILED\n", VGA_LIGHT_RED, VGA_BLACK); return; }
    vga_print_color("  install OK — power off, REMOVE ISO, boot HDD only\n", VGA_LIGHT_GREEN, VGA_BLACK);
    results_append("INSTALL OK");
}

static void cmd_stop(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_clear(VGA_BLACK);
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n\n\n\n");
    vga_print("          ################################\n");
    vga_print("          #                              #\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("          #       FLORESTOS 1.0         #\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("          #                              #\n");
    vga_print("          ################################\n\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_print("                Made by FlorestDev\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("                 Shutting down...\n");
    timer_sleep_ms(12000);
    /* VirtualBox / QEMU / Bochs power off */
    outw(0x4004, 0x3400);
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) {
        asm volatile("cli; hlt");
    }
}

static void shell() {
    char line[256];
    char* argv[16];
    print_help();
    while (1) {
        vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
        vga_print("florest");
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_print("> ");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        keyboard_readline(line, sizeof(line));
        if (!line[0]) continue;
        int argc = split_args(line, argv, 16);
        if (argc == 0) continue;
        if (strcmp(argv[0], "help") == 0) print_help();
        else if (strcmp(argv[0], "status") == 0) net_print_status();
        else if (strcmp(argv[0], "udp") == 0) cmd_udp(argc, argv);
        else if (strcmp(argv[0], "tcp") == 0) cmd_tcp(argc, argv);
        else if (strcmp(argv[0], "http") == 0) cmd_http(argc, argv);
        else if (strcmp(argv[0], "dns") == 0) cmd_dns(argc, argv);
        else if (strcmp(argv[0], "tls") == 0) cmd_tls(argc, argv);
        else if (strcmp(argv[0], "scan") == 0) cmd_scan(argc, argv);
        else if (strcmp(argv[0], "mcstatus") == 0) cmd_mcstatus(argc, argv);
        else if (strcmp(argv[0], "push") == 0) cmd_push(argc, argv);
        else if (strcmp(argv[0], "results") == 0) cmd_results(argc, argv);
        else if (strcmp(argv[0], "clear") == 0) vga_clear(VGA_BLACK);
        else if (strcmp(argv[0], "install") == 0) cmd_install(argc, argv);
        else if (strcmp(argv[0], "stop") == 0 || strcmp(argv[0], "shutdown") == 0) cmd_stop(argc, argv);
        else if (strcmp(argv[0], "edit") == 0) fln_editor();
        else if (strcmp(argv[0], "run") == 0) {
            vga_print("  [FlorestNET++] run script\n");
            fln_eval(fln_script_buf());
        } else if (strcmp(argv[0], "script") == 0) {
            vga_print("--- FlorestNET++ script ---\n");
            const char* sc = fln_script_buf();
            vga_print(sc[0] ? sc : "(empty)\n");
            vga_print("---------------------------\n");
        } else if (strcmp(argv[0], "fln") == 0) {
            if (argc < 2) cute_line("usage: fln <code...>\n");
            else {
                char code[240]; code[0] = 0;
                for (int i = 1; i < argc; i++) {
                    if (i > 1) strcat(code, " ");
                    strcat(code, argv[i]);
                }
                fln_eval(code);
            }
        }
        else cute_line("unknown — try help :3\n");
    }
}


/* FlorestNET++ OS builtins — argv[0] is function name */
static int fln_b_dns(int argc, char** argv) {
    cmd_dns(argc, argv);
    return 0;
}
static int fln_b_mcstatus(int argc, char** argv) {
    cmd_mcstatus(argc, argv);
    return 0;
}
static int fln_b_push(int argc, char** argv) {
    cmd_push(argc, argv);
    return 0;
}
static int fln_b_results(int argc, char** argv) {
    if (argc >= 2 && (strcmp(argv[1], "clear") == 0 || strcmp(argv[1], "Clear") == 0)) {
        char* a2[] = { argv[0], (char*)"clear" };
        cmd_results(2, a2);
    } else {
        cmd_results(1, argv);
    }
    return 0;
}
static int fln_b_resultsClear(int argc, char** argv) {
    (void)argc; (void)argv;
    char* a2[] = { (char*)"results", (char*)"clear" };
    cmd_results(2, a2);
    return 0;
}
static int fln_b_scan(int argc, char** argv) {
    cmd_scan(argc, argv);
    return 0;
}
static int fln_b_http(int argc, char** argv) {
    cmd_http(argc, argv);
    return 0;
}
static int fln_b_status(int argc, char** argv) {
    (void)argc; (void)argv;
    net_print_status();
    return 0;
}
static int fln_b_udp(int argc, char** argv) {
    cmd_udp(argc, argv);
    return 0;
}
static int fln_b_tcp(int argc, char** argv) {
    cmd_tcp(argc, argv);
    return 0;
}
static int fln_b_tls(int argc, char** argv) {
    cmd_tls(argc, argv);
    return 0;
}
static int fln_b_help(int argc, char** argv) {
    (void)argc; (void)argv;
    print_help();
    return 0;
}

static void fln_bind_os(void) {
    fln_register("dns", fln_b_dns);
    fln_register("mcstatus", fln_b_mcstatus);
    fln_register("push", fln_b_push);
    fln_register("results", fln_b_results);
    fln_register("resultsClear", fln_b_resultsClear);
    fln_register("scan", fln_b_scan);
    fln_register("http", fln_b_http);
    fln_register("status", fln_b_status);
    fln_register("udp", fln_b_udp);
    fln_register("tcp", fln_b_tcp);
    fln_register("tls", fln_b_tls);
    fln_register("help", fln_b_help);
}

extern "C" void kmain() {
    timer_init();
    keyboard_init();
    splash_screen();
    int mode = choose_mode();
    keyboard_flush();
    if (mode == 1) {
        gui_run();
        keyboard_flush();
        for (volatile int i = 0; i < 200000; i++) {}
        keyboard_flush();
    }
    print_banner();
    results_clear();
    results_append("FlorestOS-1.0 (FlorestDevelopment) session start");
    net_init();
    fln_init();
    fln_bind_os();
    vga_print("\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("  FlorestNET++ ready — try: edit   or   fln println(2+2)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("\n");
    shell();
}

