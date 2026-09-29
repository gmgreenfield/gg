SHELL := /bin/sh

CFLAGS := -std=c17 -Wall -Wextra -Wpedantic
DEBUG_CFLAGS := ${CFLAGS} -g
CC := gcc
CLANG_FORMAT := clang-format
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

gg: src/editor.c
	${CC} ${CFLAGS} src/editor.c -o gg

.PHONY: debug clean format format-check test install
debug: src/editor.c
	${CC} ${DEBUG_CFLAGS} src/editor.c -o gg-debug

clean:
	rm -f gg gg-debug build/editor_tests

format:
	${CLANG_FORMAT} -i src/editor.c tests/test_main.c

format-check:
	${CLANG_FORMAT} --dry-run --Werror src/editor.c tests/test_main.c

test: build/editor_tests
	./build/editor_tests

install: gg
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 gg "$(DESTDIR)$(BINDIR)/gg"

build:
	mkdir -p build

build/editor_tests: tests/test_main.c src/editor.c | build
	${CC} ${CFLAGS} tests/test_main.c -o build/editor_tests
