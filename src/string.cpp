#include "string.h"

bool parse_ipv4(const char* str, uint32_t* out) {
    uint32_t parts[4] = {0, 0, 0, 0};
    int part = 0;
    uint32_t cur = 0;
    bool digit = false;

    for (const char* p = str; ; p++) {
        if (*p >= '0' && *p <= '9') {
            cur = cur * 10 + (*p - '0');
            if (cur > 255) return false;
            digit = true;
        } else if (*p == '.' || *p == 0) {
            if (!digit || part >= 4) return false;
            parts[part++] = cur;
            cur = 0;
            digit = false;
            if (*p == 0) break;
        } else {
            return false;
        }
    }
    if (part != 4) return false;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

static void emit_u8(char** pp, uint8_t v) {
    char* p = *pp;
    char tmp[4];
    int i = 0;
    if (v == 0) {
        *p++ = '0';
        *pp = p;
        return;
    }
    while (v) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i--) *p++ = tmp[i];
    *pp = p;
}

void format_ipv4(uint32_t ip, char* buf) {
    char* p = buf;
    emit_u8(&p, (ip >> 24) & 0xFF); *p++ = '.';
    emit_u8(&p, (ip >> 16) & 0xFF); *p++ = '.';
    emit_u8(&p, (ip >> 8) & 0xFF);  *p++ = '.';
    emit_u8(&p, ip & 0xFF);
    *p = 0;
}

int parse_uint(const char* str, uint32_t* out) {
    uint32_t v = 0;
    if (!*str) return -1;
    while (*str) {
        if (*str < '0' || *str > '9') return -1;
        v = v * 10 + (*str - '0');
        str++;
    }
    *out = v;
    return 0;
}
