#include "vga.h"

static uint16_t* const vga_buffer = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x0F; // white on black

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = (bg << 4) | (fg & 0x0F);
}

void vga_clear(uint8_t bg) {
    uint8_t color = (bg << 4) | VGA_LIGHT_GREY;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', color);
    }
    cursor_x = 0;
    cursor_y = 0;
}

void vga_gotoxy(int x, int y) {
    if (x >= 0 && x < VGA_WIDTH) cursor_x = x;
    if (y >= 0 && y < VGA_HEIGHT) cursor_y = y;
}

static void scroll() {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(y - 1) * VGA_WIDTH + x] =
                vga_buffer[y * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
            vga_entry(' ', current_color);
    }
    cursor_y = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) scroll();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~3;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= VGA_HEIGHT) scroll();
        }
        return;
    }

    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, current_color);
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) scroll();
    }
}

void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_print_color(const char* str, uint8_t fg, uint8_t bg) {
    uint8_t old = current_color;
    vga_set_color(fg, bg);
    vga_print(str);
    current_color = old;
}
