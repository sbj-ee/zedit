#include "zedit/core/subprocess.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

namespace zedit::core {

namespace {

// A write to a pipe whose reader has exited raises SIGPIPE, whose default
// action kills the whole editor (exit 141) -- e.g. clangd crashing while a
// didChange is in flight. With it ignored, write() fails with EPIPE
// instead, which Subprocess::write() already reports. Done once, process
// wide; the frontend's main() does the same at startup.
void ignore_sigpipe_once() {
  static std::once_flag once;
  std::call_once(once, [] { ::signal(SIGPIPE, SIG_IGN); });
}

bool make_pipe_cloexec(int fds[2]) {
  // macOS has no pipe2(); set FD_CLOEXEC straight after. The window is only
  // a concern for another thread's fork, and the child side re-dup2s the
  // ends it needs (dup2 clears FD_CLOEXEC) before exec anyway.
  if (::pipe(fds) != 0) {
    return false;
  }
  for (int i = 0; i < 2; ++i) {
    if (::fcntl(fds[i], F_SETFD, FD_CLOEXEC) != 0) {
      ::close(fds[0]);
      ::close(fds[1]);
      return false;
    }
  }
  return true;
}

void close_pair(int fds[2]) {
  ::close(fds[0]);
  ::close(fds[1]);
}

}  // namespace

Subprocess::~Subprocess() { stop(); }

bool Subprocess::start(const std::string& command, const std::vector<std::string>& args) {
  if (running()) {
    return false;
  }
  ignore_sigpipe_once();

  // Everything the child needs is built before fork(): after fork() in a
  // multithreaded process only async-signal-safe calls are allowed, so no
  // allocation in the child.
  std::vector<char*> argv;
  argv.reserve(args.size() + 2);
  argv.push_back(const_cast<char*>(command.c_str()));
  for (const std::string& a : args) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);

  int stdin_pipe[2];   // [0]=read (child), [1]=write (parent)
  int stdout_pipe[2];  // [0]=read (parent), [1]=write (child)
  int exec_err[2];     // child reports exec failure (errno) here
  if (!make_pipe_cloexec(stdin_pipe)) {
    return false;
  }
  if (!make_pipe_cloexec(stdout_pipe)) {
    close_pair(stdin_pipe);
    return false;
  }
  if (!make_pipe_cloexec(exec_err)) {
    close_pair(stdin_pipe);
    close_pair(stdout_pipe);
    return false;
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    close_pair(stdin_pipe);
    close_pair(stdout_pipe);
    close_pair(exec_err);
    return false;
  }

  if (pid == 0) {
    // Child: async-signal-safe calls only. The parent ignores SIGPIPE; the
    // server should get the normal default.
    ::signal(SIGPIPE, SIG_DFL);
    ::dup2(stdin_pipe[0], STDIN_FILENO);
    ::dup2(stdout_pipe[1], STDOUT_FILENO);
    // All original pipe fds are FD_CLOEXEC, so exec closes them; exec_err[1]
    // closing on a successful exec is what tells the parent it worked.
    ::execvp(argv[0], argv.data());
    int err = errno;
    ssize_t ignored = ::write(exec_err[1], &err, sizeof err);
    (void)ignored;
    ::_exit(127);
  }

  // Parent: keep our ends, close the child's.
  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  ::close(exec_err[1]);

  int child_errno = 0;
  ssize_t n = 0;
  do {
    n = ::read(exec_err[0], &child_errno, sizeof child_errno);
  } while (n < 0 && errno == EINTR);
  ::close(exec_err[0]);
  if (n > 0) {
    // exec failed (e.g. clangd not installed): reap the child and report it.
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return false;
  }

  pid_ = pid;
  exited_ = false;
  stdin_fd_ = stdin_pipe[1];
  stdout_fd_.store(stdout_pipe[0]);
  return true;
}

bool Subprocess::running() const {
  if (pid_ <= 0 || exited_) {
    return false;
  }
  int status = 0;
  pid_t r = ::waitpid(pid_, &status, WNOHANG);
  if (r == pid_) {
    exited_ = true;  // reaped here; stop() must not wait for it again
    return false;
  }
  return true;
}

void Subprocess::terminate() {
  if (stdin_fd_ >= 0) {
    ::close(stdin_fd_);
    stdin_fd_ = -1;
  }
  if (pid_ > 0) {
    if (!exited_) {
      ::kill(pid_, SIGTERM);
      int status = 0;
      while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
      }
    }
    pid_ = -1;
    exited_ = false;
  }
}

void Subprocess::stop() {
  // Ends the child first: its exit closes the write end of the stdout
  // pipe, so a reader blocked in read_some() sees EOF and returns before
  // the fd is closed here (closing an fd another thread is reading is a
  // race). JsonRpcTransport joins its reader between the two steps.
  terminate();
  int fd = stdout_fd_.exchange(-1);
  if (fd >= 0) {
    ::close(fd);
  }
}

bool Subprocess::write(const std::string& data) {
  if (stdin_fd_ < 0) {
    return false;
  }
  size_t total_written = 0;
  while (total_written < data.size()) {
    ssize_t n = ::write(stdin_fd_, data.data() + total_written, data.size() - total_written);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;  // EPIPE once the child has exited (SIGPIPE is ignored)
    }
    total_written += static_cast<size_t>(n);
  }
  return true;
}

std::string Subprocess::read_some(size_t max_bytes) {
  int fd = stdout_fd_.load();
  if (fd < 0) {
    return {};
  }
  std::string buffer(max_bytes, '\0');
  ssize_t n = 0;
  do {
    n = ::read(fd, buffer.data(), buffer.size());
  } while (n < 0 && errno == EINTR);

  if (n <= 0) {
    return {};
  }
  buffer.resize(static_cast<size_t>(n));
  return buffer;
}

}  // namespace zedit::core
