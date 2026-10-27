/*
 * FlorestOS GUI — stable redraw, real editor, no PS/2 pollution
 */
#include "gui.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "string.h"
#include "timer.h"
#include "io.h"
#include "fln.h"

struct Cmd {
    const char* name;
    const char* args;
    const char* doc;
};

static const Cmd CMDS[] = {
    { "dns",      "dns(\"host\")",              "Resolve A-record via DNS UDP/53." },
    { "mcstatus", "mcstatus(\"host\", port)",   "Minecraft status TCP + UDP query." },
    { "scan",     "scan(\"ip\", from, to)",     "TCP SYN/RST port scan." },
    { "http",     "http(\"host\", port, path)", "HTTP/1.0 GET real body." },
    { "push",     "push(\"ip\", port, path)",   "POST results JSON to server." },
    { "results",  "results() / resultsClear()", "Show or clear results log." },
    { "status",   "status()",                   "NIC / network stack status." },
    { "udp",      "udp(\"ip\", port, data)",    "UDP send + wait reply." },
    { "tcp",      "tcp(\"ip\", port, data)",    "Minimal TCP client." },
    { "tls",      "tls(\"ip\", port)",          "TCP probe only, no crypto." },
    { "help",     "help()",                     "Shell command help." },
    { "println",  "println(expr)",              "Print in FlorestNET++." },
    { "val",      "val x = 1 + 2",              "Declare number/string var." },
};
static const int CMD_N = (int)(sizeof(CMDS) / sizeof(CMDS[0]));

static void cell(int x, int y, char c, uint8_t fg, uint8_t bg) {
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;
    uint16_t* v = (uint16_t*)VGA_MEMORY;
    v[y * 80 + x] = (uint16_t)(uint8_t)c | ((uint16_t)((bg << 4) | (fg & 0x0F)) << 8);
}
static void fill(int x, int y, int w, int h, char c, uint8_t fg, uint8_t bg) {
    for (int row = y; row < y + h && row < 25; row++)
        for (int col = x; col < x + w && col < 80; col++)
            cell(col, row, c, fg, bg);
}
static void text(int x, int y, const char* s, uint8_t fg, uint8_t bg) {
    for (int i = 0; s[i] && x + i < 80; i++)
        cell(x + i, y, s[i], fg, bg);
}
static void text_clip(int x, int y, const char* s, int maxw, uint8_t fg, uint8_t bg) {
    for (int i = 0; s[i] && i < maxw && x + i < 80; i++)
        cell(x + i, y, s[i], fg, bg);
}
static int hit(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void draw_editor_body(int editing) {
    fill(22, 1, 36, 18, ' ', VGA_LIGHT_GREY, VGA_BLACK);
    if (editing)
        text(23, 1, "[ EDITING — Esc=menu ]", VGA_YELLOW, VGA_BLACK);
    else
        text(23, 1, "[ Editor — click or E ]", VGA_LIGHT_CYAN, VGA_BLACK);

    const char* sc = fln_script_buf();
    int row = 2;
    const char* p = sc;
    int last_col = 24, last_row = 2;
    while (*p && row < 18) {
        int col = 23;
        cell(col++, row, editing ? '|' : '>', VGA_DARK_GREY, VGA_BLACK);
        while (*p && *p != '\n' && col < 57) {
            char ch = *p++;
            uint8_t fg = VGA_WHITE;
            if (ch == '"' || ch == '\'') fg = VGA_LIGHT_GREEN;
            else if (ch >= '0' && ch <= '9') fg = VGA_YELLOW;
            else if (ch == '/') fg = VGA_DARK_GREY;
            cell(col++, row, ch, fg, VGA_BLACK);
        }
        last_col = col;
        last_row = row;
        if (*p == '\n') { p++; row++; }
        else break;
    }
    if (!sc[0]) {
        text(24, 3, editing ? "(type your code)" : "(click here to type)", VGA_DARK_GREY, VGA_BLACK);
        last_col = 24;
        last_row = 3;
    }
    if (editing)
        cell(last_col < 57 ? last_col : 56, last_row < 18 ? last_row : 17, '_', VGA_LIGHT_GREEN, VGA_BLACK);
}

static void draw_static_chrome(int sel_cmd, int editing) {
    fill(0, 0, 80, 1, ' ', VGA_BLACK, VGA_CYAN);
    text(1, 0, " FlorestOS GUI | FlorestNET++ | Made by FlorestDev", VGA_BLACK, VGA_CYAN);

    fill(0, 1, 22, 21, ' ', VGA_WHITE, VGA_DARK_GREY);
    text(1, 1, " Commands", VGA_YELLOW, VGA_DARK_GREY);
    for (int i = 0; i < CMD_N && i < 19; i++) {
        uint8_t bg = (i == sel_cmd) ? VGA_GREEN : VGA_DARK_GREY;
        uint8_t fg = (i == sel_cmd) ? VGA_BLACK : VGA_WHITE;
        fill(1, 2 + i, 20, 1, ' ', fg, bg);
        text_clip(2, 2 + i, CMDS[i].name, 18, fg, bg);
    }

    draw_editor_body(editing);

    fill(58, 1, 22, 18, ' ', VGA_LIGHT_GREY, VGA_BLACK);
    text(59, 1, " Docs", VGA_YELLOW, VGA_BLACK);
    if (sel_cmd >= 0 && sel_cmd < CMD_N) {
        text_clip(59, 3, CMDS[sel_cmd].name, 20, VGA_LIGHT_CYAN, VGA_BLACK);
        text_clip(59, 5, CMDS[sel_cmd].args, 20, VGA_LIGHT_GREEN, VGA_BLACK);
        const char* d = CMDS[sel_cmd].doc;
        int r = 7, c = 59;
        while (*d && r < 18) {
            if (c >= 78) { c = 59; r++; if (r >= 18) break; }
            cell(c++, r, *d++, VGA_LIGHT_GREY, VGA_BLACK);
        }
    }

    fill(0, 22, 80, 3, ' ', VGA_BLACK, VGA_CYAN);
    if (editing) {
        text(1, 22, "EDIT: type code | Backspace | Enter=newline | Esc=back", VGA_BLACK, VGA_CYAN);
        text(1, 23, "Your FlorestNET++ script is saved in the buffer", VGA_DARK_GREY, VGA_CYAN);
    } else {
        text(1, 22, "[1]Shell [2]Run [3]Insert [4]Clear [E]Edit [Esc]Exit", VGA_BLACK, VGA_CYAN);
        text(1, 23, "Click EDITOR to type code | Click cmd for docs", VGA_DARK_GREY, VGA_CYAN);
    }
    text(1, 24, "Up/Down select command", VGA_DARK_GREY, VGA_CYAN);
}

static void insert_template(int sel) {
    if (sel < 0 || sel >= CMD_N) return;
    char line[128];
    line[0] = 0;
    if (strcmp(CMDS[sel].name, "dns") == 0) strcat(line, "dns(\"example.com\")");
    else if (strcmp(CMDS[sel].name, "mcstatus") == 0) strcat(line, "mcstatus(\"play.example.com\", 25565)");
    else if (strcmp(CMDS[sel].name, "scan") == 0) strcat(line, "scan(\"1.1.1.1\", 80, 90)");
    else if (strcmp(CMDS[sel].name, "http") == 0) strcat(line, "http(\"example.com\", 80, \"/\")");
    else if (strcmp(CMDS[sel].name, "push") == 0) strcat(line, "push(\"10.0.2.2\", 8080, \"/\")");
    else if (strcmp(CMDS[sel].name, "results") == 0) strcat(line, "results()");
    else if (strcmp(CMDS[sel].name, "status") == 0) strcat(line, "status()");
    else if (strcmp(CMDS[sel].name, "udp") == 0) strcat(line, "udp(\"1.1.1.1\", 53, \"hi\")");
    else if (strcmp(CMDS[sel].name, "tcp") == 0) strcat(line, "tcp(\"1.1.1.1\", 80, \"GET /\")");
    else if (strcmp(CMDS[sel].name, "tls") == 0) strcat(line, "tls(\"1.1.1.1\", 443)");
    else if (strcmp(CMDS[sel].name, "println") == 0) strcat(line, "println(\"hello \" + (2+2))");
    else if (strcmp(CMDS[sel].name, "val") == 0) strcat(line, "val x = 10 + 5");
    else if (strcmp(CMDS[sel].name, "help") == 0) strcat(line, "help()");
    else { strcat(line, CMDS[sel].name); strcat(line, "()"); }
    fln_script_append_line(line);
}

/* Read one make-scancode for menu (not edit). Returns 0 if none. */
static uint8_t read_key_scancode(void) {
    uint8_t st = inb(0x64);
    if (!(st & 1) || (st & 0x20)) return 0; /* no key or mouse */
    uint8_t sc = inb(0x60);
    if (sc & 0x80) return 0;
    if (sc == 0xE0) return 0;
    return sc;
}

void gui_run(void) {
    mouse_init();
    keyboard_flush();

    int sel = 0;
    int running = 1;
    int editing = 0;
    int dirty = 1;
    int last_mx = -1, last_my = -1;

    fill(0, 0, 80, 25, ' ', VGA_LIGHT_GREY, VGA_BLUE);

    while (running) {
        if (dirty) {
            mouse_hide_cursor();
            draw_static_chrome(sel, editing);
            dirty = 0;
            if (!editing) mouse_draw_cursor();
        }

        if (!editing) {
            mouse_poll();
            int mx = mouse_x(), my = mouse_y();
            if (mx != last_mx || my != last_my) {
                mouse_hide_cursor();
                mouse_draw_cursor();
                last_mx = mx;
                last_my = my;
            }

            if (mouse_left_pressed()) {
                mx = mouse_x(); my = mouse_y();
                if (hit(mx, my, 22, 1, 36, 18)) {
                    editing = 1;
                    mouse_hide_cursor();
                    keyboard_flush();
                    dirty = 1;
                } else if (hit(mx, my, 1, 2, 20, CMD_N)) {
                    int i = my - 2;
                    if (i >= 0 && i < CMD_N) { sel = i; dirty = 1; }
                } else if (my >= 22) {
                    if (mx < 10) { running = 0; }
                    else if (mx < 20) {
                        mouse_hide_cursor();
                        mouse_disable();
                        keyboard_flush();
                        vga_clear(VGA_BLACK);
                        vga_print("\n  Running FlorestNET++...\n\n");
                        fln_eval(fln_script_buf());
                        vga_print("\n  [Enter] ");
                        char tmp[8];
                        keyboard_readline(tmp, sizeof(tmp));
                        keyboard_flush();
                        mouse_init();
                        dirty = 1;
                        fill(0, 0, 80, 25, ' ', VGA_LIGHT_GREY, VGA_BLUE);
                    } else if (mx < 36) { insert_template(sel); dirty = 1; }
                    else if (mx < 48) { fln_script_clear(); dirty = 1; }
                    else if (mx < 58) { editing = 1; mouse_hide_cursor(); keyboard_flush(); dirty = 1; }
                }
            }

            uint8_t sc = read_key_scancode();
            if (sc) {
                if (sc == 0x01 || sc == 0x02) running = 0;
                else if (sc == 0x03) {
                    mouse_hide_cursor();
                    mouse_disable();
                    keyboard_flush();
                    vga_clear(VGA_BLACK);
                    vga_print("\n  Running...\n\n");
                    fln_eval(fln_script_buf());
                    vga_print("\n  [Enter] ");
                    char tmp[8];
                    keyboard_readline(tmp, sizeof(tmp));
                    keyboard_flush();
                    mouse_init();
                    dirty = 1;
                    fill(0, 0, 80, 25, ' ', VGA_LIGHT_GREY, VGA_BLUE);
                } else if (sc == 0x04) { insert_template(sel); dirty = 1; }
                else if (sc == 0x05) { fln_script_clear(); dirty = 1; }
                else if (sc == 0x12) { editing = 1; keyboard_flush(); dirty = 1; }
                else if (sc == 0x48 && sel > 0) { sel--; dirty = 1; }
                else if (sc == 0x50 && sel + 1 < CMD_N) { sel++; dirty = 1; }
                else if (sc == 0x1C) { insert_template(sel); dirty = 1; }
            }
        } else {
            /* EDIT MODE: only keyboard_getchar — never raw mix with mouse */
            char c = keyboard_getchar();
            if (c == 27) {
                editing = 0;
                keyboard_flush();
                dirty = 1;
            } else if (c == '\b') {
                fln_script_backspace();
                dirty = 1;
            } else if (c == '\n' || c == '\r') {
                fln_script_putc('\n');
                dirty = 1;
            } else if (c >= 32 && c < 127) {
                fln_script_putc(c);
                dirty = 1;
            }
            /* still poll mouse bit so buffer doesn't stall — discard via mouse_poll */
            mouse_poll();
        }

        timer_sleep_ms(25);
    }

    mouse_hide_cursor();
    mouse_disable();
    keyboard_flush();
    for (volatile int i = 0; i < 500000; i++) {}
    keyboard_flush();
    vga_clear(VGA_BLACK);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
