#include "test_helpers.h"

#include <stdio.h>

static int failures;

void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int test_failure_count(void) { return failures; }
