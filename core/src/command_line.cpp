#include "zedit/core/command_line.hpp"

namespace zedit::core {

ExCommand parse_ex_command(std::string_view input) {
  while (!input.empty() && input.front() == ' ') {
    input.remove_prefix(1);
  }
  while (!input.empty() && input.back() == ' ') {
    input.remove_suffix(1);
  }

  if (input.empty()) {
    return ExCommand{ExCommandKind::Empty};
  }
  if (input == "w") {
    return ExCommand{ExCommandKind::Write};
  }
  if (input == "q") {
    return ExCommand{ExCommandKind::Quit};
  }
  if (input == "q!") {
    return ExCommand{ExCommandKind::ForceQuit};
  }
  if (input == "wq" || input == "x") {
    return ExCommand{ExCommandKind::WriteQuit};
  }
  if (input == "bn") {
    return ExCommand{ExCommandKind::NextBuffer};
  }
  if (input == "bp") {
    return ExCommand{ExCommandKind::PrevBuffer};
  }
  if (input == "ls") {
    return ExCommand{ExCommandKind::ListBuffers};
  }
  if (input == "sp") {
    return ExCommand{ExCommandKind::SplitHorizontal};
  }
  if (input == "vsp" || input == "vs") {
    return ExCommand{ExCommandKind::SplitVertical};
  }
  if (input == "close" || input == "clo") {
    return ExCommand{ExCommandKind::CloseWindow};
  }
  if (input[0] == 'e' && (input.size() == 1 || input[1] == ' ')) {
    ExCommand cmd{ExCommandKind::Edit, {}};
    std::string_view rest = input.substr(1);
    while (!rest.empty() && rest.front() == ' ') {
      rest.remove_prefix(1);
    }
    cmd.argument = std::string(rest);
    return cmd;
  }
  constexpr std::string_view kDiffPrefix = "diff";
  if (input.size() > kDiffPrefix.size() && input.substr(0, kDiffPrefix.size()) == kDiffPrefix &&
      input[kDiffPrefix.size()] == ' ') {
    ExCommand cmd{ExCommandKind::Diff, {}};
    std::string_view rest = input.substr(kDiffPrefix.size());
    while (!rest.empty() && rest.front() == ' ') {
      rest.remove_prefix(1);
    }
    cmd.argument = std::string(rest);
    return cmd;
  }
  constexpr std::string_view kLuaPrefix = "lua";
  if (input.size() > kLuaPrefix.size() && input.substr(0, kLuaPrefix.size()) == kLuaPrefix &&
      input[kLuaPrefix.size()] == ' ') {
    ExCommand cmd{ExCommandKind::Lua, {}};
    std::string_view rest = input.substr(kLuaPrefix.size());
    while (!rest.empty() && rest.front() == ' ') {
      rest.remove_prefix(1);
    }
    cmd.argument = std::string(rest);
    return cmd;
  }
  return ExCommand{ExCommandKind::Unknown};
}

}  // namespace zedit::core
