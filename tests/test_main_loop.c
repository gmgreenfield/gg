#include "editor.h"
#include "file_io.h"
#include "terminal.h"
#include "test_helpers.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int editor_program_main(int argc, char **argv);

static int reads;
static int saves;
static int saw_dirty_after_failed_save;
static int saw_error_status_after_failed_save;
static int saw_edit_after_failed_save;
static int saw_unsaved_quit_prompt;
static int retry_succeeds;
static int saw_clean_after_retry;
static int saw_error_cleared_after_retry;

static void reset_scenario(int succeed_on_retry) {
    reads = 0;
    saves = 0;
    saw_dirty_after_failed_save = 0;
    saw_error_status_after_failed_save = 0;
    saw_edit_after_failed_save = 0;
    saw_unsaved_quit_prompt = 0;
    retry_succeeds = succeed_on_retry;
    saw_clean_after_retry = 0;
    saw_error_cleared_after_retry = 0;
}

int load_file(editor_state *s) { return append_row(s, "original", 8); }

int save_file(const editor_state *s) {
    saves++;
    check(s->dirty && strcmp(s->file_rows[0].chars, "xoriginal") == 0,
          "save receives the modified buffer");
    if (retry_succeeds && saves == 2) {
        return 0;
    }
    errno = ENOSPC;
    return -1;
}

int enable_raw_mode(void) { return 0; }

void handle_resize(int signal_number) { (void)signal_number; }

int take_resize_pending(void) { return 0; }

int get_window_size(int *rows, int *cols) {
    *rows = 8;
    *cols = 80;
    return 0;
}

void refresh_screen(const editor_state *s) {
    if (saves == 0 || s->file_row_count == 0) {
        return;
    }

    if (reads == 2) {
        saw_dirty_after_failed_save = s->dirty;
        saw_error_status_after_failed_save = s->status_message != NULL;
    }

    if (retry_succeeds && saves == 2 && reads == 3) {
        saw_clean_after_retry = !s->dirty;
        saw_error_cleared_after_retry =
            s->status_message == NULL || strstr(s->status_message, "Save failed") == NULL;
    }

    if (strcmp(s->file_rows[0].chars, "xyoriginal") == 0) {
        saw_edit_after_failed_save = 1;
    }

    if (s->status_message != NULL && strstr(s->status_message, "Unsaved changes") != NULL) {
        saw_unsaved_quit_prompt = 1;
    }
}

int read_key(void) {
    static const int failure_keys[] = {'x', CTRL_KEY('s'), 'y', CTRL_KEY('q'), CTRL_KEY('q')};
    static const int retry_keys[] = {'x', CTRL_KEY('s'), CTRL_KEY('s'), CTRL_KEY('q')};
    const int *keys = retry_succeeds ? retry_keys : failure_keys;
    size_t key_count = retry_succeeds ? sizeof(retry_keys) / sizeof(retry_keys[0])
                                      : sizeof(failure_keys) / sizeof(failure_keys[0]);
    if ((size_t)reads >= key_count) {
        return -1;
    }
    return keys[reads++];
}

int search_prompt(editor_state *s) {
    (void)s;
    return 0;
}

int main(void) {
    char *argv[] = {"gg", "document", NULL};

    reset_scenario(0);
    int result = editor_program_main(2, argv);

    check(saves == 1, "Ctrl-S attempts one save");
    check(reads == 5, "a failed save does not exit the editor");
    check(saw_dirty_after_failed_save, "a failed save leaves the buffer modified");
    check(saw_error_status_after_failed_save, "a failed save shows a status message");
    check(saw_edit_after_failed_save, "editing remains possible after a failed save");
    check(saw_unsaved_quit_prompt, "quitting after a failed save still requires confirmation");
    check(result == EXIT_SUCCESS, "confirmed quit exits normally after the failed save");

    reset_scenario(1);
    result = editor_program_main(2, argv);

    check(saves == 2, "Ctrl-S retries saving after the first failure");
    check(reads == 4, "a successful retry allows quitting with one Ctrl-Q");
    check(saw_dirty_after_failed_save, "buffer stays modified until the retry succeeds");
    check(saw_error_status_after_failed_save, "the first failed attempt shows an error");
    check(saw_clean_after_retry, "successful retry clears the modified flag");
    check(saw_error_cleared_after_retry, "successful retry removes the stale save error");
    check(!saw_unsaved_quit_prompt, "successful retry needs no unsaved-changes warning");
    check(result == EXIT_SUCCESS, "quit succeeds after a successful retry");

    if (test_failure_count() != 0) {
        fprintf(stderr, "%d main-loop test(s) failed\n", test_failure_count());
        return EXIT_FAILURE;
    }

    puts("main-loop tests passed");
    return EXIT_SUCCESS;
}
