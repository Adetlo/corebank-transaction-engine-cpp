#include "corebank/messaging/tcp_client.hpp"

#include "corebank/infrastructure/errno_text.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace corebank {

TcpClient::TcpClient(TcpClient&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), buffer_(std::move(other.buffer_)) {}

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = std::exchange(other.fd_, -1);
    buffer_ = std::move(other.buffer_);
  }
  return *this;
}

TcpClient::~TcpClient() {
  close();
}

void TcpClient::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

Result<TcpClient> TcpClient::connect(const std::string& host, std::uint16_t port,
                                     std::chrono::milliseconds timeout) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const std::string port_text = std::to_string(port);
  const int rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &results);
  if (rc != 0 || results == nullptr) {
    return make_error(ErrorCode::NetworkError,
                      "cannot resolve '" + host + "': " + gai_error_text(rc));
  }
  const int fd = ::socket(results->ai_family, results->ai_socktype, results->ai_protocol);
  if (fd < 0) {
    ::freeaddrinfo(results);
    return make_error(ErrorCode::NetworkError, std::string{"socket(): "} + errno_text(errno));
  }
  const int flags = ::fcntl(fd, F_GETFL, 0);
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int connected = ::connect(fd, results->ai_addr, results->ai_addrlen);
  ::freeaddrinfo(results);
  if (connected != 0 && errno == EINPROGRESS) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;
    const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (ready <= 0) {
      ::close(fd);
      return make_error(ErrorCode::Timeout, "connect to " + host + ":" + port_text + " timed out");
    }
    int error = 0;
    socklen_t len = sizeof(error);
    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
    connected = error == 0 ? 0 : -1;
    errno = error;
  }
  if (connected != 0) {
    const std::string why = errno_text(errno);
    ::close(fd);
    return make_error(ErrorCode::NetworkError,
                      "connect to " + host + ":" + port_text + " failed: " + why);
  }
  ::fcntl(fd, F_SETFL, flags);
  TcpClient client;
  client.fd_ = fd;
  return client;
}

Result<void> TcpClient::send(std::string_view data) const {
  if (fd_ < 0) {
    return make_error(ErrorCode::NetworkError, "not connected");
  }
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return make_error(ErrorCode::NetworkError, std::string{"send(): "} + errno_text(errno));
    }
    sent += static_cast<std::size_t>(n);
  }
  return ok();
}

Result<std::string> TcpClient::receive_line(std::chrono::milliseconds timeout) {
  if (fd_ < 0) {
    return make_error(ErrorCode::NetworkError, "not connected");
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::array<char, 2048> chunk{};
  while (true) {
    if (const auto newline = buffer_.find('\n'); newline != std::string::npos) {
      std::string line = buffer_.substr(0, newline);
      buffer_.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return line;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      return make_error(ErrorCode::Timeout,
                        "no response within " + std::to_string(timeout.count()) + " ms");
    }
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
    if (ready == 0) {
      return make_error(ErrorCode::Timeout,
                        "no response within " + std::to_string(timeout.count()) + " ms");
    }
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return make_error(ErrorCode::NetworkError, std::string{"poll(): "} + errno_text(errno));
    }
    const auto n = ::recv(fd_, chunk.data(), chunk.size(), 0);
    if (n == 0) {
      return make_error(ErrorCode::NetworkError, "connection closed by peer");
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return make_error(ErrorCode::NetworkError, std::string{"recv(): "} + errno_text(errno));
    }
    buffer_.append(chunk.data(), static_cast<std::size_t>(n));
  }
}

Result<std::string> TcpClient::request(std::string_view line, std::chrono::milliseconds timeout) {
  if (auto sent = send(std::string{line} + "\n"); !sent) {
    return sent.error();
  }
  return receive_line(timeout);
}

}  // namespace corebank
