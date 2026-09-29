#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "editor.h"
#include "file_io.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Only the test build of file_io.c redirects these calls. Fixtures use libc. */
size_t test_save_fwrite(const void *data, size_t size, size_t count, FILE *stream);
int test_save_rename(const char *old_path, const char *new_path);

static int failures;

enum save_fault { SAVE_FAULT_NONE, SAVE_FAULT_WRITE, SAVE_FAULT_RENAME };
static enum save_fault active_save_fault;
static size_t partial_write_bytes;
static int rename_attempts;

size_t test_save_fwrite(const void *data, size_t size, size_t count, FILE *stream) {
    if (active_save_fault != SAVE_FAULT_WRITE) {
        return fwrite(data, size, count, stream);
    }

    /* Put a real prefix on disk, then simulate a disk-full short write. */
    size_t written = fwrite(data, size, count / 2, stream);
    if (fflush(stream) == 0) {
        partial_write_bytes += written * size;
    }
    errno = ENOSPC;
    return written;
}

int test_save_rename(const char *old_path, const char *new_path) {
    if (active_save_fault == SAVE_FAULT_RENAME) {
        rename_attempts++;
        errno = EACCES;
        return -1;
    }
    return rename(old_path, new_path);
}

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_editing_operations(void) {
    editor_state state = {0};

    check(append_row(&state, "abc", 3) == 0, "append initial row");

    state.cursor_x = 1;
    check(insert_char(&state, 'X') == 0, "insert character");
    check(strcmp(state.file_rows[0].chars, "aXbc") == 0, "inserted character content");
    check(state.cursor_x == 2, "cursor advances after insertion");

    check(delete_char(&state) == 0, "delete character");
    check(strcmp(state.file_rows[0].chars, "abc") == 0, "deleted character content");
    check(state.cursor_x == 1, "cursor retreats after deletion");

    check(insert_newline(&state) == 0, "split row at cursor");
    check(state.file_row_count == 2, "row count after split");
    check(strcmp(state.file_rows[0].chars, "a") == 0, "first row after split");
    check(strcmp(state.file_rows[1].chars, "bc") == 0, "second row after split");

    check(delete_char(&state) == 0, "join rows at column zero");
    check(state.file_row_count == 1, "row count after join");
    check(strcmp(state.file_rows[0].chars, "abc") == 0, "joined row content");

    free_rows(&state);
}

static void test_scrolling(void) {
    editor_state state = {
        .cursor_y = 9,
        .cursor_x = 19,
        .screen_rows = 5,
        .screen_cols = 10,
    };

    scroll_cursor(&state);
    check(state.row_offset == 6, "vertical scroll follows cursor");
    check(state.col_offset == 10, "horizontal scroll follows cursor");

    state.cursor_y = 2;
    state.cursor_x = 4;
    scroll_cursor(&state);
    check(state.row_offset == 2, "vertical scroll follows cursor upward");
    check(state.col_offset == 4, "horizontal scroll follows cursor left");
}

static void test_home_end_navigation(void) {
    editor_state state = {0};

    check(append_row(&state, "hello", 5) == 0, "append Home/End test row");
    state.cursor_x = 3;

    move_cursor_home(&state);
    check(state.cursor_x == 0, "Home moves to the beginning of the line");

    move_cursor_end(&state);
    check(state.cursor_x == 5, "End moves to the end of the line");

    free_rows(&state);
}

static void test_page_navigation(void) {
    editor_state state = {
        .cursor_y = 0,
        .cursor_x = 4,
        .screen_rows = 4,
    };

    check(append_row(&state, "first", 5) == 0, "append first page row");
    check(append_row(&state, "hello", 5) == 0, "append second page row");
    check(append_row(&state, "two", 3) == 0, "append third page row");
    check(append_row(&state, "three", 5) == 0, "append fourth page row");
    check(append_row(&state, "end", 3) == 0, "append fifth page row");

    move_cursor_page_down(&state);
    check(state.cursor_y == 3, "Page Down moves by the text viewport height");
    check(state.cursor_x == 4, "Page Down preserves the requested column when possible");

    move_cursor_page_up(&state);
    check(state.cursor_y == 0, "Page Up moves by the text viewport height");
    check(state.cursor_x == 4, "Page Up restores the requested column when possible");

    state.cursor_y = 1;
    state.cursor_x = 4;
    move_cursor_page_down(&state);
    check(state.cursor_y == 4, "Page Down moves to the final row");
    check(state.cursor_x == 3, "Page Down clamps to a shorter destination row");

    move_cursor_page_down(&state);
    check(state.cursor_y == 4, "Page Down clamps at the last row");

    free_rows(&state);
}

static void test_page_viewport(void) {
    editor_state state = {.screen_rows = 8, .screen_cols = 80};
    for (int i = 0; i < 21; i++) {
        int result = append_row(&state, "example", 7);
        check(result == 0, "create paging viewport fixture");
        if (result != 0) {
            free_rows(&state);
            return;
        }
    }

    /* Seven text rows; move the view with the cursor, including partial
       movement at file boundaries. Each case starts independently. */
    const struct {
        const char *name;
        int down;
        int cursor;
        size_t offset;
        int expected_cursor;
        size_t expected_offset;
    } cases[] = {
        {"Page Down from screen top", 1, 0, 0, 7, 7},
        {"Page Down from screen middle", 1, 3, 0, 10, 7},
        {"Page Down from screen bottom", 1, 6, 0, 13, 7},
        {"Page Up from screen top", 0, 7, 7, 0, 0},
        {"Page Up from screen middle", 0, 10, 7, 3, 0},
        {"Page Up from screen bottom", 0, 13, 7, 6, 0},
        {"Page Down near end of file", 1, 17, 14, 20, 17},
        {"Page Down at end of file", 1, 20, 17, 20, 17},
        {"Page Up near start of file", 0, 6, 3, 0, 0},
        {"Page Up at start of file", 0, 0, 0, 0, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        state.cursor_y = cases[i].cursor;
        state.cursor_x = 2;
        state.row_offset = cases[i].offset;
        state.col_offset = 0;

        if (cases[i].down)
            move_cursor_page_down(&state);
        else
            move_cursor_page_up(&state);

        /* Include the same visibility adjustment used before each redraw. */
        scroll_cursor(&state);

        char message[160];
        snprintf(message, sizeof(message), "%s: cursor position", cases[i].name);
        check(state.cursor_y == cases[i].expected_cursor, message);
        snprintf(message, sizeof(message), "%s: viewport offset", cases[i].name);
        check(state.row_offset == cases[i].expected_offset, message);
        check(state.cursor_x == 2, "paging preserves a valid column");
        check(state.dirty == 0, "paging does not mark the document modified");
    }

    free_rows(&state);
}

static void test_search(void) {
    editor_state state = {0};
    int match_row;
    int match_col;

    check(append_row(&state, "alpha beta", 10) == 0, "append first search row");
    check(append_row(&state, "gamma alpha", 11) == 0, "append second search row");
    check(append_row(&state, "alpha alpha", 11) == 0, "append repeated-match row");
    check(append_row(&state, "", 0) == 0, "append empty search row");
    check(append_row(&state, "cross", 5) == 0, "append cross-line search row");
    check(append_row(&state, "line", 4) == 0, "append second cross-line row");

    check(find_next_match(&state, "alpha", 0, 0, &match_row, &match_col) == 0,
          "find first search match");
    check(match_row == 0 && match_col == 0, "first search match position");

    check(find_next_match(&state, "alpha", 0, 1, &match_row, &match_col) == 0,
          "find next search match");
    check(match_row == 1 && match_col == 6, "next search match position");

    check(find_next_match(&state, "alpha", 5, 0, &match_row, &match_col) == 0,
          "search wraps around at end of file");
    check(match_row == 0 && match_col == 0, "wrapped search match position");

    check(find_next_match(&state, "missing", 0, 0, &match_row, &match_col) == -1,
          "search reports a missing match");

    check(find_next_match(&state, "", 0, 0, &match_row, &match_col) == -1,
          "empty search query is rejected");

    check(find_next_match(&state, "beta", 0, 6, &match_row, &match_col) == 0,
          "search includes a match at the starting column");
    check(match_row == 0 && match_col == 6, "exact starting-column match position");

    check(find_next_match(&state, "beta", 0, 7, &match_row, &match_col) == 0,
          "search wraps within the starting row");
    check(match_row == 0 && match_col == 6, "same-row wrapped match position");

    check(find_next_match(&state, "alpha", -1, 0, &match_row, &match_col) == -1,
          "negative starting row is rejected");
    check(find_next_match(&state, "alpha", 0, 99, &match_row, &match_col) == -1,
          "starting column past the row is rejected");

    check(find_next_match(&state, "alpha", 2, 1, &match_row, &match_col) == 0,
          "search finds a later match on the same row");
    check(match_row == 2 && match_col == 6, "later same-row match position");

    check(find_next_match(&state, "solo", 2, 6, &match_row, &match_col) == -1,
          "missing same-row query is reported when no match exists");

    check(find_next_match(&state, "a", 1, 10, &match_row, &match_col) == 0,
          "search finds a match at the end of a row");
    check(match_row == 1 && match_col == 10, "end-of-row match position");

    check(find_next_match(&state, "Alpha", 0, 0, &match_row, &match_col) == -1,
          "search remains case sensitive");
    check(find_next_match(&state, "cross\nline", 0, 0, &match_row, &match_col) == -1,
          "search does not cross line boundaries");
    check(find_next_match(&state, "missing", 3, 0, &match_row, &match_col) == -1,
          "search reports no match in an empty row");
    check(find_next_match(NULL, "alpha", 0, 0, &match_row, &match_col) == -1,
          "null editor state is rejected");
    check(find_next_match(&state, NULL, 0, 0, &match_row, &match_col) == -1,
          "null search query is rejected");
    check(find_next_match(&state, "alpha", 0, 0, NULL, &match_col) == -1,
          "null match row output is rejected");
    check(find_next_match(&state, "alpha", 0, 0, &match_row, NULL) == -1,
          "null match column output is rejected");

    free_rows(&state);
}

static void test_load_save(void) {
    char path[] = "/tmp/gg-editor-test-XXXXXX";
    int fd = mkstemp(path);
    check(fd != -1, "create temporary file");
    if (fd == -1) {
        return;
    }

    const char input[] = "first\nsecond\n";
    check(write(fd, input, sizeof(input) - 1) == (ssize_t)(sizeof(input) - 1),
          "write temporary file");
    check(fchmod(fd, 0640) == 0, "set original file permissions");
    check(close(fd) == 0, "close temporary file");

    editor_state state = {.filename = path};
    check(load_file(&state) == 0, "load file");
    check(state.file_row_count == 2, "loaded row count");
    check(state.final_newline != 0, "preserve final newline");
    check(save_file(&state) == 0, "save file");

    char backup_path[sizeof(path) + 1];
    snprintf(backup_path, sizeof(backup_path), "%s~", path);
    check(access(backup_path, F_OK) == -1 && errno == ENOENT,
          "successful save does not leave a persistent backup");

    struct stat saved;
    int stat_result = stat(path, &saved);
    check(stat_result == 0, "inspect saved file permissions");
    if (stat_result == 0) {
        check((saved.st_mode & 0777) == 0640, "saving preserves file permissions");
    }

    fd = open(path, O_RDONLY);
    check(fd != -1, "reopen saved file");
    if (fd != -1) {
        char output[sizeof(input)] = {0};
        ssize_t length = read(fd, output, sizeof(output));
        check(length == (ssize_t)(sizeof(input) - 1), "saved file length");
        check(memcmp(output, input, sizeof(input) - 1) == 0, "saved file content");
        close(fd);
    }

    free_rows(&state);
    unlink(path);
}

static void test_failed_save_preserves_original(void) {
    char path[] = "/tmp/gg-save-failure-XXXXXX";
    int fd = mkstemp(path);
    check(fd != -1, "create original for failed-save test");
    if (fd == -1) {
        return;
    }

    const char original[] = "Original content must survive.\n";
    ssize_t written = write(fd, original, sizeof(original) - 1);
    int close_result = close(fd);
    check(written == (ssize_t)(sizeof(original) - 1), "write failed-save fixture");
    check(close_result == 0, "close failed-save fixture");
    if (written != (ssize_t)(sizeof(original) - 1) || close_result != 0) {
        unlink(path);
        return;
    }

    editor_state state = {.filename = path, .dirty = 1};
    char replacement[256];
    memset(replacement, 'X', sizeof(replacement));
    int result = append_row(&state, replacement, sizeof(replacement));
    check(result == 0, "prepare replacement text for failed save");
    if (result == -1) {
        unlink(path);
        return;
    }

    /* Restrict only the child: writes beyond 16 bytes must fail on both
       macOS and Linux, without changing limits for the test runner. */
    fflush(NULL);
    pid_t child = fork();
    check(child != -1, "start failed-save child");
    if (child == 0) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_FSIZE, &limit) == -1 || limit.rlim_max < 16) {
            _exit(2);
        }
        limit.rlim_cur = 16;
        if (signal(SIGXFSZ, SIG_IGN) == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limit) == -1) {
            _exit(2);
        }
        int save_result = save_file(&state);
        _exit(save_result == -1 && state.dirty == 1 ? 0 : 1);
    }

    if (child > 0) {
        int status;
        pid_t waited;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited == -1 && errno == EINTR);
        check(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "save reports a forced write failure and leaves edits unsaved");

        fd = open(path, O_RDONLY);
        check(fd != -1, "original path still exists after failed save");
        if (fd != -1) {
            char actual[sizeof(original)] = {0};
            ssize_t length = read(fd, actual, sizeof(actual));
            check(length == (ssize_t)(sizeof(original) - 1) &&
                      memcmp(actual, original, sizeof(original) - 1) == 0,
                  "failed save leaves the original contents unchanged");
            check(close(fd) == 0, "close original after checking failed save");
        }
    }

    free_rows(&state);
    check(unlink(path) == 0, "remove failed-save test fixture");
}

static void check_save_fault(int condition, enum save_fault fault, const char *message) {
    char description[200];
    snprintf(description, sizeof(description), "%s: %s",
             fault == SAVE_FAULT_WRITE ? "mid-write failure" : "rename failure", message);
    check(condition, description);
}

static void test_save_fault(enum save_fault fault) {
    char directory[] = "/tmp/gg-save-fault-XXXXXX";
    int created = mkdtemp(directory) != NULL;
    check_save_fault(created, fault, "create isolated directory");
    if (!created) {
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/document", directory);
    editor_state state = {.filename = path, .dirty = 1};
    const char original[] = "Original file contents must survive.\n";
    const char replacement[] = "Completely different replacement contents.";

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    check_save_fault(fd != -1, fault, "create original file");
    if (fd == -1) {
        rmdir(directory);
        return;
    }
    ssize_t written = write(fd, original, sizeof(original) - 1);
    int close_result = close(fd);
    int prepared = written == (ssize_t)(sizeof(original) - 1) && close_result == 0;
    check_save_fault(prepared, fault, "prepare original contents");
    if (!prepared) {
        goto cleanup;
    }

    int appended = append_row(&state, replacement, sizeof(replacement) - 1);
    check_save_fault(appended == 0, fault, "prepare edited buffer");
    if (appended == -1) {
        goto cleanup;
    }

    partial_write_bytes = 0;
    rename_attempts = 0;
    active_save_fault = fault;
    int result = save_file(&state);
    active_save_fault = SAVE_FAULT_NONE;

    check_save_fault(result == -1, fault, "save reports failure");
    if (fault == SAVE_FAULT_WRITE) {
        check_save_fault(partial_write_bytes > 0 && partial_write_bytes < sizeof(replacement) - 1,
                         fault, "error occurs after some new data reaches disk");
    } else {
        check_save_fault(rename_attempts > 0, fault, "save attempts replacement");
    }
    check_save_fault(state.dirty == 1, fault, "buffer remains modified");

    fd = open(path, O_RDONLY);
    check_save_fault(fd != -1, fault, "original path remains accessible");
    if (fd != -1) {
        char contents[sizeof(original)] = {0};
        ssize_t length = read(fd, contents, sizeof(contents));
        check_save_fault(length == (ssize_t)(sizeof(original) - 1) &&
                             memcmp(contents, original, sizeof(original) - 1) == 0,
                         fault, "original contents remain unchanged");
        check_save_fault(close(fd) == 0, fault, "close original file");
    }

cleanup:
    free_rows(&state);
    /* Assert cleanup before removing any artifacts left by a broken save.
       Only inspect files in this test's private directory. */
    DIR *entries = opendir(directory);
    check_save_fault(entries != NULL, fault, "inspect temporary-file cleanup");
    if (entries != NULL) {
        size_t leftovers = 0;
        struct dirent *entry;
        int read_error;
        for (;;) {
            errno = 0;
            entry = readdir(entries);
            if (entry == NULL) {
                read_error = errno;
                break;
            }
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
                strcmp(entry->d_name, "document") == 0) {
                continue;
            }
            leftovers++;
            char artifact[512];
            int length = snprintf(artifact, sizeof(artifact), "%s/%s", directory, entry->d_name);
            if (length >= 0 && (size_t)length < sizeof(artifact)) {
                check_save_fault(unlink(artifact) == 0, fault, "remove leaked test artifact");
            }
        }
        check_save_fault(read_error == 0, fault, "read directory entries");
        check_save_fault(leftovers == 0, fault, "no temporary files left after failed save");
        check_save_fault(closedir(entries) == 0, fault, "close fixture directory");
    }
    check_save_fault(unlink(path) == 0, fault, "remove original test fixture");
    check_save_fault(rmdir(directory) == 0, fault, "remove isolated directory");
}

int main(void) {
    test_editing_operations();
    test_scrolling();
    test_home_end_navigation();
    test_page_navigation();
    test_page_viewport();
    test_search();
    test_load_save();
    test_failed_save_preserves_original();
    test_save_fault(SAVE_FAULT_WRITE);
    test_save_fault(SAVE_FAULT_RENAME);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    puts("all tests passed");
    return 0;
}
