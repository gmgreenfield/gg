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

int main(void) {
    printf("here be dragons\n");
    
    if(!tcgetattr(STDIN_FILENO, &original)) {
        printf("success!\n");
        if(atexit(restore_original) != 0) {
            fprintf(stderr, "failed to register terminal restoration\n");
            return 1;
        }
    } else {
        perror("tcgetattr");
        return 1;
    }

    if (original.c_lflag & ECHO) {
        printf("echo is enabled\n");
    } else {
        printf("echo is disabled\n");
    }

    struct termios raw = original;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;

    if (raw.c_lflag & ECHO) {
        printf("echo is enabled\n");
    } else {
        printf("echo is disabled\n");
    }

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
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
            break;
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
