#include "mouse.h"
#include "io.h"
#include "vga.h"

static int mx = 40, my = 12;
static int left_down = 0, left_edge = 0;
static char under_ch = ' ';
static uint8_t under_attr = 0x07;
static int cursor_shown = 0;

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if ((inb(0x64) & 2) == 0) return;
}
static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1) return;
}
static void mouse_write(uint8_t val) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, val);
}
static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(0x60);
}

void mouse_init(void) {
    mouse_wait_write();
    outb(0x64, 0xA8); /* enable aux */
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t status = inb(0x60);
    status |= 2;   /* enable IRQ12 — we poll, but enable clock */
    status &= ~0x20;
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    mouse_write(0xF6); /* defaults */
    mouse_read();
    mouse_write(0xF4); /* enable streaming */
    mouse_read();

    mx = 40; my = 12;
    left_down = 0;
    left_edge = 0;
    cursor_shown = 0;
}


void mouse_disable(void) {
    mouse_write(0xF5); /* disable streaming */
    mouse_read();
    /* drain */
    for (int i = 0; i < 64; i++) {
        if (!(inb(0x64) & 1)) break;
        (void)inb(0x60);
    }
    cursor_shown = 0;
}

void mouse_poll(void) {
    left_edge = 0;
    /* need 3-byte packets; only read if full packet ready */
    static uint8_t cycle = 0;
    static uint8_t p0, p1, p2;

    for (int n = 0; n < 6; n++) {
        if ((inb(0x64) & 0x21) != 0x21) break; /* data from mouse */
        uint8_t b = inb(0x60);
        if (cycle == 0) {
            if (!(b & 0x08)) continue; /* sync bit */
            p0 = b; cycle = 1;
        } else if (cycle == 1) {
            p1 = b; cycle = 2;
        } else {
            p2 = b; cycle = 0;
            int dx = p1;
            int dy = p2;
            if (p0 & 0x10) dx |= ~0xFF;
            if (p0 & 0x20) dy |= ~0xFF;
            /* scale: mouse is finer than text cells */
            static int accx = 0, accy = 0;
            accx += dx;
            accy -= dy; /* Y inverted */
            while (accx >= 4) { mx++; accx -= 4; }
            while (accx <= -4) { mx--; accx += 4; }
            while (accy >= 4) { my++; accy -= 4; }
            while (accy <= -4) { my--; accy += 4; }
            if (mx < 0) mx = 0;
            if (mx > 79) mx = 79;
            if (my < 0) my = 0;
            if (my > 24) my = 24;
            int ld = (p0 & 1) != 0;
            if (ld && !left_down) left_edge = 1;
            left_down = ld;
        }
    }
}

int mouse_x(void) { return mx; }
int mouse_y(void) { return my; }
bool mouse_left_pressed(void) { return left_edge != 0; }
bool mouse_left_down(void) { return left_down != 0; }

void mouse_hide_cursor(void) {
    if (!cursor_shown) return;
    uint16_t* vga = (uint16_t*)VGA_MEMORY;
    vga[my * 80 + mx] = (uint16_t)under_ch | ((uint16_t)under_attr << 8);
    cursor_shown = 0;
}

void mouse_draw_cursor(void) {
    uint16_t* vga = (uint16_t*)VGA_MEMORY;
    if (cursor_shown) {
        vga[my * 80 + mx] = (uint16_t)under_ch | ((uint16_t)under_attr << 8);
    }
    /* re-read under after hide (position may have moved) */
    uint16_t e = vga[my * 80 + mx];
    under_ch = (char)(e & 0xFF);
    under_attr = (uint8_t)(e >> 8);
    vga[my * 80 + mx] = (uint16_t)'X' | ((uint16_t)0x0E << 8); /* yellow X */
    cursor_shown = 1;
}
