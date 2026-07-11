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
  return ExCommand{ExCommandKind::Unknown};
}

}  // namespace zedit::core
