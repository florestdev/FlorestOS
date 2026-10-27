#pragma once
#include "types.h"

#define RESULT_LOG_SIZE 2048

void results_clear(void);
void results_append(const char* line);
const char* results_get(void);
size_t results_len(void);
