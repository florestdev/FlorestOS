#include "keyboard.h"
#include "io.h"
#include "vga.h"

/* US QWERTY scancode set 1 */
static const char scancode_map[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0,  '*', 0, ' ',
};

static const char scancode_map_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0,  '*', 0, ' ',
};

static bool shift_down = false;
static bool e0_prefix = false;

void keyboard_flush(void) {
    shift_down = false;
    e0_prefix = false;
    /* drain both keyboard and mouse bytes from 8042 */
    for (int i = 0; i < 256; i++) {
        if (!(inb(0x64) & 1)) break;
        (void)inb(0x60);
    }
}

void keyboard_init(void) {
    keyboard_flush();
}

/*
 * CRITICAL: only consume keyboard data (status bit0=1, bit5=0).
 * Mouse data has bit5 set — never interpret as scancodes (caused 777/garbage).
 */
char keyboard_getchar(void) {
    uint8_t st = inb(0x64);
    if ((st & 1) == 0) return 0;
    if (st & 0x20) return 0; /* mouse waiting — leave for mouse_poll */

    uint8_t sc = inb(0x60);

    if (sc == 0xE0) { e0_prefix = true; return 0; }

    if (sc == 0x2A || sc == 0x36) { shift_down = true;  e0_prefix = false; return 0; }
    if (sc == 0xAA || sc == 0xB6) { shift_down = false; e0_prefix = false; return 0; }

    /* ignore break codes */
    if (sc & 0x80) { e0_prefix = false; return 0; }

    /* ignore extended keys (arrows etc.) for text input */
    if (e0_prefix) { e0_prefix = false; return 0; }

    if (sc < 128) {
        char c = shift_down ? scancode_map_shift[sc] : scancode_map[sc];
        return c;
    }
    return 0;
}

void keyboard_readline(char* buf, size_t max_len) {
    size_t pos = 0;
    buf[0] = 0;
    keyboard_flush();

    while (1) {
        char c = keyboard_getchar();
        if (!c) {
            for (volatile int i = 0; i < 30000; i++)
                asm volatile("pause");
            continue;
        }
        if (c == 27) continue; /* Esc ignore in readline */
        if (c == '\n' || c == '\r') {
            vga_putchar('\n');
            buf[pos] = 0;
            return;
        }
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                buf[pos] = 0;
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && pos + 1 < max_len) {
            buf[pos++] = c;
            buf[pos] = 0;
            vga_putchar(c);
        }
    }
}
