#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t resize_pending;
static struct termios original;

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

            for (size_t j = 0; j < length; j++) {
                putchar(row->chars[start + j]);
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
        status_length = snprintf(status, sizeof(status), "%s | %zu lines | %d:%d",
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

    fwrite(status, 1, (size_t)status_length, stdout);

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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1) {
        perror("tcsetattr");
    }
}

int enable_raw_mode(void) {
    if (!tcgetattr(STDIN_FILENO, &original)) {
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

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

static ssize_t read_byte(unsigned char *byte) {
    while (1) {
        ssize_t bytes_read = read(STDIN_FILENO, byte, 1);
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

int read_key(void) {
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
