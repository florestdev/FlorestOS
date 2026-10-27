#include "timer.h"
#include "io.h"

static volatile uint32_t g_ticks = 0;

void timer_init(void) {
    g_ticks = 0;
}

uint32_t timer_ticks(void) {
    return g_ticks;
}

/* Busy-wait ~ms. Tuned for QEMU; not precise, but must not hang for minutes. */
void timer_sleep_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile int j = 0; j < 2000; j++)
            asm volatile("pause");
    }
}
