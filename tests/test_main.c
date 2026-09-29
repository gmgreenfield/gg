#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    test_buffer();
    test_navigation();
    test_search();
    test_file_io();

    int failures = test_failure_count();
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all tests passed");
    return EXIT_SUCCESS;
}
