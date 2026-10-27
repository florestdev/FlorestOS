/*
 * FlorestNET++ interpreter + console editor
 * Syntax inspired by Kotlin DSL / Gradle build scripts
 */

#include "fln.h"
#include "vga.h"
#include "keyboard.h"
#include "string.h"

struct fln_val {
    int  is_str;       /* 1 string, 0 number */
    int  num;
    char str[FLN_VAL_STR];
};

struct fln_var {
    bool used;
    char name[FLN_VAR_NAME];
    fln_val val;
};

struct fln_reg {
    bool used;
    char name[FLN_VAR_NAME];
    fln_builtin_fn fn;
};

static fln_var  g_vars[FLN_VARS];
static fln_reg  g_regs[24];
static char     g_script[FLN_SCRIPT_MAX];
static size_t   g_script_len = 0;
static char     g_err[96];

static void set_err(const char* m) {
    size_t i = 0;
    while (m[i] && i + 1 < sizeof(g_err)) { g_err[i] = m[i]; i++; }
    g_err[i] = 0;
}

static void skip_ws(const char** p) {
    while (**p == ' ' || **p == '\t' || **p == '\r') (*p)++;
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_ident(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int parse_ident(const char** p, char* out, int maxn) {
    skip_ws(p);
    if (!is_ident_start(**p)) return -1;
    int i = 0;
    while (is_ident(**p) && i + 1 < maxn) out[i++] = *(*p)++;
    out[i] = 0;
    return 0;
}

static fln_var* var_find(const char* name) {
    for (int i = 0; i < FLN_VARS; i++)
        if (g_vars[i].used && strcmp(g_vars[i].name, name) == 0)
            return &g_vars[i];
    return nullptr;
}

static fln_var* var_set(const char* name, const fln_val* v) {
    fln_var* e = var_find(name);
    if (!e) {
        for (int i = 0; i < FLN_VARS; i++) {
            if (!g_vars[i].used) {
                e = &g_vars[i];
                e->used = true;
                size_t n = 0;
                while (name[n] && n + 1 < FLN_VAR_NAME) { e->name[n] = name[n]; n++; }
                e->name[n] = 0;
                break;
            }
        }
    }
    if (!e) { set_err("too many vars"); return nullptr; }
    e->val = *v;
    return e;
}

static fln_builtin_fn builtin_find(const char* name) {
    for (int i = 0; i < 24; i++)
        if (g_regs[i].used && strcmp(g_regs[i].name, name) == 0)
            return g_regs[i].fn;
    return nullptr;
}

void fln_register(const char* name, fln_builtin_fn fn) {
    for (int i = 0; i < 24; i++) {
        if (g_regs[i].used && strcmp(g_regs[i].name, name) == 0) {
            g_regs[i].fn = fn;
            return;
        }
    }
    for (int i = 0; i < 24; i++) {
        if (!g_regs[i].used) {
            g_regs[i].used = true;
            size_t n = 0;
            while (name[n] && n + 1 < FLN_VAR_NAME) {
                g_regs[i].name[n] = name[n]; n++;
            }
            g_regs[i].name[n] = 0;
            g_regs[i].fn = fn;
            return;
        }
    }
}

void fln_init(void) {
    for (int i = 0; i < FLN_VARS; i++) g_vars[i].used = false;
    for (int i = 0; i < 24; i++) g_regs[i].used = false;
    g_script_len = 0;
    g_script[0] = 0;
    g_err[0] = 0;
}

char*  fln_script_buf(void) { return g_script; }
size_t fln_script_len(void) { return g_script_len; }
void   fln_script_clear(void) { g_script_len = 0; g_script[0] = 0; }

void fln_script_set(const char* src) {
    g_script_len = 0;
    while (src[g_script_len] && g_script_len + 1 < FLN_SCRIPT_MAX) {
        g_script[g_script_len] = src[g_script_len];
        g_script_len++;
    }
    g_script[g_script_len] = 0;
}


int fln_script_putc(char c) {
    if (g_script_len + 1 >= FLN_SCRIPT_MAX) return -1;
    g_script[g_script_len++] = c;
    g_script[g_script_len] = 0;
    return 0;
}

int fln_script_backspace(void) {
    if (g_script_len == 0) return -1;
    g_script[--g_script_len] = 0;
    return 0;
}

int fln_script_append_line(const char* line) {
    size_t n = strlen(line);
    if (g_script_len + n + 2 >= FLN_SCRIPT_MAX) return -1;
    if (g_script_len > 0 && g_script[g_script_len - 1] != '\n') {
        g_script[g_script_len++] = '\n';
        g_script[g_script_len] = 0;
    }
    for (size_t i = 0; i < n; i++) g_script[g_script_len++] = line[i];
    g_script[g_script_len++] = '\n';
    g_script[g_script_len] = 0;
    return 0;
}

/* ---- expression parser (numbers, strings, vars, + - * /) ---- */

static int parse_expr(const char** p, fln_val* out);

static int parse_primary(const char** p, fln_val* out) {
    skip_ws(p);
    if (**p == '"') {
        (*p)++;
        int i = 0;
        while (**p && **p != '"' && i + 1 < FLN_VAL_STR) {
            if (**p == '\\' && (*p)[1]) {
                (*p)++;
                char c = **p;
                if (c == 'n') out->str[i++] = '\n';
                else if (c == 't') out->str[i++] = '\t';
                else out->str[i++] = c;
                (*p)++;
            } else {
                out->str[i++] = *(*p)++;
            }
        }
        if (**p == '"') (*p)++;
        out->str[i] = 0;
        out->is_str = 1;
        out->num = 0;
        return 0;
    }
    if (**p == '-' || (**p >= '0' && **p <= '9')) {
        int sign = 1;
        if (**p == '-') { sign = -1; (*p)++; }
        int v = 0;
        if (**p < '0' || **p > '9') { set_err("bad number"); return -1; }
        while (**p >= '0' && **p <= '9') {
            v = v * 10 + (**p - '0');
            (*p)++;
        }
        out->is_str = 0;
        out->num = sign * v;
        out->str[0] = 0;
        return 0;
    }
    if (**p == '(') {
        (*p)++;
        if (parse_expr(p, out) < 0) return -1;
        skip_ws(p);
        if (**p != ')') { set_err("expected )"); return -1; }
        (*p)++;
        return 0;
    }
    if (is_ident_start(**p)) {
        char name[FLN_VAR_NAME];
        if (parse_ident(p, name, FLN_VAR_NAME) < 0) return -1;
        fln_var* e = var_find(name);
        if (!e) { set_err("unknown var"); return -1; }
        *out = e->val;
        return 0;
    }
    set_err("bad expression");
    return -1;
}

static int parse_term(const char** p, fln_val* out) {
    if (parse_primary(p, out) < 0) return -1;
    for (;;) {
        skip_ws(p);
        char op = **p;
        if (op != '*' && op != '/') break;
        (*p)++;
        fln_val rhs;
        if (parse_primary(p, &rhs) < 0) return -1;
        if (out->is_str || rhs.is_str) { set_err("math on string"); return -1; }
        if (op == '*') out->num *= rhs.num;
        else {
            if (rhs.num == 0) { set_err("div by zero"); return -1; }
            out->num /= rhs.num;
        }
    }
    return 0;
}

static int parse_expr(const char** p, fln_val* out) {
    if (parse_term(p, out) < 0) return -1;
    for (;;) {
        skip_ws(p);
        char op = **p;
        if (op != '+' && op != '-') break;
        (*p)++;
        fln_val rhs;
        if (parse_term(p, &rhs) < 0) return -1;
        if (op == '+' && (out->is_str || rhs.is_str)) {
            /* string concat */
            char buf[FLN_VAL_STR];
            buf[0] = 0;
            if (out->is_str) {
                strcat(buf, out->str);
            } else {
                /* num to string */
                char tmp[16]; int x = out->num, i = 0, neg = 0;
                if (x < 0) { neg = 1; x = -x; }
                if (x == 0) { tmp[i++] = '0'; }
                else { char r[16]; int j = 0; while (x) { r[j++] = '0'+(x%10); x/=10; }
                       if (neg) tmp[i++] = '-'; while (j--) tmp[i++] = r[j]; }
                tmp[i] = 0; strcat(buf, tmp);
            }
            if (rhs.is_str) strcat(buf, rhs.str);
            else {
                char tmp[16]; int x = rhs.num, i = 0, neg = 0;
                if (x < 0) { neg = 1; x = -x; }
                if (x == 0) { tmp[i++] = '0'; }
                else { char r[16]; int j = 0; while (x) { r[j++] = '0'+(x%10); x/=10; }
                       if (neg) tmp[i++] = '-'; while (j--) tmp[i++] = r[j]; }
                tmp[i] = 0; strcat(buf, tmp);
            }
            out->is_str = 1;
            out->num = 0;
            size_t k = 0;
            while (buf[k] && k + 1 < FLN_VAL_STR) { out->str[k] = buf[k]; k++; }
            out->str[k] = 0;
        } else if (!out->is_str && !rhs.is_str) {
            if (op == '+') out->num += rhs.num;
            else out->num -= rhs.num;
        } else {
            set_err("type error in +/-");
            return -1;
        }
    }
    return 0;
}

/* parse call args: ( a , b , c ) into argv strings */
static int parse_args(const char** p, char argv[][FLN_VAL_STR], int max_args, int* argc) {
    skip_ws(p);
    if (**p != '(') { set_err("expected ("); return -1; }
    (*p)++;
    *argc = 0;
    skip_ws(p);
    if (**p == ')') { (*p)++; return 0; }
    while (*argc < max_args) {
        fln_val v;
        if (parse_expr(p, &v) < 0) return -1;
        if (v.is_str) {
            size_t k = 0;
            while (v.str[k] && k + 1 < FLN_VAL_STR) {
                argv[*argc][k] = v.str[k]; k++;
            }
            argv[*argc][k] = 0;
        } else {
            /* number as decimal string */
            char tmp[16]; int x = v.num, i = 0, neg = 0;
            if (x < 0) { neg = 1; x = -x; }
            if (x == 0) { tmp[i++] = '0'; }
            else {
                char r[16]; int j = 0;
                while (x) { r[j++] = '0' + (x % 10); x /= 10; }
                if (neg) tmp[i++] = '-';
                while (j--) tmp[i++] = r[j];
            }
            tmp[i] = 0;
            size_t k = 0;
            while (tmp[k]) { argv[*argc][k] = tmp[k]; k++; }
            argv[*argc][k] = 0;
        }
        (*argc)++;
        skip_ws(p);
        if (**p == ',') { (*p)++; skip_ws(p); continue; }
        if (**p == ')') { (*p)++; return 0; }
        set_err("expected , or )");
        return -1;
    }
    set_err("too many args");
    return -1;
}

static void print_val(const fln_val* v) {
    if (v->is_str) vga_print(v->str);
    else {
        int x = v->num;
        if (x == 0) { vga_putchar('0'); return; }
        if (x < 0) { vga_putchar('-'); x = -x; }
        char t[16]; int i = 0;
        while (x) { t[i++] = '0' + (x % 10); x /= 10; }
        while (i--) vga_putchar(t[i]);
    }
}

static int eval_line(const char* line) {
    const char* p = line;
    skip_ws(&p);
    if (!*p) return 0;
    if (p[0] == '/' && p[1] == '/') return 0; /* comment */

    /* val name = expr  |  var name = expr */
    char kw[FLN_VAR_NAME];
    const char* save = p;
    if (parse_ident(&p, kw, FLN_VAR_NAME) == 0) {
        if (strcmp(kw, "val") == 0 || strcmp(kw, "var") == 0) {
            char name[FLN_VAR_NAME];
            if (parse_ident(&p, name, FLN_VAR_NAME) < 0) { set_err("val name"); return -1; }
            skip_ws(&p);
            if (*p != '=') { set_err("expected ="); return -1; }
            p++;
            fln_val v;
            if (parse_expr(&p, &v) < 0) return -1;
            if (!var_set(name, &v)) return -1;
            return 0;
        }
        /* println(x) / print(x) */
        if (strcmp(kw, "println") == 0 || strcmp(kw, "print") == 0) {
            char args[6][FLN_VAL_STR];
            int argc = 0;
            if (parse_args(&p, args, 6, &argc) < 0) return -1;
            for (int i = 0; i < argc; i++) {
                if (i) vga_putchar(' ');
                /* try as var else print raw */
                fln_var* e = var_find(args[i]);
                if (e) print_val(&e->val);
                else vga_print(args[i]);
            }
            if (strcmp(kw, "println") == 0) vga_print("\n");
            return 0;
        }
        /* assignment name = expr */
        skip_ws(&p);
        if (*p == '=') {
            p++;
            fln_val v;
            if (parse_expr(&p, &v) < 0) return -1;
            if (!var_set(kw, &v)) return -1;
            return 0;
        }
        /* function call name(...) */
        if (*p == '(') {
            char args[8][FLN_VAL_STR];
            int argc = 0;
            if (parse_args(&p, args, 8, &argc) < 0) return -1;

            fln_builtin_fn fn = builtin_find(kw);
            if (!fn) { set_err("unknown function"); return -1; }

            char* argv[10];
            argv[0] = kw;
            for (int i = 0; i < argc; i++) argv[i + 1] = args[i];
            return fn(argc + 1, argv);
        }
    }
    p = save;
    set_err("syntax error");
    return -1;
}

int fln_eval(const char* source) {
    g_err[0] = 0;
    /* reset vars each run for predictability */
    for (int i = 0; i < FLN_VARS; i++) g_vars[i].used = false;

    char line[256];
    const char* s = source;
    int lineno = 0;
    while (*s) {
        int i = 0;
        while (*s && *s != '\n' && i + 1 < (int)sizeof(line))
            line[i++] = *s++;
        line[i] = 0;
        if (*s == '\n') s++;
        lineno++;
        if (eval_line(line) < 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_print("  [FlorestNET++] line ");
            /* print lineno */
            int x = lineno; char t[8]; int n = 0;
            if (x == 0) vga_putchar('0');
            else { while (x) { t[n++] = '0'+(x%10); x/=10; } while (n--) vga_putchar(t[n]); }
            vga_print(": ");
            vga_print(g_err[0] ? g_err : "error");
            vga_print("\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            return -1;
        }
    }
    return 0;
}


static int is_kw(const char* s, int n) {
    const char* kws[] = { "val", "var", "println", "print", "dns", "mcstatus",
        "push", "results", "resultsClear", "scan", "http", "status",
        "udp", "tcp", "tls", "help", nullptr };
    for (int i = 0; kws[i]; i++) {
        int k = 0; while (kws[i][k]) k++;
        if (k != n) continue;
        int ok = 1;
        for (int j = 0; j < n; j++) if (s[j] != kws[i][j]) { ok = 0; break; }
        if (ok) return 1;
    }
    return 0;
}

void fln_print_highlighted(const char* src) {
    const char* p = src;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            while (*p && *p != '\n') vga_putchar(*p++);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            continue;
        }
        if (*p == '"') {
            vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            vga_putchar(*p++);
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) { vga_putchar(*p++); vga_putchar(*p++); }
                else vga_putchar(*p++);
            }
            if (*p == '"') vga_putchar(*p++);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            continue;
        }
        if (*p >= '0' && *p <= '9') {
            vga_set_color(VGA_YELLOW, VGA_BLACK);
            while (*p >= '0' && *p <= '9') vga_putchar(*p++);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            continue;
        }
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            const char* s = p;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') p++;
            int n = (int)(p - s);
            if (is_kw(s, n)) vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            else vga_set_color(VGA_WHITE, VGA_BLACK);
            for (int i = 0; i < n; i++) vga_putchar(s[i]);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            continue;
        }
        vga_putchar(*p++);
    }
}

void fln_editor(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_print("\n  === FlorestNET++ editor ===\n");
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_print("  .end   save & exit\n");
    vga_print("  .run   run buffer now\n");
    vga_print("  .clear clear buffer\n");
    vga_print("  .show  show buffer\n");
    vga_print("  .quit  exit without save of last line\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_print("  Kotlin-DSL style, e.g.:\n");
    vga_print("    val x = 2 + 3\n");
    vga_print("    println(x)\n");
    vga_print("    dns(\"example.com\")\n\n");

    char line[240];
    for (;;) {
        vga_set_color(VGA_LIGHT_MAGENTA, VGA_BLACK);
        vga_print("fln| ");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        keyboard_readline(line, sizeof(line));
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

        if (strcmp(line, ".end") == 0) {
            vga_print("  [editor] saved, ");
            int x = (int)g_script_len; char tb[8]; int n = 0;
            if (x == 0) vga_print("0");
            else { while (x) { tb[n++] = '0'+(x%10); x/=10; } while (n--) vga_putchar(tb[n]); }
            vga_print(" bytes\n");
            return;
        }
        if (strcmp(line, ".quit") == 0) {
            vga_print("  [editor] quit\n");
            return;
        }
        if (strcmp(line, ".clear") == 0) {
            fln_script_clear();
            vga_print("  [editor] cleared\n");
            continue;
        }
        if (strcmp(line, ".show") == 0) {
            vga_print("--- script ---\n");
            if (g_script[0]) fln_print_highlighted(g_script); else vga_print("(empty)\n");
            vga_print("--------------\n");
            continue;
        }
        if (strcmp(line, ".run") == 0) {
            vga_print("  [FlorestNET++] running...\n");
            fln_eval(g_script);
            continue;
        }
        if (line[0] == 0) continue;
        if (fln_script_append_line(line) < 0)
            vga_print("  [editor] buffer full\n");
    }
}
