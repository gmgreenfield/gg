#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <errno.h>

#define LINE_CAPACITY   256
#define CTRL_KEY(k)     ((k) & 0x1f)

struct termios original;

enum editor_key {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

typedef struct {
    char *chars;
    size_t length;
} editor_row;

typedef struct {
    int cursor_x;
    int cursor_y;
    int screen_rows;
    int screen_cols;
    int line_length;
    char line[LINE_CAPACITY];
    const char *filename;
    editor_row *file_rows;
    size_t file_row_count;
    size_t file_row_capacity;
} editor_state;

void draw_rows(const editor_state *s) {
    for (int i=0; i < s->screen_rows; i++) {
        if((size_t)i < s->file_row_count) {
            size_t row_length = s->file_rows[i].length;
            if(s->file_rows[i].length > (size_t)s->screen_cols) {
                row_length = (size_t)s->screen_cols;
            }
            for (size_t j=0; j < row_length; j++) {
                putchar(s->file_rows[i].chars[j]);
            }
        } else {
            putchar('~');
        }

        if((int)i < s->screen_rows - 1) {
            printf("\r\n");
        }
    }
}

void refresh_screen(const editor_state *s) {
    printf("\x1b[?25l\x1b[2J\x1b[H");
    draw_rows(s);
    printf("\x1b[%d;%dH", s->cursor_y+1, s->cursor_x+1);
    printf("\x1b[?25h");
    fflush(stdout);
}

int get_window_size(int *rows, int *cols) {
    struct winsize dims;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &dims) == -1) {
        return -1;
    }

    *rows = dims.ws_row;
    *cols = dims.ws_col;
    return 0;
}

void restore_original(void) {
    printf("\x1b[2J\x1b[H\x1b[?25h");
    fflush(stdout);
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1) {
        perror("tcsetattr");
    }
}

int enable_raw_mode(void) {
    if(!tcgetattr(STDIN_FILENO, &original)) {
        if(atexit(restore_original) != 0) {
            fprintf(stderr, "failed to register terminal restoration\n");
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

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

int read_key(void) {
    unsigned char key;

    while (1) {
        ssize_t bytes_read = read(STDIN_FILENO, &key, 1);
        if(bytes_read == -1) {
            perror("read");
            return -1;
        } else if (bytes_read == 0) {
            continue;
        } else if (bytes_read == 1) {
            if(key == '\x1b') {
                unsigned char seq[2];

                ssize_t s0 = read(STDIN_FILENO, &seq[0], 1);
                if(s0 == -1) {
                    perror("read");
                    return -1;
                }
                if(s0 == 0) {
                    return '\x1b';
                }
                ssize_t s1 = read(STDIN_FILENO, &seq[1], 1);
                if(s1 == -1) {
                    perror("read");
                    return -1;
                }
                if(s1 == 0) {
                    return '\x1b';
                }

                if(seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A':
                            return ARROW_UP;
                        case 'B':
                            return ARROW_DOWN;
                        case 'C':
                            return ARROW_RIGHT;
                        case 'D':
                            return ARROW_LEFT;
                        default:
                            return '\x1b';
                    }
                }
            }
            return key;
        }
    }
}

int append_row(editor_state *state, const char *chars, size_t length) {
    char *copy = malloc(length + 1);
    if(copy == NULL) {
        return -1;
    }

    memcpy(copy, chars, length);
    copy[length] = '\0';

    if (state->file_row_count == state->file_row_capacity) {
        size_t new_capacity;
        if (state->file_row_capacity == 0) {
            new_capacity = 8;   
        } else {
            new_capacity = state->file_row_capacity * 2;
        }

        editor_row *new_rows = realloc(
            state->file_rows, new_capacity * sizeof(*new_rows));

        if(new_rows == NULL) {
            free(copy);
            return -1;
        }

        state->file_row_capacity = new_capacity;
        state->file_rows = new_rows;
    }

    editor_row *row = &state->file_rows[state->file_row_count];

    row->chars = copy;
    row->length = length;
    
    state->file_row_count++;

    return 0;
}

int load_file(editor_state *s) {
    if(s->filename == NULL) {
        return 0;
    }
    
    FILE *stream;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    stream = fopen(s->filename, "r");
    if(stream == NULL) {
        if(errno == ENOENT) {
            return 0;
        } else {
            perror(s->filename);
            return -1;
        }
    }

    while ((nread = getline(&line, &len, stream)) != -1) {
        while(nread > 0 &&
            (line[nread-1] == '\n' || line[nread-1] == '\r')) {
            nread--;
        }
        
        if(append_row(s, line, (size_t)nread) == -1) {
            fprintf(stderr, "failed to append row\n");
            free(line);
            if(fclose(stream) == EOF) {
                perror("fclose");
            }
            return -1;
        }
    }

    if(ferror(stream)) {
        perror("getline");
        free(line);
        fclose(stream);
        return -1;
    }

    free(line);
    if(fclose(stream) == EOF) {
        perror("fclose");
        return -1;
    }
    
    return 0;
}

int save_file(const editor_state *s) {
    if(s->filename == NULL)
        return 0;

    FILE *fd;
    fd = fopen(s->filename, "w");
    if(fd == NULL) {
        perror(s->filename);
        return -1;
    }

    size_t written = fwrite(s->line, 1, (size_t)s->line_length, fd);
    if(written != (size_t)s->line_length) {
        if(ferror(fd)) {
            perror("fwrite");
        } else {
            fprintf(stderr, "fwrite: short write\n");
        }

        fclose(fd);
        return -1;
    }
    

    if(fclose(fd) == EOF) {
        perror("fclose");
        return -1;
    }
    
    return 0;
}

int main(int argc, char **argv) {
    editor_state p = {0};
    int key;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [filename]\n", argv[0]);
        return 1;
    }

    if(argc == 2)
        p.filename = argv[1];

    if(load_file(&p) == -1)
        return 1;

    if(enable_raw_mode() == -1)
        return 1;

    if(get_window_size(&p.screen_rows, &p.screen_cols) == -1) {
        perror("ioctl");
        return 1;
    }

    while(1) {
        refresh_screen(&p);
        key = read_key();

        if(key == -1) 
            return 1;

        if(key == CTRL_KEY('q'))
            break;

        switch(key) {
            case ARROW_LEFT:
                if(p.cursor_x > 0)
                    p.cursor_x--;
                break;
            case ARROW_RIGHT:
                if(p.cursor_x < p.line_length)
                    p.cursor_x++;
                break;
            case ARROW_UP:
                break;
            case ARROW_DOWN:
                break;
            case CTRL_KEY('s'):
                if(save_file(&p) == -1) {
                    return 1;
                }
                break;
            case 127:
            case CTRL_KEY('h'):
                if(p.cursor_x > 0) { 
                    memmove(
                        &p.line[p.cursor_x - 1],
                        &p.line[p.cursor_x],
                        (size_t)(p.line_length - p.cursor_x)
                    );
                    p.cursor_x--;
                    p.line_length--;
                }
                break;
            default:
                if (key >=32 && key <= 126) {
                    if(p.line_length < LINE_CAPACITY &&
                        p.line_length < (p.screen_cols-1)) {
                            int tail_len = p.line_length - p.cursor_x;
    
                            memmove(
                                &p.line[p.cursor_x + 1],
                                &p.line[p.cursor_x],
                                (size_t)tail_len
                            );
    
                            p.line[p.cursor_x] = (char)key;
                            p.line_length++;
                            p.cursor_x++;
                    }
                }
            }
    }
    
    return 0;
}
