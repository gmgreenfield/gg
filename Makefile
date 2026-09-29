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
EDITOR_TEST_SOURCES := $(filter-out tests/test_main_loop.c,$(TEST_SOURCES))
TEST_HEADERS := $(wildcard tests/*.h)
TEST_BINARY := build/editor_tests
TEST_FILE_IO_OBJECT := build/test_file_io.o
MAIN_LOOP_BINARY := build/main_loop_tests
MAIN_LOOP_OBJECT := build/test_main_loop_main.o
FORMAT_FILES := $(SOURCES) $(HEADERS) $(TEST_SOURCES) $(TEST_HEADERS)

gg: $(SOURCES) $(HEADERS)
	${CC} ${CPPFLAGS} ${CFLAGS} $(SOURCES) ${LDFLAGS} ${LDLIBS} -o gg

.PHONY: debug clean format format-check test install
debug: $(SOURCES) $(HEADERS)
	${CC} ${CPPFLAGS} ${DEBUG_CFLAGS} $(SOURCES) ${LDFLAGS} ${LDLIBS} -o gg-debug

clean:
	rm -f gg gg-debug $(TEST_BINARY) $(TEST_FILE_IO_OBJECT) $(MAIN_LOOP_BINARY) $(MAIN_LOOP_OBJECT)

format:
	${CLANG_FORMAT} -i $(FORMAT_FILES)

format-check:
	${CLANG_FORMAT} --dry-run --Werror $(FORMAT_FILES)

test: $(TEST_BINARY) $(MAIN_LOOP_BINARY)
	./$(TEST_BINARY)
	./$(MAIN_LOOP_BINARY)

install: gg
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 gg "$(DESTDIR)$(BINDIR)/gg"

build:
	mkdir -p build

# Redirect file-I/O calls only in this object; fixtures still use real libc calls.
$(TEST_FILE_IO_OBJECT): src/file_io.c $(HEADERS) | build
	${CC} ${CPPFLAGS} ${CFLAGS} -Dfwrite=test_save_fwrite -Drename=test_save_rename -c src/file_io.c -o $@

$(TEST_BINARY): $(EDITOR_TEST_SOURCES) $(TEST_HEADERS) src/editor.c $(HEADERS) $(TEST_FILE_IO_OBJECT) | build
	${CC} ${CPPFLAGS} ${CFLAGS} -Isrc $(EDITOR_TEST_SOURCES) src/editor.c $(TEST_FILE_IO_OBJECT) ${LDFLAGS} ${LDLIBS} -o $@

# Give the editor's main function a test-specific name, then supply fake I/O.
$(MAIN_LOOP_OBJECT): src/main.c $(HEADERS) | build
	${CC} ${CPPFLAGS} ${CFLAGS} -Dmain=editor_program_main -c src/main.c -o $@

$(MAIN_LOOP_BINARY): tests/test_main_loop.c tests/test_helpers.c $(TEST_HEADERS) src/editor.c $(HEADERS) $(MAIN_LOOP_OBJECT) | build
	${CC} ${CPPFLAGS} ${CFLAGS} -Isrc tests/test_main_loop.c tests/test_helpers.c src/editor.c $(MAIN_LOOP_OBJECT) ${LDFLAGS} ${LDLIBS} -o $@
