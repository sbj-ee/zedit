#pragma once

#include <cstdint>
#include <vector>

#include "zedit/core/utf8.hpp"

namespace zedit::core {

enum class Mode { Normal, Insert, CommandLine, Visual, VisualLine, Search };

enum class OperatorKind { Delete, Yank, Change };

// Vim: the modal editing this project is built around. Gedit: a plain
// "always behaves like a text box" alternative for anyone who doesn't
// want modal editing at all -- typing always inserts immediately, no
// Normal-mode command grammar, selection is keyboard (Shift+Arrow) or
// mouse (double-click, drag) driven and typing over a selection replaces
// it. See ModeStateMachine::handle_gedit_key().
enum class EditingStyle { Vim, Gedit };

enum class Key {
  Char,
  Enter,
  Escape,
  Backspace,
  Tab,
  Left,
  Right,
  Up,
  Down,
  ShiftLeft,
  ShiftRight,
  ShiftUp,
  ShiftDown,
  CtrlR,
  CtrlW,
  CtrlA,
  CtrlP,
  CtrlC,
  CtrlX,
  CtrlS,
  Unknown,
};

struct KeyEvent {
  Key key = Key::Unknown;
  char ch = 0;  // valid when key == Key::Char
};

// A typed character as Key::Char events: one for ASCII, one per UTF-8 byte
// for anything else (so "é" inserts both of its bytes). 0, surrogates and
// values past U+10FFFF yield nothing.
inline std::vector<KeyEvent> char_key_events(uint32_t codepoint) {
  std::vector<KeyEvent> out;
  if (codepoint == 0 || codepoint > 0x10FFFF) return out;
  if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return out;  // lone surrogate
  for (char byte : utf8_encode(codepoint)) out.push_back(KeyEvent{Key::Char, byte});
  return out;
}

enum class EditorAction { None, Quit };

struct KeyResult {
  EditorAction action = EditorAction::None;
};

}  // namespace zedit::core
