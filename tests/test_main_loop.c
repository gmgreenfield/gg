#include "editor.h"
#include "file_io.h"
#include "terminal.h"
#include "test_helpers.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int editor_program_main(int argc, char **argv);

enum test_scenario {
    NAMED_SAVE_FAILURE,
    NAMED_SAVE_RETRY,
    NAMED_SAVE_THEN_EDIT,
    UNNAMED_SAVE_SUCCESS,
    UNNAMED_SAVE_CANCEL,
    UNNAMED_SAVE_FAILURE,
    READ_ONLY
};

static enum test_scenario scenario;
static int reads;
static int saves;
static int prompts;
static int timed_reads;
static int successful_save;
static int saw_dirty_after_failed_save;
static int saw_error_status_after_failed_save;
static int saw_edit_after_failed_save;
static int saw_unsaved_quit_prompt;
static int saw_clean_after_retry;
static int saw_error_cleared_after_retry;
static int saw_saved_status;
static int saw_status_expired;
static int saw_status_cleared_on_edit;
static int saw_prompt_while_unnamed;
static int saw_unnamed_save_target;
static int saw_unnamed_clean_after_save;
static int saw_unnamed_after_failed_save;
static int searches;
static int saw_read_only_mode;
static int saw_read_only_notice;
static int saw_read_only_change;
static int saw_read_only_navigation;
static int tty_selections;
static int raw_mode_calls;
static int fail_tty_selection;
static int saw_stdin_filename;

static void reset_scenario(enum test_scenario next_scenario) {
    scenario = next_scenario;
    reads = 0;
    saves = 0;
    prompts = 0;
    timed_reads = 0;
    successful_save = 0;
    saw_dirty_after_failed_save = 0;
    saw_error_status_after_failed_save = 0;
    saw_edit_after_failed_save = 0;
    saw_unsaved_quit_prompt = 0;
    saw_clean_after_retry = 0;
    saw_error_cleared_after_retry = 0;
    saw_saved_status = 0;
    saw_status_expired = 0;
    saw_status_cleared_on_edit = 0;
    saw_prompt_while_unnamed = 0;
    saw_unnamed_save_target = 0;
    saw_unnamed_clean_after_save = 0;
    saw_unnamed_after_failed_save = 0;
    searches = 0;
    saw_read_only_mode = 0;
    saw_read_only_notice = 0;
    saw_read_only_change = 0;
    saw_read_only_navigation = 0;
    tty_selections = 0;
    raw_mode_calls = 0;
    fail_tty_selection = 0;
    saw_stdin_filename = 0;
}

int load_file(editor_state *s) {
    saw_stdin_filename = s->filename != NULL && strcmp(s->filename, "-") == 0;
    if (append_row(s, "original", 8) == -1) {
        return -1;
    }
    if (scenario == READ_ONLY) {
        return append_row(s, "second", 6);
    }
    return 0;
}

int save_file(const editor_state *s) {
    saves++;
    check(s->dirty && strcmp(s->file_rows[0].chars, "xoriginal") == 0,
          "save receives the modified buffer");
    if (scenario == UNNAMED_SAVE_SUCCESS || scenario == UNNAMED_SAVE_FAILURE) {
        saw_unnamed_save_target = s->filename != NULL && strcmp(s->filename, "new-document") == 0;
    }
    if ((scenario == NAMED_SAVE_RETRY && saves == 2) || scenario == NAMED_SAVE_THEN_EDIT ||
        scenario == UNNAMED_SAVE_SUCCESS) {
        successful_save = 1;
        return 0;
    }
    errno = ENOSPC;
    return -1;
}

int use_tty_input(void) {
    tty_selections++;
    return fail_tty_selection ? -1 : 0;
}

int enable_raw_mode(void) {
    raw_mode_calls++;
    return 0;
}

void handle_resize(int signal_number) { (void)signal_number; }

int take_resize_pending(void) { return 0; }

int get_window_size(int *rows, int *cols) {
    *rows = 8;
    *cols = 80;
    return 0;
}

void refresh_screen(const editor_state *s) {
    if (s->file_row_count == 0) {
        return;
    }

    if (scenario == READ_ONLY) {
        saw_read_only_mode |= s->read_only;
        saw_read_only_notice |=
            s->status_message != NULL && strstr(s->status_message, "Read-only") != NULL;
        saw_read_only_change |= s->dirty || s->file_row_count != 2 ||
                                strcmp(s->file_rows[0].chars, "original") != 0 ||
                                strcmp(s->file_rows[1].chars, "second") != 0;
        saw_read_only_navigation |= s->cursor_y == 1;
        return;
    }

    if (saves > 0 && reads == 2) {
        saw_dirty_after_failed_save = s->dirty;
        saw_error_status_after_failed_save =
            s->status_message != NULL && strstr(s->status_message, "Save failed") != NULL;
    }

    if (scenario == NAMED_SAVE_RETRY && saves == 2 && reads == 3) {
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

    if (s->status_message != NULL && strstr(s->status_message, "Saved") != NULL) {
        saw_saved_status = 1;
    }
    if (successful_save && timed_reads >= 20 && s->status_message == NULL) {
        saw_status_expired = 1;
    }
    if (scenario == NAMED_SAVE_THEN_EDIT && s->dirty && s->status_message == NULL &&
        strcmp(s->file_rows[0].chars, "xyoriginal") == 0) {
        saw_status_cleared_on_edit = 1;
    }
    if (scenario == UNNAMED_SAVE_SUCCESS && successful_save && s->filename != NULL &&
        strcmp(s->filename, "new-document") == 0 && !s->dirty) {
        saw_unnamed_clean_after_save = 1;
    }
    if (scenario == UNNAMED_SAVE_FAILURE && saves == 1 && s->filename == NULL && s->dirty) {
        saw_unnamed_after_failed_save = 1;
    }
}

int read_key(void) {
    static const int failure_keys[] = {'x', CTRL_KEY('s'), 'y', CTRL_KEY('q'), CTRL_KEY('q')};
    static const int retry_keys[] = {'x', CTRL_KEY('s'), CTRL_KEY('s'), CTRL_KEY('q')};
    static const int edit_after_save_keys[] = {'x', CTRL_KEY('s'), 'y', CTRL_KEY('q'),
                                               CTRL_KEY('q')};
    static const int unnamed_success_keys[] = {'x', CTRL_KEY('s'), CTRL_KEY('q')};
    static const int unnamed_failure_keys[] = {'x', CTRL_KEY('s'), CTRL_KEY('q'), CTRL_KEY('q')};
    static const int read_only_keys[] = {'x',           '\n',       127,           CTRL_KEY('h'),
                                         CTRL_KEY('s'), ARROW_DOWN, CTRL_KEY('f'), CTRL_KEY('q')};
    const int *keys = failure_keys;
    size_t key_count = sizeof(failure_keys) / sizeof(failure_keys[0]);
    if (scenario == NAMED_SAVE_RETRY) {
        keys = retry_keys;
        key_count = sizeof(retry_keys) / sizeof(retry_keys[0]);
    } else if (scenario == NAMED_SAVE_THEN_EDIT) {
        keys = edit_after_save_keys;
        key_count = sizeof(edit_after_save_keys) / sizeof(edit_after_save_keys[0]);
    } else if (scenario == UNNAMED_SAVE_SUCCESS) {
        keys = unnamed_success_keys;
        key_count = sizeof(unnamed_success_keys) / sizeof(unnamed_success_keys[0]);
    } else if (scenario == UNNAMED_SAVE_CANCEL || scenario == UNNAMED_SAVE_FAILURE) {
        keys = unnamed_failure_keys;
        key_count = sizeof(unnamed_failure_keys) / sizeof(unnamed_failure_keys[0]);
    } else if (scenario == READ_ONLY) {
        keys = read_only_keys;
        key_count = sizeof(read_only_keys) / sizeof(read_only_keys[0]);
    }
    if ((size_t)reads >= key_count) {
        return -1;
    }
    return keys[reads++];
}

int read_key_with_timeout(void) {
    if (scenario == NAMED_SAVE_THEN_EDIT) {
        return read_key();
    }
    if (timed_reads < 20) {
        timed_reads++;
        return KEY_TIMEOUT;
    }
    return read_key();
}

int search_prompt(editor_state *s) {
    (void)s;
    searches++;
    return 0;
}

int save_as_prompt(editor_state *s, char **filename_out) {
    prompts++;
    saw_prompt_while_unnamed = s->filename == NULL;
    *filename_out = NULL;
    if (scenario == UNNAMED_SAVE_CANCEL) {
        return 0;
    }

    const char path[] = "new-document";
    *filename_out = malloc(sizeof(path));
    if (*filename_out == NULL) {
        return -1;
    }
    memcpy(*filename_out, path, sizeof(path));
    return 1;
}

int main(void) {
    char *named_argv[] = {"gg", "document", NULL};
    char *unnamed_argv[] = {"gg", NULL};
    char *read_only_argv[] = {"gg", "-R", "document", NULL};
    char *read_only_stdin_argv[] = {"gg", "-R", "-", NULL};
    char *missing_read_only_filename_argv[] = {"gg", "-R", NULL};

    reset_scenario(NAMED_SAVE_FAILURE);
    int result = editor_program_main(2, named_argv);

    check(saves == 1, "Ctrl-S attempts one save");
    check(reads == 5, "a failed save does not exit the editor");
    check(saw_dirty_after_failed_save, "a failed save leaves the buffer modified");
    check(saw_error_status_after_failed_save, "a failed save shows a status message");
    check(saw_edit_after_failed_save, "editing remains possible after a failed save");
    check(saw_unsaved_quit_prompt, "quitting after a failed save still requires confirmation");
    check(result == EXIT_SUCCESS, "confirmed quit exits normally after the failed save");
    check(prompts == 0, "saving a named file does not ask for a filename");

    reset_scenario(NAMED_SAVE_RETRY);
    result = editor_program_main(2, named_argv);

    check(saves == 2, "Ctrl-S retries saving after the first failure");
    check(reads == 4, "a successful retry allows quitting with one Ctrl-Q");
    check(saw_dirty_after_failed_save, "buffer stays modified until the retry succeeds");
    check(saw_error_status_after_failed_save, "the first failed attempt shows an error");
    check(saw_clean_after_retry, "successful retry clears the modified flag");
    check(saw_error_cleared_after_retry, "successful retry removes the stale save error");
    check(saw_saved_status, "successful save displays confirmation");
    check(saw_status_expired, "save confirmation disappears without another keypress");
    check(timed_reads == 20, "save confirmation has a bounded display time");
    check(!saw_unsaved_quit_prompt, "successful retry needs no unsaved-changes warning");
    check(result == EXIT_SUCCESS, "quit succeeds after a successful retry");

    reset_scenario(NAMED_SAVE_THEN_EDIT);
    result = editor_program_main(2, named_argv);
    check(saw_saved_status, "successful save displays confirmation before further editing");
    check(saw_status_cleared_on_edit,
          "next edit immediately dismisses stale save confirmation and marks buffer dirty");
    check(saw_unsaved_quit_prompt && result == EXIT_SUCCESS,
          "editing after a save requires confirmation before quitting");

    reset_scenario(UNNAMED_SAVE_SUCCESS);
    result = editor_program_main(1, unnamed_argv);
    check(prompts == 1 && saw_prompt_while_unnamed,
          "Ctrl-S prompts for a filename in an unnamed buffer");
    check(saves == 1 && saw_unnamed_save_target, "unnamed buffer saves to the entered filename");
    check(saw_unnamed_clean_after_save, "successful unnamed save names and cleans the buffer");
    check(saw_saved_status && saw_status_expired, "unnamed save shows a temporary confirmation");
    check(!saw_unsaved_quit_prompt && result == EXIT_SUCCESS,
          "saved unnamed buffer quits without an unsaved-changes warning");

    reset_scenario(UNNAMED_SAVE_CANCEL);
    result = editor_program_main(1, unnamed_argv);
    check(prompts == 1 && saves == 0, "cancelled Save As does not write a file");
    check(saw_unsaved_quit_prompt && result == EXIT_SUCCESS,
          "cancelled Save As leaves the buffer unsaved");

    reset_scenario(UNNAMED_SAVE_FAILURE);
    result = editor_program_main(1, unnamed_argv);
    check(prompts == 1 && saves == 1 && saw_unnamed_save_target,
          "failed Save As attempts the chosen filename");
    check(saw_unnamed_after_failed_save && saw_unsaved_quit_prompt,
          "failed Save As keeps the unnamed buffer dirty");
    check(result == EXIT_SUCCESS, "confirmed quit works after failed Save As");

    reset_scenario(READ_ONLY);
    result = editor_program_main(3, read_only_argv);
    check(result == EXIT_SUCCESS && reads == 8, "read-only mode accepts -R and exits normally");
    check(saw_read_only_mode, "read-only flag is available to the display");
    check(saw_read_only_notice, "blocked edits display a read-only message");
    check(!saw_read_only_change, "typing, Enter, Backspace, and Ctrl-H cannot edit");
    check(saves == 0 && prompts == 0, "Ctrl-S cannot save in read-only mode");
    check(saw_read_only_navigation && searches == 1,
          "navigation and search remain available in read-only mode");
    check(!saw_unsaved_quit_prompt, "read-only mode quits without an unsaved warning");
    check(tty_selections == 0, "viewing a regular file uses normal keyboard input");

    reset_scenario(READ_ONLY);
    result = editor_program_main(3, read_only_stdin_argv);
    check(result == EXIT_SUCCESS && saw_stdin_filename, "-R - accepts standard input");
    check(tty_selections == 1 && raw_mode_calls == 1,
          "piped viewer switches keyboard input to the controlling terminal");
    check(!saw_read_only_change && saves == 0 && prompts == 0, "piped viewer remains read-only");

    reset_scenario(READ_ONLY);
    fail_tty_selection = 1;
    result = editor_program_main(3, read_only_stdin_argv);
    check(result == EXIT_FAILURE && tty_selections == 1 && raw_mode_calls == 0 && reads == 0,
          "piped viewer exits before raw mode if no terminal is available");

    reset_scenario(READ_ONLY);
    result = editor_program_main(2, missing_read_only_filename_argv);
    check(result == EXIT_FAILURE && reads == 0, "-R requires a filename");

    if (test_failure_count() != 0) {
        fprintf(stderr, "%d main-loop test(s) failed\n", test_failure_count());
        return EXIT_FAILURE;
    }

    puts("main-loop tests passed");
    return EXIT_SUCCESS;
}
