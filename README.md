<img src="assets/logo/zedit-128.png" alt="zedit logo" width="96" height="96" align="left">

# zedit

A modal, vim-like text editor written in C++20, built on Dear ImGui + GLFW + OpenGL.
Targets Linux and macOS. The binary is `ze` — short to type, like `vi`. Think
gedit's approachable GUI chrome (menus, mouse support, a file browser) layered
over neovim's modal, keyboard-driven editing power.

## Features

- Modal editing: Normal/Insert/Visual/Visual Line/Command-line/Search modes,
  motions (`hjkl`, `w`/`b`/`e`, `0`/`$`, `%` bracket matching, `G`/`gg`/`:N`
  line jumps), operators (`d`/`y`/`c`) composable with counts and motions,
  registers (named and unnamed), undo/redo, dot-repeat, auto-indent
- Multiple buffers and windows (`:e`, `:bn`/`:bp`, `:sp`/`:vsp`, Ctrl-W),
  side-by-side file compare (`:diff`)
- Syntax highlighting via tree-sitter (C++, Python, Markdown), a line-numbers
  gutter, matching-bracket highlight, and optional word wrap and whitespace
  visualization (View menu)
- LSP client (clangd): live diagnostics, go-to-definition (`gd`), hover (`K`)
- Lua-based configuration: `~/.config/zedit/init.lua` sets options, colors,
  and keymaps via a small `zedit.*` API; `:lua <code>` runs Lua live as a
  scripting hook
- GUI-editor shortcuts alongside the modal keybindings: Ctrl-A (select all),
  Ctrl-C (copy), Ctrl-P (paste), Ctrl-S (save) — all work mid-Insert too
- Menu bar (File/Edit/View/Tools/Help): a directory-tree file browser and
  recent-files list for Open, Find and Replace, Tools > Sort Lines
- Mouse-wheel scrolling, click-to-position, status bar (mode, cursor
  position, word count, file path)
- A backstop max file size (256 MB) against accidentally opening something
  never meant to fit in memory whole

## Status

Core roadmap (M0–M7) complete: scaffolding, editing/motions/operators,
undo/registers/Visual mode, search/buffers, syntax highlighting, splits/diff,
LSP, and the Lua config system. Actively growing from there.

## Installing

**Linux (.deb):** download the latest release from
[github.com/sbj-ee/zedit/releases/latest](https://github.com/sbj-ee/zedit/releases/latest)
(currently
[zedit-1.6.1-Linux-amd64.deb](https://github.com/sbj-ee/zedit/releases/download/v1.6.1/zedit-1.6.1-Linux-amd64.deb))
and install with:

```sh
sudo dpkg -i zedit-1.6.1-Linux-amd64.deb
```

**macOS (.dmg, Apple Silicon only):** download the latest release from
[github.com/sbj-ee/zedit/releases/latest](https://github.com/sbj-ee/zedit/releases/latest)
(currently
[zedit-1.6.0-Darwin.dmg](https://github.com/sbj-ee/zedit/releases/download/v1.6.0/zedit-1.6.0-Darwin.dmg)),
open it, and drag `ze.app` to Applications. The app is unsigned, so on first
launch Gatekeeper will refuse to open it — right-click `ze.app` and choose
Open, then confirm in the dialog that appears (only needed once).

## Building

Requires CMake 3.21+, a C++20 compiler, and Ninja (recommended).

```sh
cmake -B build -G Ninja
cmake --build build
./build/frontend_imgui/ze [file]
```

## Testing

```sh
cmake --build build --target zedit_tests
ctest --test-dir build --output-on-failure
```

## Packaging

Builds a `.deb` on Linux, and (from an actual macOS machine — CPack's
DragNDrop generator shells out to `hdiutil`, which doesn't exist on Linux) a
`.dmg` on macOS, via CPack. The macOS build defaults to Apple Silicon only
(`CMAKE_OSX_ARCHITECTURES=arm64`, set in `CMakeLists.txt`):

```sh
cmake -B build -G Ninja
cmake --build build
cd build && cpack
```

The Linux package installs `ze`, its `.desktop` entry, and its icon to the
usual `/usr/{bin,share}` locations — see "Linux desktop integration" below
for the manual-install equivalent when just building from source.

## Linux desktop integration (optional, source builds only)

The running app embeds its own icon (window/taskbar), but whether a taskbar
or dock actually *shows* it varies by desktop environment -- some (notably
GNOME Shell) look it up via a `.desktop` file rather than reading it live off
the window. The `.deb` package (above) installs this automatically; building
from source without packaging, install it by hand:

```sh
cp assets/linux/zedit.desktop ~/.local/share/applications/
mkdir -p ~/.local/share/icons/hicolor/128x128/apps
cp assets/logo/zedit-128.png ~/.local/share/icons/hicolor/128x128/apps/zedit.png
```

## Configuration

Drop a `~/.config/zedit/init.lua` (or set `$ZEDIT_CONFIG` to a path) to
customize options, colors, and keymaps:

```lua
zedit.set_option("tabstop", 2)
zedit.set_color("comment", "#6a9955")
zedit.map("n", "Q", "dd")  -- non-recursive remap, like :noremap
```

## Project layout

- `core/` — the editing engine (buffer, cursor, modal state machine, motions,
  operators, undo, file I/O, syntax highlighting, LSP client, Lua config,
  word wrap). No GUI dependencies; independently testable.
- `frontend_imgui/` — the Dear ImGui/GLFW/OpenGL desktop frontend (menu bar,
  file browser, find/replace dialog, text view, status bar).
- `tests/` — Catch2 unit and integration tests for `core/`.
- `assets/logo/` — the app logo (SVG source + rasterized PNGs/icns), also
  embedded into the binary at build time as the window icon.
- `assets/linux/` — the `.desktop` entry for Linux desktop integration.
- `cmake/Packaging.cmake` — the `.deb`/`.dmg` CPack configuration.

## License

MIT — see [LICENSE](LICENSE).
