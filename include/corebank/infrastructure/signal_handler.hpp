// Async-signal-safe SIGTERM/SIGINT handling with a self-pipe wake-up.
#pragma once

#include "corebank/domain/result.hpp"

#include <chrono>

namespace corebank {

class SignalHandler {
 public:
  // Installs handlers for SIGTERM and SIGINT (and ignores SIGPIPE).
  [[nodiscard]] static Result<void> install();
  // Restores default dispositions and clears state.
  static void uninstall();

  [[nodiscard]] static bool stop_requested() noexcept;
  [[nodiscard]] static int last_signal() noexcept;
  // Blocks up to `timeout` for a signal; true if one arrived (or had already arrived).
  [[nodiscard]] static bool wait(std::chrono::milliseconds timeout);
  // File descriptor that becomes readable when a signal arrives (for poll loops).
  [[nodiscard]] static int wake_fd() noexcept;
  // Clears the stop flag (tests).
  static void reset() noexcept;
};

}  // namespace corebank
