#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

struct termios original;

void restore_original(void) {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original) == -1) {
        perror("failed to restore terminal");
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
        perror("tcgetattr error");
        return 1;
    }

    if (original.c_lflag & ECHO) {
        printf("echo is enabled\n");
    } else {
        printf("echo is disabled\n");
    }

    struct termios raw = original;
    raw.c_lflag &= ~(ECHO | ICANON);

    if (raw.c_lflag & ECHO) {
        printf("echo is enabled\n");
    } else {
        printf("echo is disabled\n");
    }

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr error");
        return 1;
    }

    unsigned char user;
    
    printf("press one key: ");
    fflush(stdout);
    
    ssize_t bytes_read = read(STDIN_FILENO, &user, 1);
    
    if(bytes_read == -1) {
        perror("read()");
    } else if (bytes_read == 0) {
        printf("\nend of input\n");
    } else {
        printf("\n%d\n", user);
    }
        
    printf("\n%d\n", user);
    
    return 0;
}