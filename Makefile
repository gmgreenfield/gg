SHELL := /bin/sh

CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -g
CC := gcc

editor: editor.c
	${CC} ${CFLAGS} editor.c -o gg

.PHONY: clean
clean:
	rm -f gg
