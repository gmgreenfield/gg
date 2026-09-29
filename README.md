# gg screen editor

[![NO AI](https://raw.githubusercontent.com/nuxy/no-ai-badge/master/badge.svg)](https://github.com/nuxy/no-ai-badge)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
![PRs](https://img.shields.io/badge/PRs-welcome-brightgreen)

This is a screen editor for Posix complaint operating systems (Linux, *BSD, Solaris, etc).
It's a spare time project, and isn't ready to be used by anyone but myself. I'll be updating
this README with additional details as the project progresses. 

## Keybindings

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor |
| `ctrl-a` or `home` | Move to the beginning of the current line |
| `ctrl-e` or `end` | Move to the end of the current line |
| `ctrl-b` or `pgup` | Move up by one screen of text |
| `ctrl-v` or `pgdn` | Move down by one screen of text |
| `ctrl-f` | Search for text and move to the next match |
| `ctrl-s` | Save the current file |
| `ctrl-q` | Quit; press it again to confirm when there are unsaved changes |

In Apple's Terminal on macOS, hold Shift when pressing Page Up or Page Down
to send those keys to the editor. Alternatively, use `ctrl-b` and `ctrl-v`.
The Control-key bindings work on both Linux and macOS; use Control, not Command.

## Installation

Run `make install` to build the editor and install it as `gg` in
`~/.local/bin`. If that directory is not in your `PATH`, add it to your shell's
`PATH` to run `gg` by name. To choose another installation directory, run
`make install BINDIR=/path/to/bin`.

## Building and testing

Run `make` to build `gg`, or `make debug` to build `gg-debug` with debugging
symbols. Run `make test` to build and run `build/editor_tests`. The file-I/O
tests deliberately print errors such as "No space left on device" while checking
failed saves; a successful run ends with `all tests passed`.

Run `make format` to format the C sources and headers, or `make format-check`
to check formatting without changing files. The GitHub formatting workflow uses
the same check. `make clean` removes the Make-built executables and test object.

Alternatively, build and test with CMake:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

## Source layout

- `src/main.c`: startup, shutdown, and key dispatch.
- `src/editor.c` and `src/editor.h`: buffer editing, navigation, search, and shared types.
- `src/file_io.c` and `src/file_io.h`: file loading and saving.
- `src/terminal.c` and `src/terminal.h`: terminal setup, input, drawing, resizing, and the search prompt.
- `tests/test_buffer.c`, `test_navigation.c`, `test_search.c`, and `test_file_io.c`:
  tests grouped by feature.
- `tests/test_main.c` and `test_helpers.c`/`.h`: the test runner and shared assertions.

The tests include headers and link separately compiled implementation files.
Only the test build of `src/file_io.c` redirects `fwrite()` and `rename()` to
failure-injection wrappers; the normal editor and test fixtures use the real
library functions. All test groups run in one executable.
