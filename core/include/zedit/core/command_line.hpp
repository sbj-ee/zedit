#pragma once

#include <string_view>

namespace zedit::core {

enum class ExCommandKind { Empty, Write, Quit, WriteQuit, ForceQuit, Unknown };

struct ExCommand {
  ExCommandKind kind = ExCommandKind::Unknown;
};

// Parses a hand-rolled subset of vim's Ex commands: :w :q :wq :x :q!
ExCommand parse_ex_command(std::string_view input);

}  // namespace zedit::core
