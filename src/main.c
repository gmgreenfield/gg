#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "editor.h"
#include "file_io.h"
#include "terminal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    editor_state p = {0};
    int key;
    int exit_status = EXIT_SUCCESS;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [filename]\n", argv[0]);
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (argc == 2)
        p.filename = argv[1];

    if (load_file(&p) == -1) {
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (p.file_row_count == 0) {
        if (append_row(&p, "", 0) == -1) {
            fprintf(stderr, "Failed to create initial row.\n");
            exit_status = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if (enable_raw_mode() == -1) {
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (signal(SIGWINCH, handle_resize) == SIG_ERR) {
        perror("signal");
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (get_window_size(&p.screen_rows, &p.screen_cols) == -1) {
        perror("ioctl");
        exit_status = EXIT_FAILURE;
        goto cleanup;
    }

    if (p.screen_rows < 2) {
        p.screen_rows = 2;
    }

    if (p.screen_cols < 1) {
        p.screen_cols = 1;
    }

    while (1) {
        if (take_resize_pending()) {

            if (get_window_size(&p.screen_rows, &p.screen_cols) == -1) {
                perror("ioctl");
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }

            if (p.screen_rows < 2) {
                p.screen_rows = 2;
            }

            if (p.screen_cols < 1) {
                p.screen_cols = 1;
            }
        }

        scroll_cursor(&p);
        refresh_screen(&p);
        key = read_key();

        if (key == KEY_RESIZE) {
            continue;
        }

        if (key == -1) {
            exit_status = EXIT_FAILURE;
            goto cleanup;
        }

        if (key == CTRL_KEY('q')) {
            if (p.dirty == 0) {
                break;
            } else {
                p.status_message = "Unsaved changes - press Ctrl-Q again to quit.";
                scroll_cursor(&p);
                refresh_screen(&p);
                if ((key = read_key()) == -1) {
                    exit_status = EXIT_FAILURE;
                    goto cleanup;
                }
                p.status_message = NULL;
                if (key == CTRL_KEY('q')) {
                    break;
                } else {
                    continue;
                }
            }
        }

        switch (key) {
        case ARROW_LEFT:
            if (p.cursor_x > 0)
                p.cursor_x--;
            break;
        case ARROW_RIGHT:
            if ((size_t)p.cursor_y < p.file_row_count &&
                (size_t)p.cursor_x < p.file_rows[p.cursor_y].length) {
                p.cursor_x++;
            }
            break;
        case ARROW_UP:
            if (p.cursor_y > 0) {
                p.cursor_y--;
                if ((size_t)p.cursor_x > p.file_rows[p.cursor_y].length) {
                    p.cursor_x = (int)p.file_rows[p.cursor_y].length;
                }
            }
            break;
        case ARROW_DOWN:
            if ((size_t)(p.cursor_y + 1) < p.file_row_count) {
                p.cursor_y++;
                if ((size_t)p.cursor_x > p.file_rows[p.cursor_y].length) {
                    p.cursor_x = (int)p.file_rows[p.cursor_y].length;
                }
            }
            break;
        case '\r':
        case '\n':
            if (insert_newline(&p) == -1) {
                fprintf(stderr, "Failed to insert newline.\n");
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            break;
        case CTRL_KEY('s'):
            if (save_file(&p) == -1) {
                p.status_message = "Save failed; changes remain unsaved.";
                break;
            }
            p.status_message = NULL;
            if (p.filename != NULL) {
                p.dirty = 0;
            }
            break;
        case 127:
        case CTRL_KEY('h'):
            if (delete_char(&p) == -1) {
                fprintf(stderr, "Failed to delete character.\n");
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            break;
        case CTRL_KEY('f'):
            if (search_prompt(&p) == -1) {
                exit_status = EXIT_FAILURE;
                goto cleanup;
            }
            break;
        case CTRL_KEY('a'):
        case HOME:
            move_cursor_home(&p);
            break;
        case CTRL_KEY('e'):
        case END:
            move_cursor_end(&p);
            break;
        case CTRL_KEY('b'):
        case PAGE_UP:
            move_cursor_page_up(&p);
            break;
        case CTRL_KEY('v'):
        case PAGE_DOWN:
            move_cursor_page_down(&p);
            break;
        default:
            if (key >= 32 && key <= 126) {
                if (insert_char(&p, key) == -1) {
                    fprintf(stderr, "Failed to insert character.\n");
                    exit_status = EXIT_FAILURE;
                    goto cleanup;
                }
            }
            break;
        }
    }

cleanup:
    free_rows(&p);
    return exit_status;
}
