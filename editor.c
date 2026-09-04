#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>

struct termios original;

void draw_rows(int rows) {
    for (int i=0; i < rows; i++) {
        if(i == rows-1) {
            printf("~");
        } else {
            printf("~\r\n");
        }
    }
}

void refresh_screen(int rows) {
    printf("\x1b[?25l\x1b[2J\x1b[H");
    draw_rows(rows);
    printf("\x1b[H");
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
    printf("\x1b[?25h");
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

int main(void) {
    unsigned char user;
    int rows, cols;

    if(enable_raw_mode() == -1) {
        return 1;
    }

    if(get_window_size(&rows, &cols) == -1) {
        perror("ioctl");
        return 1;
    }
    
    while(1) {
        refresh_screen(rows);
        ssize_t bytes_read = read(STDIN_FILENO, &user, 1);
        if(bytes_read == -1) {
            perror("read");
            return 1;
        } else if (bytes_read == 0) {
            continue;
        } else if (bytes_read == 1) {
            if(user == 'q') {
                break;
            }
            continue;
        }
    }
    
    return 0;
}
