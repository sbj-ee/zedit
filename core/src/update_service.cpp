#include "zedit/core/update_service.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <sstream>

#include "zedit/core/minisign.hpp"
#include "zedit/core/sha256.hpp"

namespace zedit::core {

namespace fs = std::filesystem;

struct UpdateService::Shared {
  UpdateConfig config;
  std::atomic<UpdateStage> stage{UpdateStage::Idle};
  std::atomic<bool> cancel{false};
  mutable std::mutex mutex;  // guards everything below
  UpdateProgress progress;
  std::optional<UpdateCheckResult> check_result;
  std::optional<UpdateInstallResult> install_result;
};

namespace {

constexpr int64_t kMaxJsonBytes = 8LL * 1024 * 1024;
constexpr int64_t kMaxSmallAssetBytes = 1LL * 1024 * 1024;  // SHA256SUMS, .minisig
constexpr int64_t kMaxPackageBytes = 1024LL * 1024 * 1024;

struct ProcessOutput {
  bool started = false;
  bool cancelled = false;
  bool signaled = false;
  int exit_code = -1;
  std::string output;  // stdout (and stderr when merged), the last 64 KiB
};

// Runs argv (argv[0] looked up in PATH when it has no slash) without a shell,
// collecting its output. `tick` runs about every 100 ms; setting `cancel`
// terminates the process.
ProcessOutput run_process(const std::vector<std::string>& argv, const std::string& cwd,
                          const std::atomic<bool>& cancel, const std::function<void()>& tick) {
  ProcessOutput result;
  if (argv.empty()) return result;
  // Everything the child needs is prepared before fork(): between fork() and
  // exec() a child of a multi-threaded process may only call
  // async-signal-safe functions (no allocation).
  std::vector<char*> args;
  for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  const char* cwd_c = cwd.empty() ? nullptr : cwd.c_str();
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) return result;
  // Signals stay blocked across fork() so the child can't run one of this
  // process's own handlers (which assume this process's state) before it has
  // reset them to the defaults and exec'd.
  sigset_t all_signals;
  sigset_t previous_mask;
  sigfillset(&all_signals);
  pthread_sigmask(SIG_BLOCK, &all_signals, &previous_mask);
  pid_t pid = fork();
  if (pid < 0) {
    pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return result;
  }
  if (pid == 0) {
    for (int sig = 1; sig < NSIG; ++sig) {
      if (sig != SIGKILL && sig != SIGSTOP) signal(sig, SIG_DFL);
    }
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      close(devnull);
    }
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    if (cwd_c != nullptr && chdir(cwd_c) != 0) {
      _exit(126);
    }
    execvp(args[0], args.data());
    _exit(127);
  }
  pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
  close(pipe_fds[1]);
  result.started = true;
  int fd = pipe_fds[0];
  bool eof = false;
  int status = 0;
  bool reaped = false;
  while (!reaped) {
    if (cancel.load() && !result.cancelled) {
      result.cancelled = true;
      kill(pid, SIGTERM);
    }
    if (!eof) {
      struct pollfd pfd = {fd, POLLIN, 0};
      int n = poll(&pfd, 1, 100);
      if (n > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        char buf[4096];
        ssize_t got = read(fd, buf, sizeof buf);
        if (got > 0) {
          result.output.append(buf, static_cast<size_t>(got));
          if (result.output.size() > 65536) {
            result.output.erase(0, result.output.size() - 65536);
          }
        } else if (got == 0 || (got < 0 && errno != EINTR)) {
          eof = true;
        }
      }
    } else {
      usleep(50 * 1000);
    }
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
      reaped = true;
    } else if (w < 0 && errno != EINTR) {
      reaped = true;
      status = 0;
    }
    if (tick) tick();
  }
  // Anything still in the pipe after the exit.
  while (true) {
    char buf[4096];
    ssize_t got = read(fd, buf, sizeof buf);
    if (got <= 0) break;
    result.output.append(buf, static_cast<size_t>(got));
  }
  close(fd);
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.signaled = true;
  }
  return result;
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == '\n' || s[i] == '\r' || s[i] == ' ')) ++i;
  return s.substr(i);
}

std::string read_file(const std::string& path, int64_t max_bytes) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || static_cast<int64_t>(size) > max_bytes) return {};
  std::string data(static_cast<size_t>(size), '\0');
  std::ifstream in(path, std::ios::binary);
  if (!in || !in.read(data.data(), static_cast<std::streamsize>(data.size()))) return {};
  return data;
}

// Case-insensitive header value from curl's -D dump (the last response's block).
std::string header_value(const std::string& headers, const std::string& name) {
  std::string value;
  std::istringstream lines(headers);
  std::string line;
  const std::string want = name + ":";
  while (std::getline(lines, line)) {
    if (line.size() > want.size() &&
        strncasecmp(line.c_str(), want.c_str(), want.size()) == 0) {
      value = trim(line.substr(want.size()));
    }
  }
  return value;
}

struct FetchResult {
  bool ok = false;
  bool cancelled = false;
  bool rate_limited = false;
  int http_status = 0;
  std::string error;  // when !ok
};

class Job {
 public:
  explicit Job(std::shared_ptr<UpdateService::Shared> shared) : s_(std::move(shared)) {}

  void set_stage(UpdateStage stage) { s_->stage.store(stage); }
  const UpdateConfig& cfg() const { return s_->config; }

  // Downloads `url` into `dest` with curl. `what` names it in messages.
  FetchResult fetch(const std::string& url, const std::string& dest, const std::string& what,
                    const char* accept, int64_t max_bytes, int64_t size_hint, bool quick) {
    FetchResult r;
    const UpdateConfig& c = cfg();
    const std::string headers_path = dest + ".headers";
    std::vector<std::string> argv = {c.curl, "-sS", "-L", "--max-redirs", "5",
                                     "--connect-timeout", std::to_string(c.connect_timeout_s),
                                     "--max-filesize", std::to_string(max_bytes)};
    if (c.https_only) {
      argv.insert(argv.end(), {"--proto", "=https", "--proto-redir", "=https"});
    }
    if (quick) {
      argv.insert(argv.end(), {"--max-time", std::to_string(c.check_timeout_s)});
    } else {
      argv.insert(argv.end(), {"--speed-time", std::to_string(c.stall_timeout_s), "--speed-limit", "1"});
    }
    argv.insert(argv.end(), {"-A", "zedit/" + c.current_version, "-H", std::string("Accept: ") + accept,
                             "-D", headers_path, "-o", dest, "-w", "%{http_code}", "--", url});
    std::error_code ec;
    if (!quick) {
      std::lock_guard<std::mutex> lock(s_->mutex);
      s_->progress.asset = what;
      s_->progress.received = 0;
      s_->progress.total = size_hint > 0 ? size_hint : -1;
    }
    ProcessOutput out = run_process(argv, "", s_->cancel, [&]() {
      if (quick) return;
      std::error_code size_ec;
      auto bytes = fs::file_size(dest, size_ec);
      std::lock_guard<std::mutex> lock(s_->mutex);
      s_->progress.asset = what;
      s_->progress.received = size_ec ? 0 : static_cast<int64_t>(bytes);
      s_->progress.total = size_hint > 0 ? size_hint : -1;
    });
    const std::string headers = read_file(headers_path, 1 << 20);
    fs::remove(headers_path, ec);
    // curl's -w prints the status last; anything before it is curl's own message.
    std::string text = trim(out.output);
    std::string curl_message;
    if (text.size() >= 3 && std::all_of(text.end() - 3, text.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
      r.http_status = std::atoi(text.substr(text.size() - 3).c_str());
      curl_message = trim(text.substr(0, text.size() - 3));
    } else {
      curl_message = text;
    }
    if (!out.started || (out.exit_code == 127 && curl_message.empty())) {
      r.error = "curl could not be started, so zedit can't download updates. Install curl or "
                "get the release from its page.";
      return r;
    }
    if (out.cancelled) {
      r.cancelled = true;
      r.error = "Download cancelled.";
      fs::remove(dest, ec);
      return r;
    }
    if (out.signaled || out.exit_code != 0) {
      fs::remove(dest, ec);
      switch (out.exit_code) {
        case 6:
        case 7:
          r.error = quick ? "Couldn't reach GitHub to check for updates (no network?)."
                          : "Downloading " + what + " failed: couldn't reach GitHub (no network?).";
          break;
        case 28:
          r.error = quick ? "The update check timed out. Check the network connection and try again."
                          : "The download of " + what + " stalled. Check the network connection and try again.";
          break;
        case 35:
        case 51:
        case 58:
        case 59:
        case 60:
        case 77:
        case 83:
          r.error = "The secure connection to GitHub failed: " + curl_message;
          break;
        case 63:
          r.error = what + " is larger than zedit accepts, so it was not downloaded.";
          break;
        default:
          r.error = (quick ? "The update check failed: " : "Downloading " + what + " failed: ") +
                    (curl_message.empty() ? "curl exit " + std::to_string(out.exit_code) : curl_message);
          break;
      }
      return r;
    }
    if (r.http_status >= 400) {
      fs::remove(dest, ec);
      if (r.http_status == 403 || r.http_status == 429) {
        const bool exhausted = header_value(headers, "x-ratelimit-remaining") == "0" || r.http_status == 429;
        if (exhausted) {
          r.rate_limited = true;
          long long reset = std::atoll(header_value(headers, "x-ratelimit-reset").c_str());
          if (reset > 0) {
            std::time_t t = static_cast<std::time_t>(reset);
            std::tm local{};
            localtime_r(&t, &local);
            char hhmm[16];
            std::strftime(hhmm, sizeof hhmm, "%H:%M", &local);
            r.error = std::string("GitHub's rate limit for update checks was reached. Try again after ") + hhmm + ".";
          } else {
            r.error = "GitHub's rate limit for update checks was reached. Try again later.";
          }
        } else {
          r.error = "GitHub refused the update check (HTTP " + std::to_string(r.http_status) + ").";
        }
      } else if (r.http_status == 404) {
        r.error = quick ? "No published zedit release was found on GitHub."
                        : what + " is no longer on the release page.";
      } else {
        r.error = quick ? "GitHub answered the update check with HTTP " + std::to_string(r.http_status) + "."
                        : "Downloading " + what + " failed (HTTP " + std::to_string(r.http_status) + ").";
      }
      return r;
    }
    r.ok = true;
    return r;
  }

  UpdateCheckResult run_check(bool manual) {
    UpdateCheckResult result;
    result.manual = manual;
    std::error_code ec;
    fs::path dir = make_private_dir("zedit-check-");
    if (dir.empty()) {
      result.error = "Couldn't create a temporary folder for the update check.";
      return result;
    }
    const std::string body_path = (dir / "release.json").string();
    FetchResult f = fetch(cfg().api_url, body_path, "the release information",
                          "application/vnd.github+json", kMaxJsonBytes, -1, /*quick=*/true);
    if (!f.ok) {
      result.error = f.error;
      result.rate_limited = f.rate_limited;
      result.outcome = f.http_status == 404 ? UpdateCheckResult::Outcome::NoRelease
                                            : UpdateCheckResult::Outcome::Error;
    } else {
      std::string error;
      auto release = ReleaseInfo::from_json(read_file(body_path, kMaxJsonBytes), &error);
      if (!release) {
        result.error = error;
      } else {
        result.outcome = UpdateCheckResult::Outcome::Release;
        result.release = *release;
      }
    }
    fs::remove_all(dir, ec);
    return result;
  }

  UpdateInstallResult run_install(const ReleaseInfo& release) {
    UpdateInstallResult result;
    result.tag = release.tag;
    auto fail = [&result](std::string message) {
      result.outcome = UpdateInstallResult::Outcome::Failed;
      result.message = std::move(message);
      return result;
    };
    const std::string key_text = update_signing_public_key();
    if (key_text.empty()) {
      return fail("This build of zedit has no update-signing key, so it can't verify downloads "
                  "and won't install them. Download " + release.tag + " from the release page.");
    }
    const auto package = release.package_asset();
    const auto sums = release.checksum_asset();
    const auto sig = release.signature_asset();
    if (!package || !sums || !sig) {
      std::vector<std::string> missing;
      if (!package) missing.push_back(expected_package_name(release.version));
      if (!sums) missing.push_back("SHA256SUMS");
      if (!sig) missing.push_back("SHA256SUMS.minisig (its signature)");
      std::string joined;
      for (size_t i = 0; i < missing.size(); ++i) joined += (i == 0 ? "" : " and ") + missing[i];
      return fail("Release " + release.tag + " is missing " + joined +
                  ", so it can't be installed automatically.");
    }
    std::string key_error;
    auto key = MinisignPublicKey::parse(key_text, &key_error);
    if (!key) {
      return fail("The built-in update-signing key is invalid (" + key_error +
                  "), so nothing was installed.");
    }

    std::error_code ec;
    fs::path dir = make_private_dir("zedit-update-");
    if (dir.empty()) return fail("Couldn't create a temporary folder for the download.");
    auto cleanup_and = [&](UpdateInstallResult r) {
      fs::remove_all(dir, ec);
      return r;
    };

    set_stage(UpdateStage::Downloading);
    auto download = [&](const ReleaseAsset& a, int64_t max_bytes) -> FetchResult {
      return fetch(a.url, (dir / a.name).string(), a.name, "application/octet-stream", max_bytes,
                   a.size, /*quick=*/false);
    };
    auto failed_fetch = [&](const FetchResult& f) {
      if (f.cancelled) {
        result.outcome = UpdateInstallResult::Outcome::Cancelled;
        result.message = f.error;
      } else {
        result.outcome = UpdateInstallResult::Outcome::Failed;
        result.message = f.error;
      }
      return cleanup_and(result);
    };
    // Safe file names only (they come from the release JSON): no directories.
    for (const ReleaseAsset* a : {&*package, &*sums, &*sig}) {
      if (a->name.find('/') != std::string::npos || a->name.find("..") != std::string::npos) {
        return cleanup_and(fail("The release lists a file name zedit won't use (" + a->name + ")."));
      }
    }

    // 1. SHA256SUMS, 2. its signature, checked before the package is fetched.
    if (FetchResult f = download(*sums, kMaxSmallAssetBytes); !f.ok) return failed_fetch(f);
    if (FetchResult f = download(*sig, kMaxSmallAssetBytes); !f.ok) return failed_fetch(f);
    set_stage(UpdateStage::Verifying);
    const std::string sums_bytes = read_file((dir / sums->name).string(), kMaxSmallAssetBytes);
    const std::string sig_bytes = read_file((dir / sig->name).string(), kMaxSmallAssetBytes);
    MinisignResult verdict = verify_minisign(sums_bytes, sig_bytes, *key);
    if (!verdict.ok) {
      return cleanup_and(fail("The release's SHA256SUMS signature is not valid (" + verdict.error +
                              "), so nothing was downloaded or installed."));
    }
    // 3. The package, against the signed sums.
    set_stage(UpdateStage::Downloading);
    if (FetchResult f = download(*package, kMaxPackageBytes); !f.ok) return failed_fetch(f);
    set_stage(UpdateStage::Verifying);
    const std::string package_path = (dir / package->name).string();
    ChecksumResult check = verify_checksum(package_path, sums_bytes, package->name);
    if (!check.ok()) {
      fs::remove(package_path, ec);
      return cleanup_and(fail(check.message()));
    }
    const std::string verified_sha = check.expected;

    // 4. A private copy, re-hashed at the last moment, goes to the installer.
    fs::path stage_dir = make_private_dir("zedit-install-");
    if (stage_dir.empty()) {
      return fail_keep(result, package_path, "Couldn't create a private folder for the package.", dir);
    }
    const std::string staged = (stage_dir / package->name).string();
    fs::copy_file(package_path, staged, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      fs::remove_all(stage_dir, ec);
      return fail_keep(result, package_path, "Couldn't copy the package to a private folder.", dir);
    }
    chmod(staged.c_str(), 0644);
    if (sha256_file_hex(staged) != verified_sha) {
      fs::remove_all(stage_dir, ec);
      return cleanup_and(fail("The package changed after it was verified, so it was not installed."));
    }
    return install_package(result, staged, stage_dir, package_path, dir, release);
  }

 private:
  fs::path make_private_dir(const std::string& prefix) {
    std::string root = cfg().temp_root;
    if (root.empty()) {
      const char* tmp = std::getenv("TMPDIR");
      root = (tmp != nullptr && *tmp != '\0') ? tmp : "/tmp";
    }
    std::string templ = root + "/" + prefix + "XXXXXX";
    std::vector<char> buf(templ.begin(), templ.end());
    buf.push_back('\0');
    if (mkdtemp(buf.data()) == nullptr) return {};
    chmod(buf.data(), 0700);
    return fs::path(buf.data());
  }

  // The verified package stays where it is for a manual install.
  UpdateInstallResult fail_keep(UpdateInstallResult& result, const std::string& package_path,
                                const std::string& why, const fs::path& /*dir*/) {
    result.outcome = UpdateInstallResult::Outcome::Failed;
    result.kept_package = package_path;
    result.message = why + manual_hint(package_path);
    return result;
  }

  static std::string manual_hint(const std::string& package_path) {
#if defined(__APPLE__)
    return "\n\nThe verified package is at:\n" + package_path +
           "\n\nOpen the .dmg and drag zedit.app to Applications.";
#else
    return "\n\nThe verified package is at:\n" + package_path +
           "\n\nInstall it with:\nsudo apt install " + package_path;
#endif
  }

  UpdateInstallResult install_package(UpdateInstallResult& result, const std::string& staged,
                                      const fs::path& stage_dir, const std::string& package_path,
                                      const fs::path& dir, const ReleaseInfo& release) {
    std::error_code ec;
    const UpdateConfig& c = cfg();
    std::vector<std::string> command;
    if (!c.installer_override.empty()) {
      command = c.installer_override;
      command.push_back(staged);
    } else {
#if defined(__APPLE__)
      // macOS: open the verified .dmg so the user can drag zedit.app to
      // Applications. Replacing a signed app bundle in place is out of scope.
      set_stage(UpdateStage::Installing);
      std::atomic<bool> never{false};
      ProcessOutput out = run_process({c.open_command, staged}, "", never, nullptr);
      if (!out.started || out.exit_code != 0) {
        fs::remove_all(stage_dir, ec);
        return fail_keep(result, package_path,
                         "Couldn't open the disk image.", dir);
      }
      // The disk image is mounted from the staged copy, so it stays.
      fs::remove_all(dir, ec);
      result.outcome = UpdateInstallResult::Outcome::DiskImageOpened;
      result.message = "zedit " + release.tag +
                       " was downloaded and verified. The disk image is open: drag zedit.app to "
                       "Applications, then quit and reopen zedit.";
      return result;
#else
      // Only replace a packaged install; a build-tree or /usr/local binary
      // wouldn't be the one apt updates.
      std::error_code link_ec;
      fs::path self = fs::read_symlink("/proc/self/exe", link_ec);
      if (link_ec || self.string() != c.installed_binary) {
        fs::remove_all(stage_dir, ec);
        return fail_keep(result, package_path,
                         "This zedit (" + (link_ec ? std::string("unknown location") : self.string()) +
                             ") wasn't installed from the .deb, so the package can't be installed in its "
                             "place automatically.",
                         dir);
      }
      command = UpdateService::install_command(c, staged);
      if (access(command[0].c_str(), X_OK) != 0 || access(command[1].c_str(), X_OK) != 0) {
        fs::remove_all(stage_dir, ec);
        return fail_keep(result, package_path,
                         "pkexec or apt isn't available, so the package can't be installed from here.", dir);
      }
#endif
    }
    set_stage(UpdateStage::Installing);
    std::atomic<bool> never{false};  // apt is never interrupted
    ProcessOutput out = run_process(command, stage_dir.string(), never, nullptr);
    fs::remove_all(stage_dir, ec);
    if (!out.started || out.signaled || out.exit_code != 0) {
      std::string why;
      if (out.exit_code == 126) {
        why = "The password prompt was dismissed, so nothing was installed.";
      } else if (out.exit_code == 127) {
        why = "Not authorised to install packages (pkexec exit 127).";
      } else {
        why = "apt couldn't install the package (exit " + std::to_string(out.exit_code) + ").";
        std::string tail = trim(out.output);
        if (!tail.empty()) {
          if (tail.size() > 600) tail = tail.substr(tail.size() - 600);
          why += "\n" + tail;
        }
      }
      return fail_keep(result, package_path, why, dir);
    }
    fs::remove_all(dir, ec);  // the download folder, after a successful install
    result.outcome = UpdateInstallResult::Outcome::Installed;
    result.message = "zedit " + release.tag + " was installed.";
    return result;
  }

  std::shared_ptr<UpdateService::Shared> s_;
};

}  // namespace

UpdateService::UpdateService(UpdateConfig config) : shared_(std::make_shared<Shared>()) {
  shared_->config = std::move(config);
}

UpdateService::~UpdateService() {
  const UpdateStage stage = shared_->stage.load();
  if (stage == UpdateStage::Installing) {
    // Never wait on (or interrupt) the installer: apt keeps running as its own
    // process, and the worker owns what it needs through shared_.
    if (thread_.joinable()) thread_.detach();
    return;
  }
  shared_->cancel.store(true);
  if (thread_.joinable()) thread_.join();
}

const UpdateConfig& UpdateService::config() const { return shared_->config; }

UpdateStage UpdateService::stage() const { return shared_->stage.load(); }

UpdateProgress UpdateService::progress() const {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  return shared_->progress;
}

void UpdateService::cancel() { shared_->cancel.store(true); }

void UpdateService::start_check(bool manual) {
  UpdateStage expected = UpdateStage::Idle;
  if (!shared_->stage.compare_exchange_strong(expected, UpdateStage::Checking)) return;
  if (thread_.joinable()) thread_.join();
  shared_->cancel.store(false);
  auto shared = shared_;
  thread_ = std::thread([shared, manual]() {
    Job job(shared);
    UpdateCheckResult result = job.run_check(manual);
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      shared->check_result = std::move(result);
    }
    shared->stage.store(UpdateStage::Idle);
  });
}

void UpdateService::start_install(ReleaseInfo release) {
  UpdateStage expected = UpdateStage::Idle;
  if (!shared_->stage.compare_exchange_strong(expected, UpdateStage::Downloading)) return;
  if (thread_.joinable()) thread_.join();
  shared_->cancel.store(false);
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->progress = UpdateProgress{};
  }
  auto shared = shared_;
  thread_ = std::thread([shared, release = std::move(release)]() {
    Job job(shared);
    UpdateInstallResult result = job.run_install(release);
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      shared->install_result = std::move(result);
    }
    shared->stage.store(UpdateStage::Idle);
  });
}

std::optional<UpdateCheckResult> UpdateService::poll_check() {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  std::optional<UpdateCheckResult> r = std::move(shared_->check_result);
  shared_->check_result.reset();
  return r;
}

std::optional<UpdateInstallResult> UpdateService::poll_install() {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  std::optional<UpdateInstallResult> r = std::move(shared_->install_result);
  shared_->install_result.reset();
  return r;
}

std::vector<std::string> UpdateService::install_command(const UpdateConfig& config,
                                                        const std::string& package_path) {
  // An absolute path makes apt treat the argument as a file, not a package name.
  std::error_code ec;
  fs::path absolute = fs::absolute(package_path, ec);
  return {config.pkexec, config.apt, "install", "-y", ec ? package_path : absolute.string()};
}

}  // namespace zedit::core
