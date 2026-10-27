#pragma once
#include "types.h"

/* FlorestNET++ — lightweight Kotlin-DSL style language for FlorestOS */

#define FLN_SCRIPT_MAX  4096
#define FLN_VARS        32
#define FLN_VAR_NAME    24
#define FLN_VAL_STR     192

void fln_init(void);

/* Register OS builtin: name -> handler(argc, argv) returns 0 on ok */
typedef int (*fln_builtin_fn)(int argc, char** argv);
void fln_register(const char* name, fln_builtin_fn fn);

/* Evaluate full source (multi-line). Returns 0 ok, -1 error */
int fln_eval(const char* source);

/* Script buffer for editor */
char*       fln_script_buf(void);
size_t      fln_script_len(void);
void        fln_script_clear(void);
void        fln_script_set(const char* src);
int         fln_script_append_line(const char* line); /* 0 ok, -1 full */
int         fln_script_putc(char c);
int         fln_script_backspace(void);

/* Console editor (blocks until .end / .quit) */
void fln_editor(void);
