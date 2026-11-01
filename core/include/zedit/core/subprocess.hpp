#pragma once

#include <atomic>
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
  // failure, including when exec fails in the child (e.g. command not
  // found): the child reports its errno over a close-on-exec pipe.
  // Also ignores SIGPIPE process-wide (once), so writing to a child that
  // has died fails with EPIPE instead of killing the editor.
  bool start(const std::string& command, const std::vector<std::string>& args);
  // Terminates the process if running and closes the pipes.
  void stop();
  // Closes stdin and ends/reaps the child but leaves the stdout pipe open,
  // so a reader thread blocked in read_some() sees EOF and can be joined
  // before stop() closes the fd.
  void terminate();
  // True only while the child is alive (a child that has exited is reaped
  // here, non-blocking).
  bool running() const;

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
  mutable bool exited_ = false;
  int stdin_fd_ = -1;
  std::atomic<int> stdout_fd_{-1};
};

}  // namespace zedit::core
