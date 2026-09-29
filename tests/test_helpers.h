#ifndef GG_TEST_HELPERS_H
#define GG_TEST_HELPERS_H

void check(int condition, const char *message);
int test_failure_count(void);
void test_buffer(void);
void test_navigation(void);
void test_search(void);
void test_file_io(void);

#endif
