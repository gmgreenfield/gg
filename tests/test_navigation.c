#include "editor.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

static void test_scrolling(void) {
    editor_state state = {
        .cursor_y = 9,
        .cursor_x = 19,
        .screen_rows = 5,
        .screen_cols = 10,
    };

    scroll_cursor(&state);
    check(state.row_offset == 6, "vertical scroll follows cursor");
    check(state.col_offset == 10, "horizontal scroll follows cursor");

    state.cursor_y = 2;
    state.cursor_x = 4;
    scroll_cursor(&state);
    check(state.row_offset == 2, "vertical scroll follows cursor upward");
    check(state.col_offset == 4, "horizontal scroll follows cursor left");
}

static void test_home_end_navigation(void) {
    editor_state state = {0};

    check(append_row(&state, "hello", 5) == 0, "append Home/End test row");
    state.cursor_x = 3;

    move_cursor_home(&state);
    check(state.cursor_x == 0, "Home moves to the beginning of the line");

    move_cursor_end(&state);
    check(state.cursor_x == 5, "End moves to the end of the line");

    free_rows(&state);
}

static void test_page_navigation(void) {
    editor_state state = {
        .cursor_y = 0,
        .cursor_x = 4,
        .screen_rows = 4,
    };

    check(append_row(&state, "first", 5) == 0, "append first page row");
    check(append_row(&state, "hello", 5) == 0, "append second page row");
    check(append_row(&state, "two", 3) == 0, "append third page row");
    check(append_row(&state, "three", 5) == 0, "append fourth page row");
    check(append_row(&state, "end", 3) == 0, "append fifth page row");

    move_cursor_page_down(&state);
    check(state.cursor_y == 3, "Page Down moves by the text viewport height");
    check(state.cursor_x == 4, "Page Down preserves the requested column when possible");

    move_cursor_page_up(&state);
    check(state.cursor_y == 0, "Page Up moves by the text viewport height");
    check(state.cursor_x == 4, "Page Up restores the requested column when possible");

    state.cursor_y = 1;
    state.cursor_x = 4;
    move_cursor_page_down(&state);
    check(state.cursor_y == 4, "Page Down moves to the final row");
    check(state.cursor_x == 3, "Page Down clamps to a shorter destination row");

    move_cursor_page_down(&state);
    check(state.cursor_y == 4, "Page Down clamps at the last row");

    free_rows(&state);
}

static void test_page_viewport(void) {
    editor_state state = {.screen_rows = 8, .screen_cols = 80};
    for (int i = 0; i < 21; i++) {
        int result = append_row(&state, "example", 7);
        check(result == 0, "create paging viewport fixture");
        if (result != 0) {
            free_rows(&state);
            return;
        }
    }

    /* Seven text rows; move the view with the cursor, including partial
       movement at file boundaries. Each case starts independently. */
    const struct {
        const char *name;
        int down;
        int cursor;
        size_t offset;
        int expected_cursor;
        size_t expected_offset;
    } cases[] = {
        {"Page Down from screen top", 1, 0, 0, 7, 7},
        {"Page Down from screen middle", 1, 3, 0, 10, 7},
        {"Page Down from screen bottom", 1, 6, 0, 13, 7},
        {"Page Up from screen top", 0, 7, 7, 0, 0},
        {"Page Up from screen middle", 0, 10, 7, 3, 0},
        {"Page Up from screen bottom", 0, 13, 7, 6, 0},
        {"Page Down near end of file", 1, 17, 14, 20, 17},
        {"Page Down at end of file", 1, 20, 17, 20, 17},
        {"Page Up near start of file", 0, 6, 3, 0, 0},
        {"Page Up at start of file", 0, 0, 0, 0, 0},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        state.cursor_y = cases[i].cursor;
        state.cursor_x = 2;
        state.row_offset = cases[i].offset;
        state.col_offset = 0;

        if (cases[i].down)
            move_cursor_page_down(&state);
        else
            move_cursor_page_up(&state);

        /* Include the same visibility adjustment used before each redraw. */
        scroll_cursor(&state);

        char message[160];
        snprintf(message, sizeof(message), "%s: cursor position", cases[i].name);
        check(state.cursor_y == cases[i].expected_cursor, message);
        snprintf(message, sizeof(message), "%s: viewport offset", cases[i].name);
        check(state.row_offset == cases[i].expected_offset, message);
        check(state.cursor_x == 2, "paging preserves a valid column");
        check(state.dirty == 0, "paging does not mark the document modified");
    }

    free_rows(&state);
}

void test_navigation(void) {
    test_scrolling();
    test_home_end_navigation();
    test_page_navigation();
    test_page_viewport();
}
