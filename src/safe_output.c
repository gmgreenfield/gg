#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "safe_output.h"

void write_safe_terminal_text(FILE *fp, const char *bytes, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = bytes[i];

        if (c >= 0x20 && c <= 0x7E) {
            fputc(c, fp);
        } else {
            fprintf(fp, "?");
        }
    }
}
