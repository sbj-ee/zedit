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
  SplitHorizontal,
  SplitVertical,
  CloseWindow,
  Diff,
  Lua,
  Unknown,
};

struct ExCommand {
  ExCommandKind kind = ExCommandKind::Unknown;
  std::string argument;  // e.g. the path for :e or :diff, or the code for :lua
};

// Parses a hand-rolled subset of vim's Ex commands:
// :w :q :wq :x :q! :e <path> :bn :bp :ls
// :sp :vsp/:vs :close/:clo :diff <path> :lua <code>
ExCommand parse_ex_command(std::string_view input);

}  // namespace zedit::core
