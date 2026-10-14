// POSIX TCP listener: accept loop + one std::jthread per connection, bounded by
// max_connections, idle timeouts, line-length limits and cooperative shutdown.
#pragma once

#include "corebank/domain/result.hpp"
#include "corebank/messaging/message_handler.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace corebank {

struct TcpServerOptions {
  std::string address{"127.0.0.1"};
  std::uint16_t port{7400};  // 0 = ephemeral (tests)
  std::size_t max_connections{64};
  std::chrono::seconds idle_timeout{30};
  std::size_t max_line_bytes{4096};
};

class TcpServer {
 public:
  TcpServer(TcpServerOptions options, MessageHandler& handler);
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;
  TcpServer(TcpServer&&) = delete;
  TcpServer& operator=(TcpServer&&) = delete;
  ~TcpServer();

  [[nodiscard]] Result<void> start();
  // Stops accepting, closes every connection, joins all threads. Idempotent.
  void stop();

  [[nodiscard]] std::uint16_t port() const noexcept { return bound_port_; }
  [[nodiscard]] std::size_t active_connections() const;
  [[nodiscard]] std::uint64_t total_connections() const noexcept { return total_.load(); }
  [[nodiscard]] bool running() const noexcept { return running_.load(); }

 private:
  struct Connection {
    int fd{-1};
    std::atomic<bool> done{false};
    std::jthread thread;
  };

  void accept_loop(const std::stop_token& stop);
  void serve_connection(Connection& connection, const std::stop_token& stop);
  void reap_finished();

  TcpServerOptions options_;
  MessageHandler& handler_;
  int listen_fd_{-1};
  std::uint16_t bound_port_{0};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> total_{0};
  std::jthread acceptor_;
  mutable std::mutex connections_mutex_;
  std::list<std::unique_ptr<Connection>> connections_;
};

}  // namespace corebank
