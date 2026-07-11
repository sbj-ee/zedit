#include <catch2/catch_test_macros.hpp>

#include "zedit/core/subprocess.hpp"

using zedit::core::Subprocess;

TEST_CASE("Subprocess spawns a real process and reads its output", "[subprocess]") {
  Subprocess proc;
  REQUIRE(proc.start("/bin/echo", {"hello", "subprocess"}));
  REQUIRE(proc.running());

  std::string output;
  for (int i = 0; i < 10 && output.find('\n') == std::string::npos; ++i) {
    output += proc.read_some();
  }
  REQUIRE(output == "hello subprocess\n");
}

TEST_CASE("Subprocess start fails cleanly for a nonexistent command", "[subprocess]") {
  Subprocess proc;
  // execvp failing in the child still returns true from start() (fork
  // succeeded); the child just exits immediately. What must hold is that
  // read_some() reflects that -- no output, pipe closes.
  REQUIRE(proc.start("/no/such/binary/zedit_test", {}));
  std::string output = proc.read_some();
  REQUIRE(output.empty());
}

TEST_CASE("Subprocess round-trips data through a cat echo", "[subprocess]") {
  Subprocess proc;
  REQUIRE(proc.start("/bin/cat", {}));
  REQUIRE(proc.write("ping\n"));

  std::string output;
  for (int i = 0; i < 10 && output.find('\n') == std::string::npos; ++i) {
    output += proc.read_some();
  }
  REQUIRE(output == "ping\n");
  proc.stop();
  REQUIRE_FALSE(proc.running());
}
