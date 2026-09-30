#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "editor.h"
#include "file_io.h"
#include "terminal.h"
#include "test_helpers.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    FILE *file;
    FILE *target;
    int saved_fd;
} output_capture;

static int begin_capture(output_capture *capture, FILE *target) {
    capture->file = tmpfile();
    if (capture->file == NULL) {
        return -1;
    }
    capture->target = target;
    int target_fd = fileno(target);
    fflush(target);
    capture->saved_fd = dup(target_fd);
    if (capture->saved_fd == -1) {
        fclose(capture->file);
        return -1;
    }
    if (dup2(fileno(capture->file), target_fd) == -1) {
        close(capture->saved_fd);
        fclose(capture->file);
        return -1;
    }
    return 0;
}

static void end_capture(output_capture *capture, char *output, size_t capacity) {
    fflush(capture->target);
    int target_fd = fileno(capture->target);
    int restored = dup2(capture->saved_fd, target_fd);
    close(capture->saved_fd);
    rewind(capture->file);
    size_t length = fread(output, 1, capacity - 1, capture->file);
    output[length] = '\0';
    fclose(capture->file);
    check(restored != -1, "restore output after capture");
}

static int capture_screen(const editor_state *state, char *output, size_t capacity) {
    output_capture capture;
    if (begin_capture(&capture, stdout) == -1) {
        return -1;
    }
    refresh_screen(state);
    end_capture(&capture, output, capacity);
    return 0;
}

static void test_file_content_is_safe_to_render(void) {
    char path[] = "/tmp/gg-terminal-content-XXXXXX";
    int fd = mkstemp(path);
    check(fd != -1, "create terminal-rendering file fixture");
    if (fd == -1) {
        return;
    }

    const char input[] = {'A',  '\x1b', '[',    '3', '1',  'm', 'B',        '\t', 'C',
                          '\r', 'D',    '\x7f', 'E', '\0', 'F', (char)0x9b, 'Z'};
    check(write(fd, input, sizeof(input)) == (ssize_t)sizeof(input),
          "write unsafe terminal-rendering file fixture");
    check(close(fd) == 0, "close terminal-rendering file fixture");

    editor_state state = {.filename = path, .screen_rows = 2, .screen_cols = 80};
    int loaded = load_file(&state);
    check(loaded == 0 && state.file_row_count == 1, "load file containing control bytes");
    if (loaded == 0 && state.file_row_count == 1) {
        char output[4096];
        int captured = capture_screen(&state, output, sizeof(output));
        check(captured == 0, "capture file rendering");
        if (captured == 0) {
            check(strstr(output, "A?[31mB?C?D?E?F?Z") != NULL,
                  "file control bytes render as visible placeholders");
            check(strstr(output, "\x1b[31m") == NULL,
                  "file escape sequence is not sent to terminal");
        }
        check(state.file_rows[0].length == sizeof(input) &&
                  memcmp(state.file_rows[0].chars, input, sizeof(input)) == 0,
              "rendering leaves original file bytes unchanged");
    }

    free_rows(&state);
    check(unlink(path) == 0, "remove terminal-rendering file fixture");
}

static void test_piped_content_is_safe_to_render(void) {
    int pipe_fds[2];
    int pipe_result = pipe(pipe_fds);
    check(pipe_result == 0, "create unsafe piped-input fixture");
    if (pipe_result == -1) {
        return;
    }

    const char input[] = {'P', '\x1b', ']', '5', '2',  ';',        'c', ';',
                          'e', 'A',    '=', '=', '\a', (char)0x9b, 'Q', '\n'};
    check(write(pipe_fds[1], input, sizeof(input)) == (ssize_t)sizeof(input),
          "write unsafe piped-input fixture");
    check(close(pipe_fds[1]) == 0, "close unsafe pipe writer");

    int saved_stdin = dup(STDIN_FILENO);
    check(saved_stdin != -1, "save stdin for piped-input test");
    if (saved_stdin == -1) {
        close(pipe_fds[0]);
        return;
    }
    check(dup2(pipe_fds[0], STDIN_FILENO) != -1, "select unsafe piped input");
    close(pipe_fds[0]);
    clearerr(stdin);

    editor_state state = {.filename = "-", .read_only = 1, .screen_rows = 2, .screen_cols = 80};
    int loaded = load_file(&state);
    check(dup2(saved_stdin, STDIN_FILENO) != -1, "restore stdin after piped-input test");
    close(saved_stdin);
    clearerr(stdin);

    check(loaded == 0 && state.file_row_count == 1, "load piped control bytes");
    if (loaded == 0 && state.file_row_count == 1) {
        char output[4096];
        int captured = capture_screen(&state, output, sizeof(output));
        check(captured == 0, "capture piped-input rendering");
        if (captured == 0) {
            check(strstr(output, "P?]52;c;eA==??Q") != NULL,
                  "piped control bytes render as visible placeholders");
            check(strstr(output, "\x1b]52;") == NULL,
                  "piped terminal command is not sent to terminal");
        }
        check(state.file_rows[0].length == sizeof(input) - 1 &&
                  memcmp(state.file_rows[0].chars, input, sizeof(input) - 1) == 0,
              "rendering leaves original piped bytes unchanged");
    }
    free_rows(&state);
}

static void test_filename_and_status_are_safe_to_render(void) {
    editor_state state = {.filename = "unsafe\x1b[31mname", .screen_rows = 2, .screen_cols = 80};
    check(append_row(&state, "text", 4) == 0, "prepare filename-rendering buffer");

    char output[4096];
    int captured = capture_screen(&state, output, sizeof(output));
    check(captured == 0, "capture filename rendering");
    if (captured == 0) {
        check(strstr(output, "unsafe?[31mname") != NULL,
              "filename control byte renders as a placeholder");
        check(strstr(output, "\x1b[31m") == NULL,
              "filename escape sequence is not sent to terminal");
    }

    state.status_message = "Saved: unsafe\x1b[31mname";
    captured = capture_screen(&state, output, sizeof(output));
    check(captured == 0, "capture save status rendering");
    if (captured == 0) {
        check(strstr(output, "Saved: unsafe?[31mname") != NULL,
              "save status control byte renders as a placeholder");
        check(strstr(output, "\x1b[31m") == NULL,
              "save status escape sequence is not sent to terminal");
    }
    free_rows(&state);
}

static void test_filename_in_error_is_safe(void) {
    char directory[] = "/tmp/gg-terminal-error-XXXXXX";
    char *created = mkdtemp(directory);
    check(created != NULL, "create filename-error directory");
    if (created == NULL) {
        return;
    }

    char path[sizeof(directory) + sizeof("/unsafe\x1b[31mname")];
    snprintf(path, sizeof(path), "%s/unsafe\x1b[31mname", directory);
    editor_state state = {.filename = path, .read_only = 1};
    char output[4096];
    output_capture capture;
    int started = begin_capture(&capture, stderr);
    check(started == 0, "capture filename error output");
    if (started == 0) {
        int loaded = load_file(&state);
        int load_errno = errno;
        end_capture(&capture, output, sizeof(output));
        check(loaded == -1 && load_errno == ENOENT, "missing file reports ENOENT");
        check(strstr(output, "unsafe?[31mname") != NULL,
              "error filename control byte renders as a placeholder");
        check(strstr(output, "\x1b[31m") == NULL,
              "error filename escape sequence is not sent to terminal");
    }
    free_rows(&state);
    check(rmdir(directory) == 0, "remove filename-error directory");
}

int main(void) {
    test_file_content_is_safe_to_render();
    test_piped_content_is_safe_to_render();
    test_filename_and_status_are_safe_to_render();
    test_filename_in_error_is_safe();

    int failures = test_failure_count();
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all terminal tests passed");
    return EXIT_SUCCESS;
}
