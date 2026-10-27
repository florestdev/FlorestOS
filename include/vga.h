#pragma once
#include "types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

enum vga_color {
    VGA_BLACK = 0,
    VGA_BLUE,
    VGA_GREEN,
    VGA_CYAN,
    VGA_RED,
    VGA_MAGENTA,
    VGA_BROWN,
    VGA_LIGHT_GREY,
    VGA_DARK_GREY,
    VGA_LIGHT_BLUE,
    VGA_LIGHT_GREEN,
    VGA_LIGHT_CYAN,
    VGA_LIGHT_RED,
    VGA_LIGHT_MAGENTA,
    VGA_YELLOW,
    VGA_WHITE
};

void vga_clear(uint8_t bg);
void vga_putchar(char c);
void vga_print(const char* str);
void vga_print_color(const char* str, uint8_t fg, uint8_t bg);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_gotoxy(int x, int y);
