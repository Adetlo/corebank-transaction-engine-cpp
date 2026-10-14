#include "corebank/messaging/tcp_server.hpp"

#include "corebank/infrastructure/errno_text.hpp"
#include "corebank/infrastructure/logger.hpp"
#include "corebank/messaging/protocol.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace corebank {
namespace {

constexpr int kPollSliceMs = 100;

bool send_all(int fd, std::string_view data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

void send_line(int fd, const std::string& line) {
  send_all(fd, line + "\n");
}

}  // namespace

TcpServer::TcpServer(TcpServerOptions options, MessageHandler& handler)
    : options_(std::move(options)), handler_(handler) {}

TcpServer::~TcpServer() {
  stop();
}

Result<void> TcpServer::start() {
  if (running_.exchange(true)) {
    return ok();
  }
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    running_.store(false);
    return make_error(ErrorCode::NetworkError, std::string{"socket(): "} + errno_text(errno));
  }
  const int one = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  ::fcntl(listen_fd_, F_SETFD, FD_CLOEXEC);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options_.port);
  if (::inet_pton(AF_INET, options_.address.c_str(), &addr.sin_addr) != 1) {
    ::close(listen_fd_);
    listen_fd_ = -1;
    running_.store(false);
    return make_error(ErrorCode::ConfigurationError,
                      "listen_address '" + options_.address + "' is not a valid IPv4 address");
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    const std::string why = errno_text(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    running_.store(false);
    return make_error(ErrorCode::NetworkError, "bind(" + options_.address + ":" +
                                                   std::to_string(options_.port) + "): " + why);
  }
  if (::listen(listen_fd_, 128) != 0) {
    const std::string why = errno_text(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    running_.store(false);
    return make_error(ErrorCode::NetworkError, "listen(): " + why);
  }
  sockaddr_in bound{};
  socklen_t len = sizeof(bound);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &len) == 0) {
    bound_port_ = ntohs(bound.sin_port);
  }
  const int flags = ::fcntl(listen_fd_, F_GETFL, 0);
  ::fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

  acceptor_ = std::jthread([this](const std::stop_token& stop) { accept_loop(stop); });
  Logger::instance().info(
      "tcp.listening", {{"address", options_.address},
                        {"port", static_cast<std::int64_t>(bound_port_)},
                        {"max_connections", static_cast<std::int64_t>(options_.max_connections)}});
  return ok();
}

void TcpServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (acceptor_.joinable()) {
    acceptor_.request_stop();
    acceptor_.join();
  }
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  std::list<std::unique_ptr<Connection>> connections;
  {
    const std::lock_guard lock(connections_mutex_);
    connections.swap(connections_);
  }
  for (const auto& connection : connections) {
    connection->thread.request_stop();
    if (connection->fd >= 0) {
      ::shutdown(connection->fd, SHUT_RDWR);
    }
  }
  for (const auto& connection : connections) {
    if (connection->thread.joinable()) {
      connection->thread.join();
    }
  }
  Logger::instance().info("tcp.stopped", {{"total_connections", total_.load()}});
}

std::size_t TcpServer::active_connections() const {
  const std::lock_guard lock(connections_mutex_);
  std::size_t active = 0;
  for (const auto& connection : connections_) {
    if (!connection->done.load()) {
      ++active;
    }
  }
  return active;
}

void TcpServer::reap_finished() {
  const std::lock_guard lock(connections_mutex_);
  for (auto it = connections_.begin(); it != connections_.end();) {
    if ((*it)->done.load()) {
      if ((*it)->thread.joinable()) {
        (*it)->thread.join();
      }
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }
}

void TcpServer::accept_loop(const std::stop_token& stop) {
  while (!stop.stop_requested()) {
    pollfd pfd{};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, kPollSliceMs);
    if (ready <= 0) {
      reap_finished();
      continue;
    }
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &len);
    if (fd < 0) {
      continue;
    }
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    total_.fetch_add(1, std::memory_order_relaxed);
    reap_finished();
    if (active_connections() >= options_.max_connections) {
      send_line(fd, protocol::error_response(
                        make_error(ErrorCode::TooManyConnections, "connection limit reached")));
      ::close(fd);
      Logger::instance().warn("tcp.connection_refused", {{"reason", "max_connections"}});
      continue;
    }
    auto connection = std::make_unique<Connection>();
    connection->fd = fd;
    Connection* raw = connection.get();
    {
      const std::lock_guard lock(connections_mutex_);
      connections_.push_back(std::move(connection));
    }
    raw->thread = std::jthread(
        [this, raw](const std::stop_token& conn_stop) { serve_connection(*raw, conn_stop); });
  }
}

void TcpServer::serve_connection(Connection& connection, const std::stop_token& stop) {
  const int fd = connection.fd;
  std::string buffer;
  auto last_activity = std::chrono::steady_clock::now();
  std::array<char, 2048> chunk{};
  bool open = true;
  while (open && !stop.stop_requested()) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, kPollSliceMs);
    if (ready < 0 && errno != EINTR) {
      break;
    }
    if (ready == 0) {
      if (std::chrono::steady_clock::now() - last_activity > options_.idle_timeout) {
        send_line(fd, protocol::error_response(make_error(ErrorCode::Timeout, "idle timeout")));
        break;
      }
      continue;
    }
    if (ready <= 0) {
      continue;
    }
    const auto n = ::recv(fd, chunk.data(), chunk.size(), 0);
    if (n <= 0) {
      break;  // peer closed or error
    }
    last_activity = std::chrono::steady_clock::now();
    buffer.append(chunk.data(), static_cast<std::size_t>(n));
    std::size_t newline = 0;
    while ((newline = buffer.find('\n')) != std::string::npos) {
      const std::string line = buffer.substr(0, newline);
      buffer.erase(0, newline + 1);
      const auto started = std::chrono::steady_clock::now();
      const std::string response = handler_.handle(line);
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count();
      Logger::instance().debug(
          "tcp.message", {{"bytes", static_cast<std::int64_t>(line.size())}, {"duration_ms", ms}});
      if (!send_all(fd, response + "\n")) {
        open = false;
        break;
      }
    }
    if (buffer.size() > options_.max_line_bytes) {
      send_line(fd, protocol::error_response(make_error(
                        ErrorCode::PayloadTooLarge,
                        "line exceeds " + std::to_string(options_.max_line_bytes) + " bytes")));
      break;
    }
  }
  ::close(fd);
  connection.fd = -1;
  connection.done.store(true);
}

}  // namespace corebank
