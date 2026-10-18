#include "open_url.hpp"

#include <unistd.h>

namespace zedit::frontend {

// Launches the OS's default handler for a URL (xdg-open on Linux, open on
// macOS) without going through a shell -- execlp takes the URL as a plain
// argv entry, not shell-interpreted text, so this is immune to injection
// regardless of what characters end up in it. Deliberately doesn't
// waitpid(): a short-lived launcher process sitting as a zombie until
// zedit exits is harmless for a rare, user-triggered action, and simpler
// than managing its lifecycle for something we don't need to track.
void open_url(const std::string& url) {
  if (url.empty()) {
    return;
  }
#if defined(__APPLE__)
  const char* opener = "open";
#else
  const char* opener = "xdg-open";
#endif
  pid_t pid = fork();
  if (pid == 0) {
    execlp(opener, opener, url.c_str(), static_cast<char*>(nullptr));
    _exit(127);  // only reached if execlp itself failed
  }
}

}  // namespace zedit::frontend
