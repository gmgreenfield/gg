#ifndef GG_EDITOR_H
#define GG_EDITOR_H

#include <stddef.h>

typedef struct {
    char *chars;
    size_t length;
} editor_row;

typedef struct {
    int cursor_x;
    int cursor_y;
    int screen_rows;
    int screen_cols;
    int dirty;
    int final_newline;
    const char *filename;
    editor_row *file_rows;
    size_t file_row_count;
    size_t file_row_capacity;
    size_t row_offset;
    size_t col_offset;
    const char *status_message;
} editor_state;

int find_next_match(const editor_state *s, const char *search_term, int start_row, int start_col,
                    int *match_row, int *match_col);
void move_cursor_page_up(editor_state *s);
void move_cursor_page_down(editor_state *s);
void move_cursor_home(editor_state *s);
void move_cursor_end(editor_state *s);
void scroll_cursor(editor_state *s);
int insert_row(editor_state *state, size_t index, const char *chars, size_t length);
int append_row(editor_state *state, const char *chars, size_t length);
int insert_char(editor_state *s, int key);
int delete_char(editor_state *s);
int insert_newline(editor_state *s);
void free_rows(editor_state *s);

#endif
