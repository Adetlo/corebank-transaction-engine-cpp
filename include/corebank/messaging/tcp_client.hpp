// Small blocking TCP client used by the CLI, the health check and the tests.
#pragma once

#include "corebank/domain/result.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace corebank {

class TcpClient {
 public:
  TcpClient() = default;
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;
  TcpClient(TcpClient&& other) noexcept;
  TcpClient& operator=(TcpClient&& other) noexcept;
  ~TcpClient();

  [[nodiscard]] static Result<TcpClient> connect(const std::string& host, std::uint16_t port,
                                                 std::chrono::milliseconds timeout);

  // Sends raw bytes (caller adds the newline where needed).
  [[nodiscard]] Result<void> send(std::string_view data) const;
  // Reads one line (without newline); fails with Timeout or NetworkError.
  [[nodiscard]] Result<std::string> receive_line(std::chrono::milliseconds timeout);
  // Convenience: send `line` + '\n' and read the response line.
  [[nodiscard]] Result<std::string> request(std::string_view line,
                                            std::chrono::milliseconds timeout);
  void close() noexcept;
  [[nodiscard]] bool connected() const noexcept { return fd_ >= 0; }

 private:
  int fd_{-1};
  std::string buffer_;
};

}  // namespace corebank
