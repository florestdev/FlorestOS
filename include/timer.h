#pragma once
#include "types.h"

/* PIT-based crude timer. Call timer_init once. */
void timer_init(void);
uint32_t timer_ticks(void);          /* ~18.2 Hz ticks since init */
void timer_sleep_ms(uint32_t ms);    /* busy-wait using PIT */
