#include "zedit/core/config.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>

#include "zedit/core/file_io.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace zedit::core {

namespace {

ConfigResult* result_from_upvalue(lua_State* L) {
  return static_cast<ConfigResult*>(lua_touserdata(L, lua_upvalueindex(1)));
}

bool parse_hex_color(const std::string& hex, RgbColor& out) {
  std::string s = hex;
  if (!s.empty() && s[0] == '#') {
    s = s.substr(1);
  }
  if (s.size() != 6) {
    return false;
  }
  for (char c : s) {
    if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  out.r = static_cast<uint8_t>(std::stoul(s.substr(0, 2), nullptr, 16));
  out.g = static_cast<uint8_t>(std::stoul(s.substr(2, 2), nullptr, 16));
  out.b = static_cast<uint8_t>(std::stoul(s.substr(4, 2), nullptr, 16));
  return true;
}

// zedit.set_option(name, value) -- only "tabstop" is recognized today.
// Unknown names are recorded as a soft error rather than a Lua error, so
// one typo doesn't abort the rest of the config script.
int l_set_option(lua_State* L) {
  ConfigResult* result = result_from_upvalue(L);
  const char* name = luaL_checkstring(L, 1);
  if (std::string(name) == "tabstop") {
    result->options.tabstop = static_cast<int>(luaL_checkinteger(L, 2));
  } else {
    result->errors.push_back(std::string("zedit.set_option: unknown option '") + name + "'");
  }
  return 0;
}

// zedit.set_color(name, "#rrggbb") -- names match the token/severity kinds
// the frontend's theme table already uses (see theme.cpp).
int l_set_color(lua_State* L) {
  ConfigResult* result = result_from_upvalue(L);
  const char* name = luaL_checkstring(L, 1);
  const char* hex = luaL_checkstring(L, 2);
  RgbColor color;
  if (parse_hex_color(hex, color)) {
    result->colors[name] = color;
  } else {
    result->errors.push_back(std::string("zedit.set_color: invalid hex color '") + hex +
                              "' for '" + name + "'");
  }
  return 0;
}

// zedit.map(mode, lhs, rhs) -- non-recursive remap of a single Normal-mode
// key to a literal sequence of keys (vim's :noremap, not :map). Only mode
// "n" is supported; lhs must be exactly one character. This is deliberately
// small: a full leader-key/multi-key-lhs/other-mode remapping system is a
// much bigger feature than "minimal scripting hook" calls for.
int l_map(lua_State* L) {
  ConfigResult* result = result_from_upvalue(L);
  const char* mode = luaL_checkstring(L, 1);
  const char* lhs = luaL_checkstring(L, 2);
  const char* rhs = luaL_checkstring(L, 3);
  if (std::string(mode) != "n") {
    result->errors.push_back(std::string("zedit.map: unsupported mode '") + mode +
                              "' (only \"n\" is supported)");
    return 0;
  }
  if (std::string(lhs).size() != 1) {
    result->errors.push_back(std::string("zedit.map: lhs must be a single character, got '") +
                              lhs + "'");
    return 0;
  }
  result->normal_remap[lhs[0]] = rhs;
  return 0;
}

void register_api(lua_State* L, ConfigResult* result) {
  lua_newtable(L);

  auto push_fn = [&](const char* name, lua_CFunction fn) {
    lua_pushlightuserdata(L, result);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
  };
  push_fn("set_option", l_set_option);
  push_fn("set_color", l_set_color);
  push_fn("map", l_map);

  lua_setglobal(L, "zedit");
}

}  // namespace

ConfigResult eval_lua(const std::string& lua_source) {
  ConfigResult result;
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  register_api(L, &result);

  if (luaL_dostring(L, lua_source.c_str()) != LUA_OK) {
    const char* msg = lua_tostring(L, -1);
    result.errors.emplace_back(msg != nullptr ? msg : "unknown Lua error");
    lua_pop(L, 1);
  }
  lua_close(L);
  return result;
}

ConfigResult load_config_file(const std::string& path) {
  if (path.empty() || !std::filesystem::exists(path)) {
    return ConfigResult{};
  }
  std::string source;
  try {
    source = read_file(path);
  } catch (const FileIoError&) {
    return ConfigResult{};
  }
  return eval_lua(source);
}

std::string default_config_path() {
  if (const char* env = std::getenv("ZEDIT_CONFIG"); env != nullptr && *env != '\0') {
    return env;
  }
  std::string base;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    base = xdg;
  } else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    base = std::string(home) + "/.config";
  }
  if (base.empty()) {
    return "";
  }
  return base + "/zedit/init.lua";
}

}  // namespace zedit::core
