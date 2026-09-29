#include "editor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int find_next_match(const editor_state *s, const char *search_term, int start_row, int start_col,
                    int *match_row, int *match_col) {
    if (s == NULL || search_term == NULL || search_term[0] == '\0' || match_row == NULL ||
        match_col == NULL || start_row < 0 || start_col < 0 ||
        (size_t)start_row >= s->file_row_count ||
        (size_t)start_col > s->file_rows[start_row].length) {
        return -1;
    }

    for (int search_pass = 0; search_pass < 2; search_pass++) {
        size_t first_row;
        size_t last_row;

        if (search_pass == 0) {
            first_row = (size_t)start_row;
            last_row = s->file_row_count;
        } else {
            first_row = 0;
            last_row = (size_t)start_row + 1;
        }

        for (size_t row_index = first_row; row_index < last_row; row_index++) {
            size_t column = 0;

            if (search_pass == 0 && row_index == (size_t)start_row) {
                column = (size_t)start_col;
            }

            const editor_row *row = &s->file_rows[row_index];
            const char *match = strstr(row->chars + column, search_term);

            if (match != NULL) {
                *match_row = (int)row_index;
                *match_col = (int)(match - row->chars);
                return 0;
            }
        }
    }

    return -1;
}

void move_cursor_page_up(editor_state *s) {
    if (s == NULL || s->file_row_count == 0 || s->screen_rows < 2) {
        return;
    }

    size_t page_height = (size_t)(s->screen_rows - 1);
    size_t current_row = (size_t)s->cursor_y;
    size_t target_row;

    if (current_row > page_height) {
        target_row = current_row - page_height;
    } else {
        target_row = 0;
    }

    size_t moved = current_row - target_row;
    if (moved > s->row_offset) {
        s->row_offset = 0;
    } else {
        s->row_offset -= moved;
    }

    s->cursor_y = (int)target_row;

    if ((size_t)s->cursor_x > s->file_rows[target_row].length) {
        s->cursor_x = (int)s->file_rows[target_row].length;
    }
}

void move_cursor_page_down(editor_state *s) {
    if (s == NULL || s->file_row_count == 0 || s->screen_rows < 2) {
        return;
    }

    size_t page_height = (size_t)(s->screen_rows - 1);
    size_t last_row = s->file_row_count - 1;
    size_t current_row = (size_t)s->cursor_y;
    size_t target_row;

    if (page_height > last_row - current_row) {
        target_row = last_row;
    } else {
        target_row = current_row + page_height;
    }

    s->cursor_y = (int)target_row;
    s->row_offset += target_row - current_row;

    if ((size_t)s->cursor_x > s->file_rows[target_row].length) {
        s->cursor_x = (int)s->file_rows[target_row].length;
    }
}

void move_cursor_home(editor_state *s) { s->cursor_x = 0; }

void move_cursor_end(editor_state *s) {
    if (s->cursor_y >= 0 && (size_t)s->cursor_y < s->file_row_count) {
        s->cursor_x = (int)s->file_rows[s->cursor_y].length;
    }
}

void scroll_cursor(editor_state *s) {
    size_t text_rows = (size_t)(s->screen_rows - 1);
    size_t text_cols = (size_t)s->screen_cols;

    if ((size_t)s->cursor_y < s->row_offset) {
        s->row_offset = (size_t)s->cursor_y;
    } else if ((size_t)s->cursor_y >= s->row_offset + text_rows) {
        s->row_offset = (size_t)s->cursor_y - text_rows + 1;
    }

    if ((size_t)s->cursor_x < s->col_offset) {
        s->col_offset = (size_t)s->cursor_x;
    } else if ((size_t)s->cursor_x >= s->col_offset + text_cols) {
        s->col_offset = (size_t)s->cursor_x - text_cols + 1;
    }
}

int insert_row(editor_state *state, size_t index, const char *chars, size_t length) {
    if (index > state->file_row_count) {
        return -1;
    }

    if (length == SIZE_MAX) {
        return -1;
    }

    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }

    memcpy(copy, chars, length);
    copy[length] = '\0';

    if (state->file_row_count == state->file_row_capacity) {
        size_t new_capacity;
        if (state->file_row_capacity == 0) {
            new_capacity = 8;
        } else {
            if (state->file_row_capacity > SIZE_MAX / 2) {
                free(copy);
                return -1;
            }

            new_capacity = state->file_row_capacity * 2;
        }

        if (new_capacity > SIZE_MAX / sizeof(*state->file_rows)) {
            free(copy);
            return -1;
        }

        editor_row *new_rows = realloc(state->file_rows, new_capacity * sizeof(*new_rows));

        if (new_rows == NULL) {
            free(copy);
            return -1;
        }

        state->file_row_capacity = new_capacity;
        state->file_rows = new_rows;
    }

    size_t rows_to_move = state->file_row_count - index;

    memmove(&state->file_rows[index + 1], &state->file_rows[index],
            rows_to_move * sizeof(*state->file_rows));

    editor_row *row = &state->file_rows[index];

    row->chars = copy;
    row->length = length;

    state->file_row_count++;

    return 0;
}

int append_row(editor_state *state, const char *chars, size_t length) {
    return insert_row(state, state->file_row_count, chars, length);
}

int insert_char(editor_state *s, int key) {
    if (s->read_only) {
        return -1;
    }

    if (s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    char *new_chars = realloc(row->chars, row->length + 2);

    if (new_chars == NULL) {
        return -1;
    }

    row->chars = new_chars;

    size_t position = (size_t)s->cursor_x;

    memmove(&row->chars[position + 1], &row->chars[position], row->length - position + 1);

    row->chars[position] = (char)key;
    row->length++;
    s->cursor_x++;
    s->dirty = 1;

    return 0;
}

int delete_char(editor_state *s) {
    if (s->read_only) {
        return -1;
    }

    if (s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    if (s->cursor_x == 0) {
        if (s->cursor_y == 0) {
            return 0;
        }

        size_t row_index = (size_t)s->cursor_y;
        editor_row *previous = &s->file_rows[row_index - 1];
        size_t previous_length = previous->length;
        size_t current_length = row->length;

        if (previous_length > SIZE_MAX - current_length) {
            return -1;
        }

        size_t combined_length = previous_length + current_length;
        if (combined_length == SIZE_MAX) {
            return -1;
        }

        char *new_chars = realloc(previous->chars, combined_length + 1);
        if (new_chars == NULL) {
            return -1;
        }

        previous->chars = new_chars;
        memcpy(&previous->chars[previous_length], row->chars, current_length + 1);
        previous->length = combined_length;

        free(row->chars);
        memmove(row, &s->file_rows[row_index + 1],
                (s->file_row_count - row_index - 1) * sizeof(*s->file_rows));

        s->file_row_count--;
        s->cursor_y--;
        s->cursor_x = (int)previous_length;
        s->dirty = 1;

        return 0;
    }

    size_t position = (size_t)s->cursor_x;
    memmove(&row->chars[position - 1], &row->chars[position], row->length - position + 1);

    row->length--;
    s->cursor_x--;
    s->dirty = 1;

    return 0;
}

int insert_newline(editor_state *s) {
    if (s->read_only) {
        return -1;
    }

    if (s->cursor_y < 0 || (size_t)s->cursor_y >= s->file_row_count) {
        return -1;
    }

    editor_row *row = &s->file_rows[s->cursor_y];

    if (s->cursor_x < 0 || (size_t)s->cursor_x > row->length) {
        return -1;
    }

    size_t position = (size_t)s->cursor_x;
    size_t tail_length = row->length - position;

    if (insert_row(s, (size_t)s->cursor_y + 1, &row->chars[position], tail_length) == -1) {
        return -1;
    }

    row = &s->file_rows[s->cursor_y];

    row->length = position;
    row->chars[position] = '\0';

    s->cursor_y++;
    s->cursor_x = 0;
    s->dirty = 1;

    return 0;
}

void free_rows(editor_state *s) {
    for (size_t i = 0; i < s->file_row_count; i++) {
        free(s->file_rows[i].chars);
    }

    free(s->file_rows);

    s->file_rows = NULL;
    s->file_row_count = 0;
    s->file_row_capacity = 0;
}
