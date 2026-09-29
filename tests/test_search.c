#include "editor.h"
#include "test_helpers.h"

#include <stdio.h>
#include <string.h>

void test_search(void) {
    editor_state state = {0};
    int match_row;
    int match_col;

    check(append_row(&state, "alpha beta", 10) == 0, "append first search row");
    check(append_row(&state, "gamma alpha", 11) == 0, "append second search row");
    check(append_row(&state, "alpha alpha", 11) == 0, "append repeated-match row");
    check(append_row(&state, "", 0) == 0, "append empty search row");
    check(append_row(&state, "cross", 5) == 0, "append cross-line search row");
    check(append_row(&state, "line", 4) == 0, "append second cross-line row");

    check(find_next_match(&state, "alpha", 0, 0, &match_row, &match_col) == 0,
          "find first search match");
    check(match_row == 0 && match_col == 0, "first search match position");

    check(find_next_match(&state, "alpha", 0, 1, &match_row, &match_col) == 0,
          "find next search match");
    check(match_row == 1 && match_col == 6, "next search match position");

    check(find_next_match(&state, "alpha", 5, 0, &match_row, &match_col) == 0,
          "search wraps around at end of file");
    check(match_row == 0 && match_col == 0, "wrapped search match position");

    check(find_next_match(&state, "missing", 0, 0, &match_row, &match_col) == -1,
          "search reports a missing match");

    check(find_next_match(&state, "", 0, 0, &match_row, &match_col) == -1,
          "empty search query is rejected");

    check(find_next_match(&state, "beta", 0, 6, &match_row, &match_col) == 0,
          "search includes a match at the starting column");
    check(match_row == 0 && match_col == 6, "exact starting-column match position");

    check(find_next_match(&state, "beta", 0, 7, &match_row, &match_col) == 0,
          "search wraps within the starting row");
    check(match_row == 0 && match_col == 6, "same-row wrapped match position");

    check(find_next_match(&state, "alpha", -1, 0, &match_row, &match_col) == -1,
          "negative starting row is rejected");
    check(find_next_match(&state, "alpha", 0, 99, &match_row, &match_col) == -1,
          "starting column past the row is rejected");

    check(find_next_match(&state, "alpha", 2, 1, &match_row, &match_col) == 0,
          "search finds a later match on the same row");
    check(match_row == 2 && match_col == 6, "later same-row match position");

    check(find_next_match(&state, "solo", 2, 6, &match_row, &match_col) == -1,
          "missing same-row query is reported when no match exists");

    check(find_next_match(&state, "a", 1, 10, &match_row, &match_col) == 0,
          "search finds a match at the end of a row");
    check(match_row == 1 && match_col == 10, "end-of-row match position");

    check(find_next_match(&state, "Alpha", 0, 0, &match_row, &match_col) == -1,
          "search remains case sensitive");
    check(find_next_match(&state, "cross\nline", 0, 0, &match_row, &match_col) == -1,
          "search does not cross line boundaries");
    check(find_next_match(&state, "missing", 3, 0, &match_row, &match_col) == -1,
          "search reports no match in an empty row");
    check(find_next_match(NULL, "alpha", 0, 0, &match_row, &match_col) == -1,
          "null editor state is rejected");
    check(find_next_match(&state, NULL, 0, 0, &match_row, &match_col) == -1,
          "null search query is rejected");
    check(find_next_match(&state, "alpha", 0, 0, NULL, &match_col) == -1,
          "null match row output is rejected");
    check(find_next_match(&state, "alpha", 0, 0, &match_row, NULL) == -1,
          "null match column output is rejected");

    free_rows(&state);
}
