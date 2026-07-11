#pragma once

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

enum class EditorAction { None, Quit };

struct KeyResult {
  EditorAction action = EditorAction::None;
};

}  // namespace zedit::core
