<img src="assets/logo/zedit-128.png" alt="zedit logo" width="96" height="96" align="left">

# zedit

A modal, vim-like text editor written in C++20, built on Dear ImGui + GLFW + OpenGL.
Targets Linux and macOS. The binary is `ze` — short to type, like `vi`. Think
gedit's approachable GUI chrome (tabs, menus, a toolbar, mouse support) layered
over neovim's modal, keyboard-driven editing power.

## Features

- Modal editing: Normal/Insert/Visual/Visual Line/Command-line/Search modes,
  motions (`hjkl`, `w`/`b`/`e`, `0`/`$`), operators (`d`/`y`/`c`) composable
  with counts and motions, registers (named and unnamed), undo/redo, dot-repeat
- Multiple buffers and windows (`:e`, `:bn`/`:bp`, `:sp`/`:vsp`, Ctrl-W),
  side-by-side file compare (`:diff`)
- Syntax highlighting via tree-sitter (C++, Python, Markdown)
- LSP client (clangd): live diagnostics, go-to-definition (`gd`), hover (`K`)
- Lua-based configuration: `~/.config/zedit/init.lua` sets options, colors,
  and keymaps via a small `zedit.*` API; `:lua <code>` runs Lua live as a
  scripting hook
- GUI-editor shortcuts alongside the modal keybindings: Ctrl-A (select all),
  Ctrl-C (copy), Ctrl-P (paste), Ctrl-S (save) — all work mid-Insert too
- Menu bar (File/Edit/View/Help), with a directory-tree file browser for Open
- Status bar: mode, cursor position, word count, file path

## Status

Core roadmap (M0–M7) complete: scaffolding, editing/motions/operators,
undo/registers/Visual mode, search/buffers, syntax highlighting, splits/diff,
LSP, and the Lua config system. Actively growing from there.

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

## Linux desktop integration (optional)

The running app embeds its own icon (window/taskbar), but whether a taskbar
or dock actually *shows* it varies by desktop environment -- some (notably
GNOME Shell) look it up via a `.desktop` file rather than reading it live off
the window. To install one:

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
  operators, undo, file I/O, syntax highlighting, LSP client, Lua config).
  No GUI dependencies; independently testable.
- `frontend_imgui/` — the Dear ImGui/GLFW/OpenGL desktop frontend (menu bar,
  file browser, text view, status bar).
- `tests/` — Catch2 unit and integration tests for `core/`.
- `assets/logo/` — the app logo (SVG source + rasterized PNGs), also embedded
  into the binary at build time as the window icon.

## License

MIT — see [LICENSE](LICENSE).
