#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zedit::core {

struct RgbColor {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

// Fields are optional rather than defaulted, so a ConfigResult that never
// touched an option (e.g. a ":lua" snippet that only calls zedit.map())
// can be distinguished from one that explicitly set it back to the
// default -- callers should only apply a field to the Editor when it's
// present, not overwrite existing state with a phantom default every time.
struct Options {
  // Spaces inserted by the Tab key in Insert mode.
  std::optional<int> tabstop;
};

// Everything a config script can produce. Colors are keyed by the same
// names theme.cpp's TokenKind/DiagnosticSeverity map to (e.g. "comment",
// "string", "keyword", "error", ...) -- core stays GUI-free and doesn't
// know what an ImU32 is, so the frontend is responsible for interpreting
// this map against its own theme table.
struct ConfigResult {
  Options options;
  std::unordered_map<char, std::string> normal_remap;
  std::unordered_map<std::string, RgbColor> colors;
  // Lua syntax/runtime errors, or bad argument errors from zedit.* calls.
  // Non-fatal: whatever the script managed to set before erroring still
  // applies, matching how a typo partway through a vimrc doesn't undo
  // earlier lines.
  std::vector<std::string> errors;
};

// Runs `lua_source` against a fresh Lua state with the `zedit` API table
// (set_option/map/set_color) registered, and collects the results. Used
// both for loading a config file and for the ":lua" command.
ConfigResult eval_lua(const std::string& lua_source);

// Loads and evaluates the config file at `path`. A missing file is not an
// error -- it just means "no config," matching vim's handling of a missing
// vimrc -- and returns a default-constructed ConfigResult.
ConfigResult load_config_file(const std::string& path);

// $ZEDIT_CONFIG if set, else $XDG_CONFIG_HOME/zedit/init.lua, else
// $HOME/.config/zedit/init.lua.
std::string default_config_path();

}  // namespace zedit::core
