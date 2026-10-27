#include "results.h"
#include "string.h"

static char g_log[RESULT_LOG_SIZE];
static size_t g_len = 0;

void results_clear(void) {
    g_len = 0;
    g_log[0] = 0;
}

void results_append(const char* line) {
    if (!line) return;
    size_t n = strlen(line);
    if (g_len + n + 2 >= RESULT_LOG_SIZE) return;
    memcpy(g_log + g_len, line, n);
    g_len += n;
    g_log[g_len++] = '\n';
    g_log[g_len] = 0;
}

const char* results_get(void) {
    return g_log;
}

size_t results_len(void) {
    return g_len;
}
