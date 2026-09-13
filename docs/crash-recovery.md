# zedit crash recovery (swap / autosave)

**Status:** design for MVP  
**Policy:** Linux + Apple Silicon only; no project-dir `filename~` / `#filename#` litter.

## Goal

Survive process crash / kill / power loss without littering the project tree. Recover unsaved buffer text on next open of the same file.

## Non-goals (MVP)

- Emacs/Vim-style backups next to the file
- Multi-instance locking / exclusive edit
- Restoring cursor, mode, splits, or undo history
- Encrypted swap
- Untitled (no-path) buffers (see Later)

## Store location

| Platform | Directory |
|----------|-----------|
| Linux | `$XDG_CACHE_HOME/zedit/swap/` or `~/.cache/zedit/swap/` |
| macOS | `~/Library/Application Support/zedit/swap/` (per approved CoS plan) |

Override later via env e.g. `ZEDIT_SWAP_DIR` if useful for tests.

Create the directory on first write (`mkdir -p`).

## Keying

One swap file per **canonical absolute path** of a named buffer:

- Resolve with `std::filesystem::weakly_canonical` (or absolute + normalize) so `/a/../b` and `/b` collide correctly when possible.
- Filename: `sha256(utf8 absolute path)` hex + `.swp` (stable, no awkward chars).
- Sidecar fields inside the file also store the absolute path for human debugging and for verifying the hash.

**Inode + mtime:** recorded as the **baseline** of the on-disk file at last successful load or save (inode + mtime + size). Used to decide whether recovery is “newer than what’s on disk” and to detect external edits.

Shared buffers (`:e` same path switches, no duplicate) ⇒ **one** swap entry. Splits sharing a buffer do not multiply swaps.

## On-disk format (v1)

Single binary file, written atomically (`*.swp.tmp` → `rename`):

```
magic:     "ZEDITSWP" (8 bytes)
version:   u32 LE (= 1)
path_len:  u32 LE
path:      utf-8 absolute path
baseline_mtime_ns: i64 LE   # from last clean load/save; 0 if unknown
baseline_size:     u64 LE
baseline_inode:    u64 LE   # 0 if unavailable
saved_at_ns:       i64 LE   # wall time when this swap was written
cursor_line:       u32 LE   # reserved; MVP may write 0
cursor_col:        u32 LE   # reserved; MVP may write 0
content_len:       u64 LE
content:           utf-8 bytes (full buffer text)
```

No compression in MVP. Respect existing `kMaxFileSizeBytes` — refuse to write/read swap larger than that.

**Crash mid-write:** only the `.tmp` is partial; previous `.swp` remains valid until `rename` replaces it. Orphan `.tmp` files can be deleted on startup GC (optional MVP).

## When to write

- Buffer is **dirty** and has a non-empty filename.
- **Debounce:** schedule write ~**2s** after last `mark_dirty()` (acceptable range 1–5s; constant OK for MVP).
- Frontend already has a per-frame tick (`App::render_frame`) — call `RecoveryStore::poll(editor)` once per frame (or every N ms) to flush due debounces.
- Skip write if content hash equals last written swap (optional micro-opt).

## When to clear

Delete the swap file when:

1. Successful `save()` / `save_as()` (dirty cleared).
2. User chooses **Don’t recover** on the reopen prompt.
3. After successful recover **and** a later successful save (keep swap while still dirty so a second crash doesn’t lose the recovered text).
4. Buffer closed while **not** dirty (optional hygiene).

Do **not** clear merely because the app quit while dirty — that *is* the crash/unclean case we want to recover. Clean quit with dirty buffers: today zedit doesn’t prompt; MVP leaves swap in place (acts as recovery for next open). Later: quit prompt can clear after discard.

## Reopen UX

Hook after `Editor::open_file` / `open_buffer` loads disk contents (and on CLI initial path):

1. Look up swap for that absolute path.
2. If missing → normal open.
3. If present:
   - If `saved_at` ≤ baseline file mtime **and** content identical to disk → stale; delete and continue.
   - If swap content equals disk content → delete and continue.
   - Else → **prompt:** “Recover unsaved changes to `<basename>`?”  
     - **Recover:** replace buffer text with swap content; leave `dirty = true`; keep swap until save.  
     - **Discard:** delete swap; keep disk content; `dirty = false`.

**Prompt UI:** ImGui modal in `frontend_imgui` (zedit has no core confirm dialog today; `:bd` on dirty is silent no-op). Core exposes `std::optional<RecoveryOffer>` / apply+discard APIs; frontend owns the modal.

Defer prompt until the first frame after open so GLFW/ImGui are up (CLI open in `main`).

## Edge cases

| Case | Behavior |
|------|----------|
| Untitled (`filename` empty) | MVP: **no swap**. Later: `untitled-<uuid>.swp` + recovery picker on launch. |
| Multi-window same file | One buffer ⇒ one swap. |
| Two zedit instances same file | Last debounce wins; no flock in MVP. |
| External disk edit while swap exists | If disk mtime > swap `saved_at` and contents differ from swap → prefer prompt text: “File changed on disk; recover zedit autosave anyway?” (MVP may use same Recover/Discard). |
| Path rename / Save As | Clear old path’s swap; new path starts fresh; after save clear new path swap. |
| File deleted on disk, swap remains | Opening that path (new file / `:e`) can still offer recover if swap exists. |
| Corrupt / bad magic / truncated | Ignore + delete swap; open disk normally; optional status message. |

## Module sketch

- `core/include/zedit/core/recovery.hpp` + `recovery.cpp` — path helpers, read/write/clear, debounce helper.
- `Editor`: after `mark_dirty` arm debounce; `save` clears; `open_*` returns/queues recovery offer.
- `App::render_frame`: `poll` debounced writes; show modal if offer pending.
- Tests: round-trip format, atomic replace, clear-on-save, detect-on-open, corrupt ignore (temp dirs under `/tmp`).

## MVP vs later

**MVP**

1. Cache/AS swap dir + hashed `.swp` format v1  
2. Debounced write while dirty (named buffers only)  
3. Clear on successful save  
4. On open: detect + ImGui Recover/Discard  
5. Unit tests for store  

**Later**

- Untitled recovery + startup “orphan swaps” list  
- Cursor/mode restore  
- Periodic flush while dirty (e.g. 30s) + config (`swapinterval`, disable)  
- `flock` / singleton per path  
- GC old swaps (age / missing path)  
- Quit dialog: save / discard / cancel (discard clears swap)  

## Implementation order

1. `RecoveryStore` + tests (no UI)  
2. Wire `mark_dirty` / `save` / `open_file`  
3. ImGui modal + `App` poll  
4. Docs blurb in README (one short subsection)
