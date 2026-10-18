#pragma once
// A tiny in-process HTTP/1.1 server on 127.0.0.1 (random port) that serves
// canned responses, for driving the real updater (and the real `curl`) in
// tests without the network.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <signal.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS has SO_NOSIGPIPE instead (set on the accepted socket)
#endif

namespace zedit::test {

class FakeHttpServer {
 public:
  struct Response {
    int status = 200;
    std::string body;
    std::map<std::string, std::string> headers;
    // Sends the headers (claiming a huge body) and then a trickle of bytes
    // until the client goes away: a download that can be cancelled.
    bool endless = false;
  };

  FakeHttpServer() {
    signal(SIGPIPE, SIG_IGN);  // a client that hangs up mid-send must not kill the test
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    listen(listen_fd_, 16);
    socklen_t len = sizeof addr;
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    accept_thread_ = std::thread([this] { accept_loop(); });
  }

  ~FakeHttpServer() {
    stop_.store(true);
    if (accept_thread_.joinable()) accept_thread_.join();
    for (std::thread& t : handlers_) t.join();
    close(listen_fd_);
  }

  FakeHttpServer(const FakeHttpServer&) = delete;
  FakeHttpServer& operator=(const FakeHttpServer&) = delete;

  std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }

  void set(const std::string& path, Response response) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_[path] = std::move(response);
  }
  void remove(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_.erase(path);
  }
  // Request paths in arrival order.
  std::vector<std::string> requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
  }
  int count(const std::string& path) const {
    int n = 0;
    for (const std::string& r : requests()) n += r == path ? 1 : 0;
    return n;
  }
  int index_of(const std::string& path) const {
    std::vector<std::string> all = requests();
    for (size_t i = 0; i < all.size(); ++i) {
      if (all[i] == path) return static_cast<int>(i);
    }
    return -1;
  }

 private:
  void accept_loop() {
    while (!stop_.load()) {
      pollfd pfd{listen_fd_, POLLIN, 0};
      if (poll(&pfd, 1, 50) <= 0) continue;
      int fd = accept(listen_fd_, nullptr, nullptr);
      if (fd < 0) continue;
      handlers_.emplace_back([this, fd] { handle(fd); });
    }
  }

  static bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
      ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
      if (n <= 0) return false;
      sent += static_cast<size_t>(n);
    }
    return true;
  }

  void handle(int fd) {
    std::string request;
    char buf[2048];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
      pollfd pfd{fd, POLLIN, 0};
      if (poll(&pfd, 1, 2000) <= 0) break;
      ssize_t n = recv(fd, buf, sizeof buf, 0);
      if (n <= 0) break;
      request.append(buf, static_cast<size_t>(n));
    }
    std::string path;
    if (request.rfind("GET ", 0) == 0) {
      size_t end = request.find(' ', 4);
      path = request.substr(4, end == std::string::npos ? std::string::npos : end - 4);
    }
    Response response;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      requests_.push_back(path);
      auto it = routes_.find(path);
      if (it == routes_.end()) {
        response.status = 404;
        response.body = R"({"message":"Not Found"})";
      } else {
        response = it->second;
      }
    }
    std::string head = "HTTP/1.1 " + std::to_string(response.status) + " X\r\n";
    for (const auto& [k, v] : response.headers) head += k + ": " + v + "\r\n";
    head += "Content-Length: " +
            (response.endless ? std::string("500000000") : std::to_string(response.body.size())) +
            "\r\nConnection: close\r\n\r\n";
    if (send_all(fd, head)) {
      if (response.endless) {
        const std::string chunk(4096, 'x');
        while (!stop_.load() && send_all(fd, chunk)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      } else {
        send_all(fd, response.body);
      }
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
  }

  int listen_fd_ = -1;
  int port_ = 0;
  std::atomic<bool> stop_{false};
  std::thread accept_thread_;
  std::vector<std::thread> handlers_;  // only touched by the accept thread, then the destructor
  mutable std::mutex mutex_;
  std::map<std::string, Response> routes_;
  std::vector<std::string> requests_;
};

}  // namespace zedit::test
