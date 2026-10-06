#include "wl_paste_clipboard.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <sstream>

extern char** environ;

namespace zedit::frontend {

namespace {
using Clock = std::chrono::steady_clock;
}

bool program_on_path(const std::string& name) {
  if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0;
  const char* path = std::getenv("PATH");
  if (path == nullptr) return false;
  std::stringstream dirs(path);
  std::string dir;
  while (std::getline(dirs, dir, ':')) {
    if (dir.empty()) dir = ".";
    if (access((dir + "/" + name).c_str(), X_OK) == 0) return true;
  }
  return false;
}

WlPasteClipboard::Run WlPasteClipboard::run(const std::vector<std::string>& args) {
  Run result;
  if (program_missing_) return result;
  if (!program_on_path(options_.program)) {
    program_missing_ = true;
    return result;
  }

  int out_pipe[2];
  if (pipe2(out_pipe, O_CLOEXEC) != 0) return result;

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

  std::vector<std::string> argv_storage;
  argv_storage.push_back(options_.program);
  argv_storage.insert(argv_storage.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (std::string& a : argv_storage) argv.push_back(a.data());
  argv.push_back(nullptr);

  pid_t pid = -1;
  int rc = posix_spawnp(&pid, options_.program.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(out_pipe[1]);
  if (rc != 0) {
    close(out_pipe[0]);
    if (rc == ENOENT) program_missing_ = true;
    return result;
  }
  result.started = true;

  const auto deadline = Clock::now() + std::chrono::milliseconds(options_.timeout_ms);
  char buf[65536];
  bool too_big = false;
  for (;;) {
    auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (remaining <= 0) {
      result.timed_out = true;
      break;
    }
    pollfd pfd{out_pipe[0], POLLIN, 0};
    int prc = poll(&pfd, 1, static_cast<int>(remaining));
    if (prc < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (prc == 0) continue;  // re-checks the deadline
    ssize_t n = ::read(out_pipe[0], buf, sizeof buf);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;  // EOF: wl-paste is done
    result.out.append(buf, static_cast<size_t>(n));
    if (result.out.size() > options_.max_bytes) {
      too_big = true;
      break;
    }
  }
  close(out_pipe[0]);

  if (result.timed_out || too_big) kill(pid, SIGKILL);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (!result.timed_out && !too_big && WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
  return result;
}

std::optional<std::vector<std::string>> WlPasteClipboard::targets() {
  Run r = run({"--list-types"});
  // Non-zero exit: "Nothing is copied", or no Wayland compositor at all.
  if (!r.started || r.timed_out || r.exit_code != 0) return std::nullopt;
  std::vector<std::string> types;
  std::stringstream lines(r.out);
  std::string line;
  while (std::getline(lines, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) types.push_back(line);
  }
  if (types.empty()) return std::nullopt;
  return types;
}

WlPasteClipboard::Read WlPasteClipboard::read(const std::string& type) {
  Read result;
  Run r = run({"--no-newline", "--type", type});
  if (r.timed_out) {
    result.status = ReadStatus::timed_out;
  } else if (r.started && r.exit_code == 0) {
    result.status = ReadStatus::ok;
    result.data = std::move(r.out);
  }
  return result;
}

}  // namespace zedit::frontend
