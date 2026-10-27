#pragma once
#include "types.h"

void keyboard_init(void);
void keyboard_flush(void); /* drain PS/2 buffer + reset modifiers */
char keyboard_getchar(void);
void keyboard_readline(char* buf, size_t max_len);
