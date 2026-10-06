// Test helper (Linux, Wayland): puts exactly the given MIME types on the
// Wayland clipboard and serves them until another client takes the
// clipboard over or it is killed.
//
//   zedit_wl_clipboard_owner [--hang] TYPE=FILE...   (split at the last =)
//
// Prints "ready" on stdout once the compositor has the selection. With
// --hang it accepts paste requests but never writes or closes the pipe (an
// owner that never answers).
//
// Exists because `wl-copy --type text/html` also offers text/plain,
// TEXT, STRING and UTF8_STRING aliases for any text/* type, so it can't
// produce the HTML-only clipboard that broke paste. Uses the
// wlr-data-control protocol (sway, other wlroots compositors, KDE), which
// lets a client set the selection without keyboard focus -- a headless
// compositor in CI has no keyboard to give focus with.

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-client.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "wlr-data-control-unstable-v1-client-protocol.h"

namespace {

wl_seat* g_seat = nullptr;
zwlr_data_control_manager_v1* g_manager = nullptr;
std::map<std::string, std::string> g_data;
bool g_hang = false;
bool g_running = true;
std::vector<int> g_held_fds;

void registry_global(void*, wl_registry* registry, uint32_t name, const char* interface,
                     uint32_t) {
  if (std::strcmp(interface, wl_seat_interface.name) == 0 && g_seat == nullptr) {
    g_seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
  } else if (std::strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
    g_manager = static_cast<zwlr_data_control_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, 1));
  }
}
void registry_global_remove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistryListener = {registry_global, registry_global_remove};

void source_send(void*, zwlr_data_control_source_v1*, const char* mime_type, int32_t fd) {
  if (g_hang) {
    g_held_fds.push_back(fd);  // never written, never closed
    return;
  }
  const std::string& data = g_data[mime_type];
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = write(fd, data.data() + off, data.size() - off);
    if (n <= 0) break;
    off += static_cast<size_t>(n);
  }
  close(fd);
}
void source_cancelled(void*, zwlr_data_control_source_v1*) { g_running = false; }
const zwlr_data_control_source_v1_listener kSourceListener = {source_send, source_cancelled};

}  // namespace

int main(int argc, char** argv) {
  signal(SIGPIPE, SIG_IGN);
  std::vector<std::string> types;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--hang") {
      g_hang = true;
      continue;
    }
    size_t eq = arg.rfind('=');  // MIME types can contain '='
    if (eq == std::string::npos) {
      std::fprintf(stderr, "usage: %s [--hang] TYPE=FILE...\n", argv[0]);
      return 2;
    }
    std::ifstream in(arg.substr(eq + 1), std::ios::binary);
    std::stringstream buf;
    buf << in.rdbuf();
    types.push_back(arg.substr(0, eq));
    g_data[types.back()] = buf.str();
  }

  wl_display* display = wl_display_connect(nullptr);
  if (display == nullptr) {
    std::fprintf(stderr, "wl_clipboard_owner: no Wayland display\n");
    return 1;
  }
  wl_registry* registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &kRegistryListener, nullptr);
  wl_display_roundtrip(display);
  if (g_seat == nullptr || g_manager == nullptr) {
    std::fprintf(stderr, "wl_clipboard_owner: compositor lacks wl_seat or wlr-data-control\n");
    return 1;
  }
  zwlr_data_control_device_v1* device =
      zwlr_data_control_manager_v1_get_data_device(g_manager, g_seat);
  zwlr_data_control_source_v1* source = zwlr_data_control_manager_v1_create_data_source(g_manager);
  zwlr_data_control_source_v1_add_listener(source, &kSourceListener, nullptr);
  for (const std::string& type : types) zwlr_data_control_source_v1_offer(source, type.c_str());
  zwlr_data_control_device_v1_set_selection(device, source);
  wl_display_roundtrip(display);
  std::printf("ready\n");
  std::fflush(stdout);

  while (g_running && wl_display_dispatch(display) != -1) {
  }
  wl_display_disconnect(display);
  return 0;
}
