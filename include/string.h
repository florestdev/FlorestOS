#pragma once
#include "types.h"

static inline size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline void* memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

static inline void* memset(void* dst, int c, size_t n) {
    char* d = (char*)dst;
    while (n--) *d++ = (char)c;
    return dst;
}

static inline int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static inline char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

static inline char* strcat(char* dst, const char* src) {
    char* d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

/* Parse IPv4 "a.b.c.d" -> host-order uint32, return false on error */
bool parse_ipv4(const char* str, uint32_t* out);

/* Format host-order IP to string */
void format_ipv4(uint32_t ip, char* buf);

/* Parse decimal number */
int parse_uint(const char* str, uint32_t* out);
