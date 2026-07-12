#pragma once

#include <string>
#include <string_view>

namespace zedit::core {

enum class ExCommandKind {
  Empty,
  Write,
  Quit,
  WriteQuit,
  ForceQuit,
  Edit,
  NextBuffer,
  PrevBuffer,
  ListBuffers,
  CloseBuffer,
  SplitHorizontal,
  SplitVertical,
  CloseWindow,
  Diff,
  Lua,
  GotoLine,
  Unknown,
};

struct ExCommand {
  ExCommandKind kind = ExCommandKind::Unknown;
  std::string argument;  // e.g. the path for :e or :diff, the code for :lua,
                          // or the line number (as digits) for :N
};

// Parses a hand-rolled subset of vim's Ex commands:
// :w :q :wq :x :q! :e <path> :bn :bp :ls :bd/:bdelete
// :sp :vsp/:vs :close/:clo :diff <path> :lua <code> :N (goto line N)
ExCommand parse_ex_command(std::string_view input);

}  // namespace zedit::core
