#pragma once

#include <string>
#include <vector>

namespace zedit::core {

// A child process connected via pipes to its stdin/stdout (stderr is
// inherited so server errors show up on zedit's own stderr for
// debugging). POSIX only (Linux/macOS, this project's target platforms)
// -- uses fork/exec/pipe directly since the C++ standard library has no
// subprocess support.
class Subprocess {
 public:
  Subprocess() = default;
  ~Subprocess();
  Subprocess(const Subprocess&) = delete;
  Subprocess& operator=(const Subprocess&) = delete;

  // Spawns `command` (searched via PATH) with `args`. Returns false on
  // failure (e.g. command not found).
  bool start(const std::string& command, const std::vector<std::string>& args);
  // Terminates the process if running and closes the pipes.
  void stop();
  bool running() const { return pid_ > 0; }

  // Writes raw bytes to the child's stdin. Returns false if the write
  // failed (e.g. the child has exited).
  bool write(const std::string& data);

  // Blocking read of up to `max_bytes` from the child's stdout. Returns
  // the bytes read, or an empty string once the pipe closes (child
  // exited) or on error. Meant to be called from a dedicated reader
  // thread, since it blocks until the child writes something.
  std::string read_some(size_t max_bytes = 4096);

 private:
  int pid_ = -1;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
};

}  // namespace zedit::core
