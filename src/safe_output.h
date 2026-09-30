#ifndef GG_SAFE_OUTPUT_H
#define GG_SAFE_OUTPUT_H

#include <stddef.h>
#include <stdio.h>

void write_safe_terminal_text(FILE *fp, const char *bytes, size_t length);

#endif
