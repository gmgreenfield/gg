#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "file_io.h"
#include "safe_output.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int load_file(editor_state *s) {
    if (s->filename == NULL) {
        return 0;
    }

    FILE *stream;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    int from_stdin = s->read_only && strcmp(s->filename, "-") == 0;

    stream = from_stdin ? stdin : fopen(s->filename, "r");
    if (stream == NULL) {
        if (errno == ENOENT && !s->read_only) {
            return 0;
        } else {
            int saved_errno = errno;
            write_safe_terminal_text(stderr, s->filename, strlen(s->filename));
            fprintf(stderr, ": %s\n", strerror(saved_errno));
            errno = saved_errno;
            return -1;
        }
    }

    while ((nread = getline(&line, &len, stream)) != -1) {
        s->final_newline = nread > 0 && line[nread - 1] == '\n';

        while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
            nread--;
        }

        if (append_row(s, line, (size_t)nread) == -1) {
            fprintf(stderr, "Failed to append row.\n");
            free(line);
            if (!from_stdin && fclose(stream) == EOF) {
                perror("fclose");
            }
            return -1;
        }
    }

    if (ferror(stream)) {
        perror("getline");
        free(line);
        if (!from_stdin) {
            fclose(stream);
        }
        return -1;
    }

    free(line);
    if (!from_stdin && fclose(stream) == EOF) {
        perror("fclose");
        return -1;
    }

    return 0;
}

static int write_buffer_to_stream(FILE *stream, const editor_state *s) {
    for (size_t i = 0; i < s->file_row_count; i++) {
        const editor_row *row = &s->file_rows[i];

        errno = 0;
        if (fwrite(row->chars, sizeof(char), row->length, stream) != row->length) {
            if (errno == 0) {
                errno = EIO;
            }
            perror("fwrite");
            return -1;
        }

        errno = 0;
        if (i + 1 < s->file_row_count && fputc('\n', stream) == EOF) {
            if (errno == 0) {
                errno = EIO;
            }
            perror("fputc");
            return -1;
        }
    }

    errno = 0;
    if (s->final_newline && fputc('\n', stream) == EOF) {
        if (errno == 0) {
            errno = EIO;
        }
        perror("fputc");
        return -1;
    }

    return 0;
}

static int create_temp_file(const char *filename, char **temp_path) {
    static const char suffix[] = ".gg-tmp-XXXXXX";
    size_t filename_length = strlen(filename);

    if (filename_length > SIZE_MAX - sizeof(suffix)) {
        errno = ENAMETOOLONG;
        perror("save_file");
        return -1;
    }

    char *path = malloc(filename_length + sizeof(suffix));
    if (path == NULL) {
        perror("malloc");
        return -1;
    }

    memcpy(path, filename, filename_length);
    memcpy(path + filename_length, suffix, sizeof(suffix));

    int fd = mkstemp(path);
    if (fd == -1) {
        int saved_errno = errno;
        perror("mkstemp");
        free(path);
        errno = saved_errno;
        return -1;
    }

    *temp_path = path;
    return fd;
}

static int fail_save(FILE *stream, int temp_fd, char *temp_path) {
    int saved_errno = errno;

    if (stream != NULL) {
        fclose(stream);
    } else if (temp_fd != -1) {
        close(temp_fd);
    }

    if (temp_path != NULL) {
        unlink(temp_path);
    }

    free(temp_path);
    errno = saved_errno;
    return -1;
}

static int get_output_mode(const char *filename, mode_t *output_mode) {
    struct stat file_stat;

    if (lstat(filename, &file_stat) == 0) {
        if (!S_ISREG(file_stat.st_mode)) {
            errno = EINVAL;
            perror("save_file: not a regular file");
            return -1;
        }

        *output_mode = file_stat.st_mode & 0777;
        return 0;
    }

    if (errno != ENOENT) {
        int saved_errno = errno;
        write_safe_terminal_text(stderr, filename, strlen(filename));
        fprintf(stderr, ": %s\n", strerror(saved_errno));
        errno = saved_errno;
        return -1;
    }

    /* new file: apply the user's permission mask */
    mode_t mask = umask(0);
    umask(mask);
    *output_mode = (mode_t)(0666 & ~mask);
    return 0;
}

int save_file(const editor_state *s) {
    if (s == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (s->filename == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (s->read_only) {
        errno = EROFS;
        return -1;
    }

    mode_t output_mode;
    if (get_output_mode(s->filename, &output_mode) == -1) {
        return -1;
    }

    char *temp_path = NULL;
    int temp_fd = create_temp_file(s->filename, &temp_path);
    if (temp_fd == -1) {
        return -1;
    }

    FILE *stream = fdopen(temp_fd, "w");
    if (stream == NULL) {
        perror("fdopen");
        return fail_save(NULL, temp_fd, temp_path);
    }

    /* fdopen owns temp_fd now. */
    temp_fd = -1;

    if (write_buffer_to_stream(stream, s) == -1) {
        return fail_save(stream, -1, temp_path);
    }

    if (fchmod(fileno(stream), output_mode) == -1) {
        perror("fchmod");
        return fail_save(stream, -1, temp_path);
    }

    errno = 0;
    if (fflush(stream) == EOF) {
        if (errno == 0) {
            errno = EIO;
        }
        perror("fflush");
        return fail_save(stream, -1, temp_path);
    }

    errno = 0;
    if (fclose(stream) == EOF) {
        if (errno == 0) {
            errno = EIO;
        }
        perror("fclose");
        /* fclose consumes the stream even when it reports an error. */
        return fail_save(NULL, -1, temp_path);
    }

    if (rename(temp_path, s->filename) == -1) {
        perror("rename");
        return fail_save(NULL, -1, temp_path);
    }

    free(temp_path);
    return 0;
}
