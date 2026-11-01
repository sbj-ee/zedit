#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

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
  // execvp failing in the child is reported back over the exec-error pipe,
  // so start() returns false and nothing claims to be running.
  REQUIRE_FALSE(proc.start("/no/such/binary/zedit_test", {}));
  REQUIRE_FALSE(proc.running());
  REQUIRE(proc.read_some().empty());
  REQUIRE_FALSE(proc.write("x"));
}

TEST_CASE("Subprocess running() turns false once the child exits", "[subprocess]") {
  Subprocess proc;
  REQUIRE(proc.start("/bin/sh", {"-c", "exit 3"}));
  bool exited = false;
  for (int i = 0; i < 200 && !exited; ++i) {
    exited = !proc.running();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  REQUIRE(exited);
  proc.stop();  // must not hang waiting for an already-reaped child
}

TEST_CASE("Writing to a child that has exited fails instead of raising SIGPIPE",
          "[subprocess]") {
  Subprocess proc;
  // Closes its stdin and exits straight away, like a crashed server.
  REQUIRE(proc.start("/bin/sh", {"-c", "exec 0<&-; exit 0"}));
  for (int i = 0; i < 200 && proc.running(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // Bigger than any pipe buffer, so the write has to hit the closed reader.
  std::string big(1 << 20, 'x');
  bool ok = true;
  for (int i = 0; i < 4 && ok; ++i) {
    ok = proc.write(big);
  }
  REQUIRE_FALSE(ok);  // reached only if SIGPIPE didn't kill the test binary
}

namespace {
// Number of fds a freshly started child shell sees.
size_t child_fd_count() {
  Subprocess lister;
  REQUIRE(lister.start("/bin/sh", {"-c", "ls /proc/self/fd 2>/dev/null || ls /dev/fd"}));
  std::string out;
  for (std::string chunk = lister.read_some(); !chunk.empty(); chunk = lister.read_some()) {
    out += chunk;
  }
  size_t count = 0;
  for (char c : out) {
    if (c == '\n') ++count;
  }
  return count;
}
}  // namespace

TEST_CASE("Subprocess doesn't leak its pipe ends into later children", "[subprocess]") {
  // Compare against a baseline so fds inherited from the test runner
  // (CI keeps a few open) don't count.
  size_t baseline = child_fd_count();
  Subprocess a;
  REQUIRE(a.start("/bin/cat", {}));
  // Without CLOEXEC the second child would also hold a's pipe ends.
  REQUIRE(child_fd_count() == baseline);
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
