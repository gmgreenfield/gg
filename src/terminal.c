#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "terminal.h"
#include "safe_output.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t resize_pending;
static struct termios original;
static int input_fd = STDIN_FILENO;
static int tty_input_fd = -1;

int use_tty_input(void) {
    int fd = open("/dev/tty", O_RDWR);
    if (fd == -1) {
        if (errno == ENXIO || errno == ENOTTY) {
            fprintf(stderr, "Piped viewing requires a controlling terminal (/dev/tty).\n");
        } else {
            perror("/dev/tty");
        }
        return -1;
    }

    input_fd = fd;
    tty_input_fd = fd;
    return 0;
}

void handle_resize(int signal_number) {
    (void)signal_number;
    resize_pending = 1;
}

int take_resize_pending(void) {
    if (!resize_pending) {
        return 0;
    }
    resize_pending = 0;
    return 1;
}

static void draw_rows(const editor_state *s) {
    for (int i = 0; i < s->screen_rows - 1; i++) {
        size_t file_row = s->row_offset + (size_t)i;
        if (file_row < s->file_row_count) {
            const editor_row *row = &s->file_rows[file_row];
            size_t start = s->col_offset;
            size_t length = 0;

            if (start < row->length) {
                length = row->length - start;
                if (length > (size_t)s->screen_cols) {
                    length = s->screen_cols;
                }
            }

            if (length > 0) {
                write_safe_terminal_text(stdout, row->chars + start, length);
            }
        } else {
            putchar('~');
        }

        if ((int)i < s->screen_rows - 1) {
            printf("\r\n");
        }
    }
}

static void draw_status_bar(const editor_state *s) {
    printf("\x1b[7m");

    char status[256];
    int status_length = 0;

    if (s->status_message != NULL) {
        status_length = snprintf(status, sizeof(status), "%s", s->status_message);
    } else {
        status_length = snprintf(status, sizeof(status), "%s%s | %zu lines | %d:%d",
                                 s->read_only ? "[read-only] " : "",
                                 s->filename != NULL ? s->filename : "[No Name]", s->file_row_count,
                                 s->cursor_y + 1, s->cursor_x + 1);
    }

    if (status_length < 0) {
        status_length = 0;
    }

    if (status_length > (int)sizeof(status) - 1) {
        status_length = (int)sizeof(status) - 1;
    }

    if (status_length > s->screen_cols) {
        status_length = s->screen_cols;
    }

    write_safe_terminal_text(stdout, status, (size_t)status_length);

    for (int i = status_length; i < s->screen_cols; i++) {
        putchar(' ');
    }

    printf("\x1b[m");
}

void refresh_screen(const editor_state *s) {
    printf("\x1b[?25l\x1b[2J\x1b[H");
    draw_rows(s);
    draw_status_bar(s);
    printf("\x1b[%d;%dH", s->cursor_y - (int)s->row_offset + 1,
           s->cursor_x - (int)s->col_offset + 1);
    printf("\x1b[?25h");
    fflush(stdout);
}

int get_window_size(int *rows, int *cols) {
    struct winsize dims;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &dims) == -1) {
        return -1;
    }

    *rows = dims.ws_row;
    *cols = dims.ws_col;
    return 0;
}

static void restore_original(void) {
    printf("\x1b[2J\x1b[H\x1b[?25h");
    fflush(stdout);
    if (tcsetattr(input_fd, TCSAFLUSH, &original) == -1) {
        perror("tcsetattr");
    }
    if (tty_input_fd != -1) {
        close(tty_input_fd);
    }
}

int enable_raw_mode(void) {
    if (!tcgetattr(input_fd, &original)) {
        if (atexit(restore_original) != 0) {
            fprintf(stderr, "Failed to register terminal restoration.\n");
            return -1;
        }
    } else {
        perror("tcgetattr");
        return -1;
    }

    struct termios raw = original;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(input_fd, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

static ssize_t read_byte(unsigned char *byte) {
    while (1) {
        ssize_t bytes_read = read(input_fd, byte, 1);
        if (bytes_read == -1 && errno == EINTR) {
            continue;
        } else if (bytes_read == -1) {
            return -1;
        } else if (bytes_read == 0) {
            return 0;
        } else if (bytes_read == 1) {
            return 1;
        }
    }
}

static int read_key_internal(int return_on_timeout) {
    unsigned char key;

    while (1) {
        if (resize_pending) {
            return KEY_RESIZE;
        }

        ssize_t bytes_read = read_byte(&key);
        if (bytes_read == -1) {
            perror("read");
            return -1;
        } else if (bytes_read == 0) {
            if (return_on_timeout) {
                return KEY_TIMEOUT;
            }
            continue;
        } else if (bytes_read == 1) {
            if (key == '\x1b') {
                unsigned char seq[3];

                ssize_t s0 = read_byte(&seq[0]);
                if (s0 == -1) {
                    perror("read");
                    return -1;
                }
                if (s0 == 0) {
                    return '\x1b';
                }

                ssize_t s1 = read_byte(&seq[1]);
                if (s1 == -1) {
                    perror("read");
                    return -1;
                }
                if (s1 == 0) {
                    return '\x1b';
                }

                if (seq[0] == '[') {
                    switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME;
                    case 'F':
                        return END;
                    case '5':
                        if (read_byte(&seq[2]) == 1 && seq[2] == '~') {
                            return PAGE_UP;
                        }
                        return '\x1b';
                    case '6':
                        if (read_byte(&seq[2]) == 1 && seq[2] == '~') {
                            return PAGE_DOWN;
                        }
                        return '\x1b';
                    default:
                        return '\x1b';
                    }
                }
            }
            return key;
        }
    }
}

int read_key(void) { return read_key_internal(0); }

int read_key_with_timeout(void) { return read_key_internal(1); }

int search_prompt(editor_state *s) {
    char query[256] = {0};
    char status[300];
    size_t length = 0;

    while (1) {
        snprintf(status, sizeof(status), "Search: %s", query);
        s->status_message = status;

        scroll_cursor(s);
        refresh_screen(s);

        int key = read_key();

        if (key == '\x1b') {
            s->status_message = NULL;
            return 0;
        }

        if (key == '\r' || key == '\n') {
            int match_row;
            int match_col;

            if (find_next_match(s, query, s->cursor_y, s->cursor_x, &match_row, &match_col) == 0) {
                s->cursor_y = match_row;
                s->cursor_x = match_col;
            } else {
                s->status_message = "Not found";
                refresh_screen(s);
                read_key();
            }

            s->status_message = NULL;
            return 0;
        }

        if (key == 127 || key == CTRL_KEY('h')) {
            if (length > 0) {
                query[--length] = '\0';
            }
            continue;
        }

        if (key >= 32 && key <= 126 && length < sizeof(query) - 1) {
            query[length++] = (char)key;
            query[length] = '\0';
        }

        if (key == -1) {
            s->status_message = NULL;
            return -1;
        }
    }
}

int save_as_prompt(editor_state *s, char **filename_out) {
    if (s == NULL || filename_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    *filename_out = NULL;
    size_t capacity = 64;
    size_t length = 0;
    char *filename = malloc(capacity);
    if (filename == NULL) {
        return -1;
    }
    filename[0] = '\0';

    char status[512];
    for (;;) {
        snprintf(status, sizeof(status), "Save as: %s (Enter to save, Esc to cancel)", filename);
        s->status_message = status;
        scroll_cursor(s);
        refresh_screen(s);

        int key = read_key();
        if (key == -1) {
            break;
        }
        if (key == KEY_RESIZE) {
            take_resize_pending();
            if (get_window_size(&s->screen_rows, &s->screen_cols) == -1) {
                perror("ioctl");
                break;
            }
            if (s->screen_rows < 2) {
                s->screen_rows = 2;
            }
            if (s->screen_cols < 1) {
                s->screen_cols = 1;
            }
            continue;
        }
        if (key == '\x1b' || ((key == '\r' || key == '\n') && length == 0)) {
            s->status_message = NULL;
            free(filename);
            return 0;
        }
        if (key == '\r' || key == '\n') {
            struct stat file_info;
            if (lstat(filename, &file_info) == 0) {
                snprintf(status, sizeof(status), "Overwrite existing file? (y/N): %s", filename);
                s->status_message = status;
                refresh_screen(s);
                int answer = read_key();
                if (answer == -1) {
                    break;
                }
                if (answer == '\x1b') {
                    s->status_message = NULL;
                    free(filename);
                    return 0;
                }
                if (answer != 'y' && answer != 'Y') {
                    continue;
                }
            }
            s->status_message = NULL;
            *filename_out = filename;
            return 1;
        }
        if (key == 127 || key == CTRL_KEY('h')) {
            if (length > 0) {
                filename[--length] = '\0';
            }
            continue;
        }
        if (key >= 32 && key <= 126) {
            if (length == capacity - 1) {
                size_t new_capacity = capacity * 2;
                if (new_capacity <= capacity) {
                    errno = ENOMEM;
                    break;
                }
                char *new_filename = realloc(filename, new_capacity);
                if (new_filename == NULL) {
                    break;
                }
                filename = new_filename;
                capacity = new_capacity;
            }
            filename[length++] = (char)key;
            filename[length] = '\0';
        }
    }

    s->status_message = NULL;
    free(filename);
    return -1;
}
