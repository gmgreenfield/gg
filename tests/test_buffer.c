#include "editor.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

static void test_editing_operations(void) {
    editor_state state = {0};

    check(append_row(&state, "abc", 3) == 0, "append initial row");

    state.cursor_x = 1;
    check(insert_char(&state, 'X') == 0, "insert character");
    check(strcmp(state.file_rows[0].chars, "aXbc") == 0, "inserted character content");
    check(state.cursor_x == 2, "cursor advances after insertion");

    check(delete_char(&state) == 0, "delete character");
    check(strcmp(state.file_rows[0].chars, "abc") == 0, "deleted character content");
    check(state.cursor_x == 1, "cursor retreats after deletion");

    check(insert_newline(&state) == 0, "split row at cursor");
    check(state.file_row_count == 2, "row count after split");
    check(strcmp(state.file_rows[0].chars, "a") == 0, "first row after split");
    check(strcmp(state.file_rows[1].chars, "bc") == 0, "second row after split");

    check(delete_char(&state) == 0, "join rows at column zero");
    check(state.file_row_count == 1, "row count after join");
    check(strcmp(state.file_rows[0].chars, "abc") == 0, "joined row content");

    free_rows(&state);
}

static void test_read_only_buffer_rejects_edits(void) {
    editor_state state = {.read_only = 1, .cursor_x = 1};
    check(append_row(&state, "abc", 3) == 0, "load a row into a read-only buffer");

    check(insert_char(&state, 'X') == -1, "read-only buffer rejects character insertion");
    check(delete_char(&state) == -1, "read-only buffer rejects deletion");
    check(insert_newline(&state) == -1, "read-only buffer rejects newlines");
    check(state.file_row_count == 1 && strcmp(state.file_rows[0].chars, "abc") == 0,
          "rejected edits leave read-only text unchanged");
    check(state.cursor_x == 1 && state.cursor_y == 0 && !state.dirty,
          "rejected edits leave cursor and dirty flag unchanged");

    free_rows(&state);
}

void test_buffer(void) {
    test_editing_operations();
    test_read_only_buffer_rejects_edits();
}
