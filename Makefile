SHELL := /bin/sh

CFLAGS := -std=c17 -Wall -Wextra -Wpedantic
DEBUG_CFLAGS := ${CFLAGS} -g
CC := gcc
CLANG_FORMAT := clang-format
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

SOURCES := src/main.c src/editor.c src/file_io.c src/terminal.c
HEADERS := $(wildcard src/*.h)
TEST_SOURCES := $(wildcard tests/*.c)
TEST_HEADERS := $(wildcard tests/*.h)
TEST_BINARY := build/editor_tests
TEST_FILE_IO_OBJECT := build/test_file_io.o
FORMAT_FILES := $(SOURCES) $(HEADERS) $(TEST_SOURCES) $(TEST_HEADERS)

gg: $(SOURCES) $(HEADERS)
	${CC} ${CPPFLAGS} ${CFLAGS} $(SOURCES) ${LDFLAGS} ${LDLIBS} -o gg

.PHONY: debug clean format format-check test install
debug: $(SOURCES) $(HEADERS)
	${CC} ${CPPFLAGS} ${DEBUG_CFLAGS} $(SOURCES) ${LDFLAGS} ${LDLIBS} -o gg-debug

clean:
	rm -f gg gg-debug $(TEST_BINARY) $(TEST_FILE_IO_OBJECT)

format:
	${CLANG_FORMAT} -i $(FORMAT_FILES)

format-check:
	${CLANG_FORMAT} --dry-run --Werror $(FORMAT_FILES)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

install: gg
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 gg "$(DESTDIR)$(BINDIR)/gg"

build:
	mkdir -p build

# Redirect file-I/O calls only in this object; fixtures still use real libc calls.
$(TEST_FILE_IO_OBJECT): src/file_io.c $(HEADERS) | build
	${CC} ${CPPFLAGS} ${CFLAGS} -Dfwrite=test_save_fwrite -Drename=test_save_rename -c src/file_io.c -o $@

$(TEST_BINARY): $(TEST_SOURCES) $(TEST_HEADERS) src/editor.c $(HEADERS) $(TEST_FILE_IO_OBJECT) | build
	${CC} ${CPPFLAGS} ${CFLAGS} -Isrc $(TEST_SOURCES) src/editor.c $(TEST_FILE_IO_OBJECT) ${LDFLAGS} ${LDLIBS} -o $@
