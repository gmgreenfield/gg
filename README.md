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

Configure and build the editor, then install `gg` in `~/.local/bin`:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
cmake --install build/release --prefix "$HOME/.local"
```

If `~/.local/bin` is not in your `PATH`, add it to run `gg` by name. Change
the `--prefix` path to install somewhere else. To use a different directory
name under that prefix, configure with `-DCMAKE_INSTALL_BINDIR=your-bin-dir`.

## Building and testing

Build and run both test executables with CMake and CTest:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
(cd build/release && ctest --output-on-failure)
```

The file-I/O tests deliberately print errors such as "No space left on device"
while checking failed saves. A successful CTest run reports that both tests passed.

For a build with debugging symbols, use a separate build directory:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

The executables are `build/release/gg` and `build/debug/gg`. To check formatting
without changing files, run `cmake --build build/release --target format-check`.
To format the C sources and headers, use the `format` target instead. These two
targets require `clang-format`; normal builds and tests do not. The GitHub
workflow runs the same read-only formatting check. Run
`cmake --build build/release --target clean` to remove compiled artifacts
from that build directory.

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
