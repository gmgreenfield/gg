#ifndef GG_TERMINAL_H
#define GG_TERMINAL_H

#include "editor.h"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME,
    END,
    PAGE_UP,
    PAGE_DOWN,
    KEY_RESIZE,
    KEY_TIMEOUT
};

void handle_resize(int signal_number);
int take_resize_pending(void);
int enable_raw_mode(void);
int get_window_size(int *rows, int *cols);
int read_key(void);
int read_key_with_timeout(void);
void refresh_screen(const editor_state *s);
int search_prompt(editor_state *s);
/* Returns 1 with an allocated path, 0 if cancelled, or -1 on error. */
int save_as_prompt(editor_state *s, char **filename_out);

#endif
