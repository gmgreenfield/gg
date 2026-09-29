#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "editor.h"
#include "file_io.h"
#include "test_helpers.h"

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

static void test_unnamed_save_is_not_reported_as_success(void) {
    editor_state state = {.dirty = 1};
    check(append_row(&state, "unsaved", 7) == 0, "prepare unnamed buffer");

    errno = 0;
    check(save_file(&state) == -1 && errno == EINVAL,
          "saving without a filename reports an error instead of false success");
    check(state.dirty == 1, "failed unnamed save leaves the buffer dirty");

    free_rows(&state);
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

void test_file_io(void) {
    test_load_save();
    test_unnamed_save_is_not_reported_as_success();
    test_failed_save_preserves_original();
    test_save_fault(SAVE_FAULT_WRITE);
    test_save_fault(SAVE_FAULT_RENAME);
}
