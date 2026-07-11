#pragma once

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "zedit/core/cursor.hpp"
#include "zedit/core/diff.hpp"
#include "zedit/core/highlight.hpp"
#include "zedit/core/languages.hpp"
#include "zedit/core/lsp.hpp"
#include "zedit/core/mode.hpp"
#include "zedit/core/mode_state_machine.hpp"
#include "zedit/core/piece_table.hpp"
#include "zedit/core/registers.hpp"

namespace zedit::core {

enum class SplitLayout { Single, Stacked, SideBySide };

// Facade tying the buffer, cursor, and modal state machine together. This is
// the API surface both the GUI frontend and headless unit tests drive.
//
// Editor can hold multiple open buffers (:e, :bn, :bp) and multiple windows
// (:sp, :vsp) -- a window is a viewport (cursor position) onto some buffer;
// several windows can view the same buffer independently, or different
// buffers. Every method below that touches "the buffer" or "the cursor" --
// buffer(), cursor(), filename(), dirty(), save(), undo()/redo(), etc. --
// operates on whichever window/buffer is current, so existing callers
// (ModeStateMachine, the operator functions, the frontend) needed no
// changes when multi-buffer or multi-window support was added: the
// frontend renders N panes by temporarily calling set_current_window(i)
// for each one and reusing the same single-window rendering code. Undo
// history and the syntax highlighter live on the buffer (shared by every
// window viewing it); the cursor lives on the window. Registers, search
// state, and the modal state machine are shared across everything,
// matching vim: switching buffers or windows doesn't clear your registers.
class Editor {
 public:
  Editor() = default;
  static Editor open_file(const std::string& path);

  KeyResult handle_key(KeyEvent ev) { return mode_sm_.handle_key(ev, *this); }

  PieceTable& buffer() { return cur_buffer().content; }
  const PieceTable& buffer() const { return cur_buffer().content; }

  Cursor cursor() const { return cur_window().cursor; }
  void set_cursor(Cursor c) { cur_window().cursor = c; }
  size_t cursor_offset() const;
  Cursor offset_to_cursor(size_t offset) const;

  Mode mode() const { return mode_sm_.mode(); }
  const std::string& command_line_buffer() const {
    return mode_sm_.command_line_buffer();
  }
  char line_input_prefix() const { return mode_sm_.line_input_prefix(); }
  const std::string& last_error() const { return mode_sm_.last_error(); }
  Cursor visual_anchor() const { return mode_sm_.visual_anchor(); }

  const std::string& filename() const { return cur_buffer().filename; }
  // Also re-picks the syntax highlighter to match the new filename's
  // extension (e.g. starting an empty buffer destined for "foo.cpp").
  void set_filename(std::string path);

  bool dirty() const { return cur_buffer().dirty; }
  bool should_quit() const { return should_quit_; }
  void request_quit() { should_quit_ = true; }

  void save();
  void save_as(const std::string& path);

  // Cursor motion, clamped per the current mode's valid column range
  // (Normal mode sits on a character; Insert mode may sit one past the
  // last character).
  void move_left();
  void move_right();
  void move_up();
  void move_down();
  void clamp_cursor_to_line();

  // Editing operations used by the mode state machine's Insert handling.
  void insert_char(char c);
  void backspace();
  void open_line_below();
  void open_line_above();
  // The leading spaces/tabs of `line`, used for auto-indent -- both
  // open_line_below/above and Insert mode's Enter key copy the current
  // line's indentation onto the new line.
  std::string leading_whitespace_of_line(size_t line) const;

  // General-purpose range edits used by operator (d/y/c/x/p) execution.
  void erase_range(size_t offset, size_t length);
  void insert_text(size_t offset, std::string_view text);
  size_t current_line_length() const;

  // Registers populated by delete/change/yank and consumed by paste. `name`
  // 0 means the unnamed register (the default target). A named register
  // ('a'-'z') also updates the unnamed register, matching vim: "ayy then a
  // bare p pastes what you just yanked, register prefix or not.
  const RegisterContent& unnamed_register() const { return unnamed_register_; }
  void set_unnamed_register(std::string text, bool linewise) {
    unnamed_register_ = RegisterContent{std::move(text), linewise};
  }
  const RegisterContent& register_content(char name) const {
    if (name == 0) return unnamed_register_;
    return named_registers_[static_cast<size_t>(name - 'a')];
  }
  void set_register(char name, std::string text, bool linewise) {
    if (name == 0) {
      set_unnamed_register(std::move(text), linewise);
      return;
    }
    unnamed_register_ = RegisterContent{text, linewise};
    named_registers_[static_cast<size_t>(name - 'a')] =
        RegisterContent{std::move(text), linewise};
  }

  // Each undo-able user action snapshots buffer + cursor state once via
  // begin_undo_group() before mutating; undo()/redo() restore snapshots.
  // Because PieceTable snapshots are just the piece list (see
  // PieceTable::Snapshot), this is cheap regardless of document size. Undo
  // history is per-buffer, like vim, shared by every window viewing it.
  void begin_undo_group();
  void undo();
  void redo();

  // Search state persists across mode transitions and buffer switches (like
  // registers) so 'n' and 'N' can repeat the last '/' or '?' search, or the
  // last '*'/'#' word search, regardless of what's happened since.
  const std::string& last_search_pattern() const { return last_search_pattern_; }
  bool last_search_forward() const { return last_search_forward_; }
  void set_last_search(std::string pattern, bool forward) {
    last_search_pattern_ = std::move(pattern);
    last_search_forward_ = forward;
  }
  // Jumps to the next/previous match of the last search pattern. Returns
  // false (no-op) if there is no pattern set or it doesn't occur anywhere.
  bool jump_to_search(bool forward);

  // Find & replace (plain substring, no regex -- same scope as search).
  // If the cursor is currently sitting exactly at an occurrence of
  // `find`, replaces it and returns true; otherwise a no-op returning
  // false (callers pair this with jump_to_search to walk through
  // matches one at a time, "Replace" in a gedit-style dialog).
  bool replace_current_match(std::string_view find, std::string_view replace_with);
  // Replaces every non-overlapping occurrence of `find` in the whole
  // buffer as a single undoable action. Returns the number replaced.
  size_t replace_all(std::string_view find, std::string_view replace_with);

  // Sorts lines [start_line, end_line] (inclusive, clamped to the buffer)
  // lexicographically, replacing that range in place as a single
  // undoable action. Used by the Tools > Sort menu, over either the
  // current Visual selection's line range or the whole buffer.
  void sort_lines(size_t start_line, size_t end_line, bool reverse);

  // Multiple buffers (:e, :bn, :bp, :ls). Opening a path that's already
  // open just switches to it rather than duplicating it; opening a path
  // that doesn't exist on disk yet starts an empty buffer with that name,
  // matching vim's ":e newfile.txt". These act on the CURRENT WINDOW's
  // buffer assignment; other windows viewing a different buffer are
  // unaffected.
  size_t buffer_count() const { return buffers_.size(); }
  size_t current_buffer_index() const { return cur_window().buffer_index; }
  const std::string& buffer_filename(size_t index) const {
    return buffers_[index].filename;
  }
  bool buffer_dirty(size_t index) const { return buffers_[index].dirty; }
  void switch_to_buffer(size_t index);
  void next_buffer();
  void prev_buffer();
  void open_buffer(const std::string& path);

  // The current buffer's syntax highlighter (one per buffer, since
  // different open files can be different languages). Defaults to a
  // PlainHighlighter; callers that know the file type (e.g. Editor's own
  // open_file/open_buffer, by extension) can install a real one.
  Highlighter& highlighter() { return *cur_buffer().highlighter; }
  const Highlighter& highlighter() const { return *cur_buffer().highlighter; }
  void set_highlighter(std::unique_ptr<Highlighter> h) {
    cur_buffer().highlighter = std::move(h);
    cur_buffer().highlighter->set_text(buffer().to_string());
  }

  // Windows (:sp, :vsp, Ctrl-W). A window is a cursor position onto some
  // buffer; splitting duplicates the current window (same buffer, same
  // cursor) so both start out looking at the same place. Only one uniform
  // orientation is supported at a time -- the first split in a session
  // fixes it, and later splits (regardless of :sp vs :vsp) join that same
  // row/column rather than nesting into a general split tree. That's a
  // deliberate scope cut: it covers the common "N panes side by side" or
  // "N panes stacked" cases without the complexity of arbitrary nested
  // layouts.
  size_t window_count() const { return windows_.size(); }
  size_t current_window_index() const { return current_window_; }
  void set_current_window(size_t index) {
    if (index < windows_.size()) current_window_ = index;
  }
  SplitLayout split_layout() const { return split_layout_; }
  void split_horizontal();  // :sp -- stacked (top/bottom) panes
  void split_vertical();    // :vsp -- side-by-side (left/right) panes
  void next_window();       // Ctrl-W -- cycles focus
  void close_window();      // :close

  // Compare-files (:diff <path>). Vertically splits and opens `path`
  // alongside the current buffer, then marks the pair for side-by-side
  // diff coloring. The diff is recomputed on demand (see
  // diff_status_for_window), not cached -- an O(N*M) LCS per call, which
  // is fine for the modest file sizes this is meant for, at the cost of
  // being wasteful to call every frame for huge files.
  void diff_with(const std::string& path);
  bool is_diffing() const { return diff_pair_.has_value(); }
  // The diff status for each line of the buffer shown in window
  // `window_index`, or empty if that window isn't part of the active diff.
  std::vector<DiffLineStatus> diff_status_for_window(size_t window_index) const;

  // Language server integration (currently: clangd for C++ files only).
  // Started lazily the first time a C++ file becomes current; silently
  // does nothing if clangd isn't installed. Call poll_lsp() once per
  // frame from the frontend to process incoming messages.
  bool lsp_running() const { return lsp_->running(); }
  void poll_lsp() { lsp_->poll(); }
  std::vector<LspDiagnostic> diagnostics() const { return lsp_->diagnostics_for(filename()); }
  // 'gd': jumps to the definition of the symbol under the cursor,
  // switching buffers if it's in a different file. No-op if clangd isn't
  // running or the request comes back empty.
  void go_to_definition();
  // 'K': asynchronously fetches hover text for the symbol under the
  // cursor; see hover_text() for the result once it arrives.
  void request_hover();
  const std::optional<std::string>& hover_text() const { return hover_text_; }
  void dismiss_hover() { hover_text_.reset(); }

  // Options (set via a config script's zedit.set_option(), or the :lua
  // command). Deliberately just one field rather than a generic key-value
  // bag -- add more fields here as real options accumulate.
  int tabstop() const { return tabstop_; }
  void set_tabstop(int n) { tabstop_ = std::max(1, n); }
  void set_normal_remap(const std::unordered_map<char, std::string>& remap) {
    mode_sm_.set_normal_remap(remap);
  }

 private:
  struct UndoEntry {
    PieceTable::Snapshot buffer_snapshot;
    Cursor cursor;
  };

  struct Buffer {
    PieceTable content;
    std::string filename;
    bool dirty = false;
    std::vector<UndoEntry> undo_stack;
    std::vector<UndoEntry> redo_stack;
    std::unique_ptr<Highlighter> highlighter = std::make_unique<PlainHighlighter>();
    // Where a window's cursor was the last time it looked at this buffer,
    // so switching back to it (via :e, :bn/:bp, or a tab click) returns
    // you to where you left off, matching vim.
    Cursor last_cursor;
  };

  struct Window {
    size_t buffer_index = 0;
    Cursor cursor;
  };

  struct DiffPair {
    size_t buffer_a;
    size_t buffer_b;
  };

  std::vector<Buffer> buffers_ = std::vector<Buffer>(1);
  std::vector<Window> windows_ = std::vector<Window>(1);
  size_t current_window_ = 0;
  SplitLayout split_layout_ = SplitLayout::Single;
  std::optional<DiffPair> diff_pair_;
  ModeStateMachine mode_sm_;
  bool should_quit_ = false;
  RegisterContent unnamed_register_;
  std::array<RegisterContent, 26> named_registers_;
  std::string last_search_pattern_;
  bool last_search_forward_ = true;
  // Boxed for the same reason JsonRpcTransport is boxed inside LspManager:
  // LspManager's own pending-request callbacks capture `this` (LspManager*),
  // so its address must never change even when Editor itself is moved (e.g.
  // Editor::open_file()'s return value being moved into App's member).
  std::unique_ptr<LspManager> lsp_ = std::make_unique<LspManager>();
  std::optional<std::string> hover_text_;
  int tabstop_ = 4;

  Window& cur_window() { return windows_[current_window_]; }
  const Window& cur_window() const { return windows_[current_window_]; }
  Buffer& cur_buffer() { return buffers_[cur_window().buffer_index]; }
  const Buffer& cur_buffer() const { return buffers_[cur_window().buffer_index]; }
  void do_split(SplitLayout requested);
  void switch_window_to_buffer(size_t index);
  void maybe_start_lsp_for_current_buffer();
  void mark_dirty() {
    cur_buffer().dirty = true;
    cur_buffer().highlighter->set_text(cur_buffer().content.to_string());
    if (lsp_->running() && is_cpp_filename(cur_buffer().filename)) {
      lsp_->change_document(cur_buffer().filename, cur_buffer().content.to_string());
    }
  }
};

}  // namespace zedit::core
