#pragma once

#include <string>

namespace zedit::frontend {

// Launches the OS's default handler for a URL (xdg-open on Linux, open on
// macOS) without a shell. Only pass URLs you trust to open (see
// zedit::core::is_trusted_release_page_url for release links).
void open_url(const std::string& url);

}  // namespace zedit::frontend
