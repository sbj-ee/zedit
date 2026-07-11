#include "zedit/core/subprocess.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace zedit::core {

Subprocess::~Subprocess() { stop(); }

bool Subprocess::start(const std::string& command, const std::vector<std::string>& args) {
  if (running()) {
    return false;
  }

  int stdin_pipe[2];   // [0]=read (child), [1]=write (parent)
  int stdout_pipe[2];  // [0]=read (parent), [1]=write (child)
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // Child: wire stdin/stdout to the pipes, inherit stderr, then exec.
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(command.c_str()));
    for (const std::string& a : args) {
      argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    execvp(command.c_str(), argv.data());
    _exit(127);  // exec failed
  }

  // Parent: keep our ends, close the child's.
  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  pid_ = pid;
  stdin_fd_ = stdin_pipe[1];
  stdout_fd_ = stdout_pipe[0];
  return true;
}

void Subprocess::stop() {
  if (stdin_fd_ >= 0) {
    close(stdin_fd_);
    stdin_fd_ = -1;
  }
  if (stdout_fd_ >= 0) {
    close(stdout_fd_);
    stdout_fd_ = -1;
  }
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    int status = 0;
    waitpid(pid_, &status, 0);
    pid_ = -1;
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
      return false;
    }
    total_written += static_cast<size_t>(n);
  }
  return true;
}

std::string Subprocess::read_some(size_t max_bytes) {
  if (stdout_fd_ < 0) {
    return {};
  }
  std::string buffer(max_bytes, '\0');
  ssize_t n = 0;
  do {
    n = ::read(stdout_fd_, buffer.data(), buffer.size());
  } while (n < 0 && errno == EINTR);

  if (n <= 0) {
    return {};
  }
  buffer.resize(static_cast<size_t>(n));
  return buffer;
}

}  // namespace zedit::core
