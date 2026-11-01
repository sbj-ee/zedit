# Changelog

zedit had no changelog before 1.8.0; earlier history is in the git log and the GitHub releases.

## 1.10.0 — 2026-11-01

- Editing is UTF-8 aware: h/l, a, x (with counts), insert-mode backspace and cursor clamping move and delete whole code points instead of single bytes.
- Typed characters of U+0080 and above are inserted as their full UTF-8 sequence instead of being dropped.
- LSP positions use UTF-16 columns as the protocol requires: cursor positions sent to the server, plus diagnostics and go-to-definition results coming back, are converted.
- file:// URIs percent-encode spaces, #, % and non-ASCII bytes, and decoding handles percent escapes and file://localhost/.
- Known limitations: normal-mode r followed by a multibyte character still replaces with only one byte; the ImGui frontend uses 16-bit ImWchar, so characters above U+FFFF typed from the keyboard arrive as U+FFFD (pasting them works).

## 1.9.4 (unreleased)

### Security
- The updater now requires the `SHA256SUMS` signature's trusted comment to be exactly `zedit <tag> SHA256SUMS` for the release being installed (what `release.yml` signs; checked against the real v1.8.5 release, whose signed text is `zedit v1.8.5 SHA256SUMS`). A validly signed `SHA256SUMS` from another release can no longer be replayed under a different tag; it fails before the package is downloaded.
- Downloads are capped while they stream: curl's `--max-filesize` only works when the server sends a length, so a response without one (or a wrong one) is now stopped as soon as the file on disk passes the limit (1 MiB for `SHA256SUMS`/`.minisig`, 1 GiB for packages) and reported as too large.

## 1.9.3 (unreleased)

### Tests / CI
- Tests that need a real `clangd` now `SKIP` when it isn't on `PATH` instead of failing; CI sets `ZEDIT_REQUIRE_CLANGD=1` so a missing clangd there is still a failure.
- New CI job: Linux AddressSanitizer + UndefinedBehaviorSanitizer build of the test suite (`-DZEDIT_SANITIZE=ON`).
- CI builds with `-DZEDIT_WERROR=ON` (new option, off by default): warnings in zedit's own code fail CI without breaking local builds on newer compilers. Dear ImGui and Lua headers are now SYSTEM includes so their macros don't warn in our code; `-Wmissing-field-initializers` is off for the `Struct{first}` idiom. On GCC, `-Wnull-dereference` stays a warning (not an error) because GCC 13 at -O2 reports false positives from inlined libstdc++ `<streambuf>`.
- New `scripts/check-version.sh` (run in CI and, with `--tag`, by the release workflow): `project()` version, the top CHANGELOG heading and the README note must agree; a tag must match and its heading must be dated.

### Docs
- README install instructions point at `releases/latest` rather than stale pinned 1.7.4 links, use `apt install ./…deb`, list the Linux build dependencies, and document the clangd skip and sanitizer/Werror options; the "currently" version is current again.
- CHANGELOG headings of released versions carry their release dates.

## 1.9.2 (unreleased)

### Performance
- Syntax highlighting reparses incrementally: `set_text()` finds the changed span (common prefix/suffix), describes it with `ts_tree_edit()`, and hands the old tree to tree-sitter, so a keystroke re-lexes only the region around the edit instead of the whole file. Applies to every tree-sitter language and to the Markdown block tree. Unchanged text skips the reparse.

## 1.9.1 (unreleased)

### Performance
- Git gutter no longer re-diffs the whole file every frame: the result is cached per buffer and recomputed only when the buffer content changes (new `PieceTable::revision()`), and HEAD is split into lines once.
- `diff_lines` now uses Myers' O((N+M)·D) algorithm (linear-space middle snake) with common prefix/suffix trimming and interned lines, replacing the O(N·M) time and memory LCS table. A 50,000-line file with a few edits now diffs in milliseconds instead of needing a 2.5e9-cell table.

## 1.9.0 (unreleased)

### Fixed

- **Pasting in Insert mode is no longer quadratic.** Ctrl-P (and dot-repeat of typed text) now
  inserts the whole text as one edit instead of one byte at a time, each of which copied the
  buffer, re-parsed it and re-sent it to clangd. A 90 KB paste that took tens of seconds is now
  instant.
- **No more unbounded memory while clangd is starting.** Changes made before the server finishes
  initializing used to queue a full copy of the document per keystroke; now only the newest text
  per document is kept and sent once the server is ready.

### Changed

- Each edit copies the buffer text once (shared by the highlighter and the LSP change) instead
  of twice.

## 1.8.10 (unreleased)

### Security

- **Swap files are now private.** Crash-recovery swap files hold unsaved text, so they are now
  created `0600` (never through a symlink) inside a `0700` directory; an existing swap directory
  made with default permissions by an older zedit is tightened to `0700`, and a swap directory
  that is a symlink or owned by someone else is refused.
- **No guessable shared fallback.** With no `HOME`/`XDG_CACHE_HOME`, swaps go to
  `/tmp/zedit-<uid>/swap` instead of a fixed `/tmp/zedit/swap` another user could create first.

### Fixed

- **A swap file written by a different zedit version is kept.** It used to be deleted as
  corrupt, discarding that version's recovery data; it's now left alone.

### Changed

- Recovery uses the shared SHA-256 (`sha256.cpp`) instead of its own copy. Swap file names are
  unchanged, so existing swaps are still found.

## 1.8.9 (unreleased)

### Fixed

- **`:q` and `:wq` no longer quit over unsaved changes in another buffer.** Like vim, they now
  refuse with `E162: No write since last change for buffer "…" (add ! to override)` while any
  open buffer is dirty; `:wq` still saves the current buffer first. `:q!` quits regardless.
- **A failed save no longer looks like a successful one.** Saving now writes a temp file next to
  the original, fsyncs it and renames it into place, so a full disk or I/O error leaves the
  original file untouched, keeps the buffer marked unsaved, and shows the error on the status
  line (including from File ▸ Save, the command palette and the close-buffer prompt, which used
  to swallow it). Saves keep the file's permissions and write through symlinks to their target.
  Devices and FIFOs, and files in a directory zedit can't create files in, are still written in
  place, with every write, fsync and close checked.

## 1.8.8 (unreleased)

### Fixed

- **A crashed or missing clangd no longer takes zedit down.** zedit now ignores `SIGPIPE`, so
  typing after the language server exits fails the write quietly instead of killing the editor
  (exit 141) with unsaved buffers.
- **A missing clangd is reported as missing.** Starting a server whose `exec` fails now returns
  failure (the child reports its errno over a close-on-exec pipe), and "LSP running" checks that
  the child is still alive, so nothing is sent to a process that isn't there.
- **Pasting non-ASCII text in a C/C++ file can't crash the editor.** Messages to the server are
  encoded with invalid UTF-8 replaced (U+FFFD) instead of throwing `type_error.316`; sends and
  incoming server messages can no longer throw into the editor, and the frame loop logs and
  survives any exception that does escape.

### Changed

- Subprocess pipes are close-on-exec (no fd leaks into other children), the child's argv is
  built before `fork()` (no allocation in the child of a threaded process), and the LSP reader
  thread is joined before its pipe is closed (no close-while-reading race).

## 1.8.7 (unreleased)

### Documentation

- **README screenshot**: the README now shows zedit editing a C file (`docs/screenshot.png`).
  No code changes.

## 1.8.6 (unreleased)

### Changed

- **CI and release builds moved from the macos-14 to the macos-15 runner image**, which GitHub
  retires on November 2, 2026. No code changes.

## 1.8.5 — 2026-10-29

### Changed

- **Recent Files moved to the bottom of the File menu**, after Close File and just above Quit,
  between separators. Its submenu no longer covers Save and Save As… while moving down the menu.
  Behaviour is unchanged.

## 1.8.4 — 2026-10-28

### Fixed

- **Quitting no longer throws away unsaved work.** File → Quit, Cmd-Q (and the Dock's Quit) and
  the window close button used to exit immediately even with unsaved changes. If any buffer —
  the one on screen or one in the background, named or not — has unsaved changes, zedit now
  stays open and asks about each in turn, showing it first: **Save** (**Save As…** for a buffer
  with no name; cancelling that dialog returns to the question), **Don't Save**, or **Cancel**;
  **Discard All & Quit** skips the rest. It quits once nothing is left unsaved, and at once when
  everything is already saved. `:q!` still quits unconditionally; `:q`/`:wq` are unchanged.

## 1.8.3 — 2026-10-27

### Fixed

- **File → New did nothing, and a buffer with unsaved changes couldn't be closed.** Pasting into
  the startup (unnamed) buffer and then choosing File → New (or Command Palette → New) appeared
  to do nothing: New was `open_buffer("")`, and an empty path matches the buffer that is already
  open, so it only switched back to it. New now always adds a fresh empty buffer
  (`Editor::new_buffer()`) and the earlier one, unsaved changes included, stays open. In the same
  situation File → Close File was greyed out for any buffer with unsaved changes, leaving no way
  to close it short of quitting. It is now always enabled; on a buffer with unsaved changes it
  asks **Save / Discard / Cancel** (Save only for a named file; an unnamed buffer says to use
  Save As… first). Cancel keeps everything as it was. `:bd!` / `:bdelete!` discards a dirty
  buffer from the keyboard; plain `:bd` still declines.

## 1.8.2 — 2026-10-18

### Fixed

- **Release workflow.** The v1.8.1 tag built and tested but its release run failed, so nothing
  was published. The macOS "Verify .dmg" step died with `hdiutil: attach canceled` because the
  .dmg shows its licence agreement and CI has no stdin; it now accepts the agreement
  non-interactively and still checks that `ze.app` and its executable are in the image. The Publish
  step also looked for the public key on a single line of `core/src/update_signing_key.cpp`, but
  the key literal sits on its own line, so it would have refused to publish ("has no public key");
  it now matches the key literal itself. No change to the app. 1.8.1 was tagged but never
  published; 1.8.2 is the first release signed with `DBC46B23C3AF0EF3`.

## 1.8.1 — 2026-10-18 (tagged, not published; see 1.8.2)

### Changed

- **zedit's own release key.** The updater now trusts minisign key `DBC46B23C3AF0EF3` (zedit's
  own, replacing the zterminal key `32960BBA77B36F1F` that 1.8.0 embedded), the key that
  `release.yml` signs with. 1.8.0 was never tagged or released. The real-signature test (a
  published zterminal `SHA256SUMS`) now pins zterminal's key as a constant in the test instead of
  reading the embedded key, so it still proves real-minisign interoperability; the embedded
  key's ID and bytes are pinned by their own test. Docs name the new key. No other behaviour
  change.

## 1.8.0 — unreleased

### Added

- **In-app updates.** Help → Check for Updates now offers to install a newer release instead of
  just linking to it: it downloads the package, verifies it, and installs it.
  - The release's `SHA256SUMS` must carry a valid minisign signature
    (`SHA256SUMS.minisig`) by the release key built into zedit, checked before the package is
    downloaded; the package must then match the signed SHA-256. A release without a signature
    is never installed: zedit says so and points at the release page.
  - Linux: the verified package is copied to a private folder, re-hashed, and installed with
    `pkexec apt install` (a system password prompt; no terminal `sudo`). Only for the `.deb`
    install at `/usr/bin/ze`. If it fails, the verified package is kept and the manual
    `sudo apt install` command is shown.
  - macOS: the verified `.dmg` opens for drag-to-Applications.
  - After a Linux install, zedit offers to restart (disabled while a buffer has unsaved
    changes) and reopens the current file.
  - Download progress with Cancel; "Skip This Version"; **Help → Check for Updates at Startup**
    (on by default, at most once a day).
- Release workflow (`.github/workflows/release.yml`) that builds, tests, packages, signs
  `SHA256SUMS` with minisign and publishes on a `v*` tag; `docs/RELEASING.md`.

### Changed

- The startup update check now runs at most once a day (it ran on every launch), and can be
  switched off in the Help menu. Its state is stored in `~/.config/zedit/update_state`.
- Update checks parse versions as full semantic versions (prereleases sort before releases).

### Dependencies

- Vendored [Monocypher](https://monocypher.com/) 4.0.2 (public domain / BSD-2), statically linked,
  used only to verify the minisign signature. No new runtime libraries; the `.deb`'s
  dependencies are unchanged. Network access still goes through the system `curl`.
