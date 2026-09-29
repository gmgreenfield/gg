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
| `ctrl-s` | Save; prompt for a filename if the buffer is unnamed (disabled with `-R`) |
| `ctrl-q` | Quit; press it again to confirm when there are unsaved changes |

In Apple's Terminal on macOS, hold Shift when pressing Page Up or Page Down
to send those keys to the editor. Alternatively, use `ctrl-b` and `ctrl-v`.
The Control-key bindings work on both Linux and macOS; use Control, not Command.
In the Save As prompt, press Enter to use the typed filename or Esc to cancel.
If that filename already exists, the editor asks before replacing it.
After a successful save, the status bar briefly displays the saved filename;
the message disappears after about two seconds or on the next keypress.

## Read-only viewing

Use `-R` to view an existing file without changing it:

```sh
gg -R path/to/file
```

The status bar shows `[read-only]`. Navigation and search still work, but typing,
Enter, Backspace, and Ctrl-S cannot change or save the file. Ctrl-Q exits without
an unsaved-changes prompt.

To view output from a command, use `-` in place of the filename:

```sh
some-command | gg -R -
```

The command must finish before `gg` opens the viewer. Keyboard input still comes
from your terminal, so this form needs an interactive terminal. It does not
follow live output from commands such as `tail -f`.

To try it with a local build, run `./build/release/gg -R README.md`. Press a letter,
Enter, Backspace, and Ctrl-S; each should show a read-only message without changing
the text. Then move with the arrow keys, search with Ctrl-F, and quit with Ctrl-Q.
To test piped input, run `printf 'first\nsecond\n' | ./build/release/gg -R -`
and check that both lines appear with the same read-only behavior.

## Installation

From the project directory, run these commands (requires CMake 3.20 or newer):

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix "$HOME/.local"
```

The first command prepares the build, the second compiles `gg`, and the third
copies it to `~/.local/bin/gg`. This installs only for your user account; no
`sudo` is needed. To start the editor, run `~/.local/bin/gg`. If you want to
run it by typing only `gg`, add `~/.local/bin` to your shell's `PATH`.

To clean the release build (the equivalent of `make clean`), run:

```sh
cmake --build --preset clean
```

This removes the compiled build files but leaves the installed
`~/.local/bin/gg` in place.

## Building and testing

Build and run both test executables with the project presets (CMake 3.20 or newer):

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The file-I/O tests deliberately print errors such as "No space left on device"
while checking failed saves. A successful CTest run reports that both tests passed.

For a build with debugging symbols, use a separate build directory:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The executables are `build/release/gg` and `build/debug/gg`. To check formatting
without changing files, run `cmake --build --preset format-check`. To format
the C sources and headers, run `cmake --build --preset format`. These two
presets require `clang-format`; normal builds and tests do not. The GitHub
workflow runs the same read-only formatting check.

The presets are shortcuts for configuring, building, and testing in `build/`;
they do not change the editor's minimum CMake requirement of 3.16. With CMake
3.16–3.19, use the equivalent commands without presets, for example:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
(cd build/release && ctest --output-on-failure)
```

## Source layout

- `src/main.c`: startup, shutdown, and key dispatch.
- `src/editor.c` and `src/editor.h`: buffer editing, navigation, search, and shared types.
- `src/file_io.c` and `src/file_io.h`: file loading and saving.
- `src/terminal.c` and `src/terminal.h`: terminal setup, input, drawing, resizing, and the search prompt.
- `tests/test_buffer.c`, `test_navigation.c`, `test_search.c`, and `test_file_io.c`:
  tests grouped by feature.
- `tests/test_main.c` and `test_helpers.c`/`.h`: the test runner and shared assertions.
- `tests/test_main_loop.c`: tests for the editor's main loop.

The tests include headers and link separately compiled implementation files.
Only the test build of `src/file_io.c` redirects `fwrite()` and `rename()` to
failure-injection wrappers; the normal editor and test fixtures use the real
library functions. CTest runs the editor and main-loop tests as separate
executables.
