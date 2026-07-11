#include <catch2/catch_test_macros.hpp>

#include <string>

#include "zedit/core/editor.hpp"
#include "zedit/core/motion.hpp"
#include "zedit/core/operator.hpp"
#include "zedit/core/piece_table.hpp"

using zedit::core::Cursor;
using zedit::core::Editor;
using zedit::core::execute_delete_char;
using zedit::core::execute_operator_charwise;
using zedit::core::execute_operator_linewise;
using zedit::core::execute_paste;
using zedit::core::OperatorKind;
using zedit::core::PieceTable;
using zedit::core::Range;

TEST_CASE("charwise delete removes the range and yanks it to the register", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar baz"));
  execute_operator_charwise(OperatorKind::Delete, Range{4, 8}, ed);
  REQUIRE(ed.buffer().to_string() == "foo baz");
  REQUIRE(ed.unnamed_register().text == "bar ");
  REQUIRE(ed.unnamed_register().linewise == false);
  REQUIRE(ed.cursor().col == 4);
}

TEST_CASE("charwise yank does not mutate the buffer", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar"));
  execute_operator_charwise(OperatorKind::Yank, Range{0, 3}, ed);
  REQUIRE(ed.buffer().to_string() == "foo bar");
  REQUIRE(ed.unnamed_register().text == "foo");
  REQUIRE(ed.dirty() == false);
}

TEST_CASE("charwise change deletes and leaves the cursor at the deletion point", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("foo bar"));
  execute_operator_charwise(OperatorKind::Change, Range{4, 7}, ed);
  REQUIRE(ed.buffer().to_string() == "foo ");
  REQUIRE(ed.cursor().col == 4);  // one past the last remaining char
}

TEST_CASE("linewise delete (dd) removes whole lines including the newline", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc\nd"));
  execute_operator_linewise(OperatorKind::Delete, 1, 2, ed);  // dd on "b","c"
  REQUIRE(ed.buffer().to_string() == "a\nd");
  REQUIRE(ed.unnamed_register().text == "b\nc\n");
  REQUIRE(ed.unnamed_register().linewise);
  REQUIRE(ed.cursor().line == 1);
  REQUIRE(ed.cursor().col == 0);
}

TEST_CASE("linewise delete through the last line leaves no dangling blank line", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  execute_operator_linewise(OperatorKind::Delete, 1, 2, ed);  // dd on "b","c" (last line)
  REQUIRE(ed.buffer().to_string() == "a");
  REQUIRE(ed.cursor().line == 0);
}

TEST_CASE("linewise yank (yy) always yields register text ending in a newline", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));  // "c" has no trailing newline
  execute_operator_linewise(OperatorKind::Yank, 2, 1, ed);
  REQUIRE(ed.unnamed_register().text == "c\n");
  REQUIRE(ed.buffer().to_string() == "a\nb\nc");  // unchanged
}

TEST_CASE("linewise change (cc) in the middle leaves one empty line to type into", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  execute_operator_linewise(OperatorKind::Change, 1, 1, ed);  // cc on "b"
  REQUIRE(ed.buffer().to_string() == "a\n\nc");
  REQUIRE(ed.cursor().line == 1);
  REQUIRE(ed.cursor().col == 0);
}

TEST_CASE("linewise change on the last line leaves an empty trailing line", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb\nc"));
  execute_operator_linewise(OperatorKind::Change, 2, 1, ed);  // cc on "c" (last line)
  REQUIRE(ed.buffer().to_string() == "a\nb\n");
  REQUIRE(ed.buffer().line_count() == 3);
  REQUIRE(ed.cursor().line == 2);
  REQUIRE(ed.cursor().col == 0);
}

TEST_CASE("x deletes characters under the cursor without crossing the line end", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("abc\nd"));
  ed.set_cursor(Cursor{0, 1});
  execute_delete_char(10, ed);  // count exceeds remaining chars on the line
  REQUIRE(ed.buffer().to_string() == "a\nd");
  REQUIRE(ed.unnamed_register().text == "bc");
}

TEST_CASE("charwise paste after inserts following the cursor and repeats by count", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("Xy"));
  ed.set_cursor(Cursor{0, 0});
  ed.set_unnamed_register("ab", false);
  execute_paste(/*before=*/false, 2, ed);
  REQUIRE(ed.buffer().to_string() == "Xababy");
  REQUIRE(ed.cursor().col == 4);  // last character of the pasted block
}

TEST_CASE("charwise paste before inserts a single repeated block in order", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("Xy"));
  ed.set_cursor(Cursor{0, 0});
  ed.set_unnamed_register("ab", false);
  execute_paste(/*before=*/true, 2, ed);
  REQUIRE(ed.buffer().to_string() == "ababXy");
}

TEST_CASE("linewise paste after inserts below and stacks in order", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb"));
  ed.set_cursor(Cursor{0, 0});
  ed.set_unnamed_register("x\n", true);
  execute_paste(/*before=*/false, 1, ed);
  REQUIRE(ed.buffer().to_string() == "a\nx\nb");
  REQUIRE(ed.cursor().line == 1);
}

TEST_CASE("linewise paste before inserts above", "[operator]") {
  Editor ed;
  ed.buffer() = PieceTable(std::string("a\nb"));
  ed.set_cursor(Cursor{1, 0});
  ed.set_unnamed_register("x\n", true);
  execute_paste(/*before=*/true, 1, ed);
  REQUIRE(ed.buffer().to_string() == "a\nx\nb");
  REQUIRE(ed.cursor().line == 1);
}
