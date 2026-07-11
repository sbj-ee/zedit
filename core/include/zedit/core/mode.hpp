#pragma once

namespace zedit::core {

enum class Mode { Normal, Insert, CommandLine, Visual, VisualLine, Search };

enum class OperatorKind { Delete, Yank, Change };

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
  CtrlR,
  CtrlW,
  CtrlA,
  CtrlP,
  CtrlC,
  CtrlS,
  Unknown,
};

struct KeyEvent {
  Key key = Key::Unknown;
  char ch = 0;  // valid when key == Key::Char
};

enum class EditorAction { None, Quit };

struct KeyResult {
  EditorAction action = EditorAction::None;
};

}  // namespace zedit::core
