#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

struct termios original;

void restore_original(void) {
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
    if(enable_raw_mode() == -1) {
        return 1;
    }

    unsigned char user;
    printf("press keys; q quits: ");
    fflush(stdout);
    
    while(1) {
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
            printf("\r\n%d\r\n", user);
            continue;
        }
    }
    
    return 0;
}
