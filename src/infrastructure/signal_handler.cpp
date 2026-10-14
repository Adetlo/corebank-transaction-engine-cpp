#include "corebank/infrastructure/signal_handler.hpp"

#include "corebank/infrastructure/errno_text.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <string>

namespace corebank {
namespace {

std::atomic<int> g_last_signal{0};
std::atomic<bool> g_stop_requested{false};
std::atomic<int> g_wake_write_fd{-1};
std::atomic<int> g_wake_read_fd{-1};

// Only async-signal-safe calls here: atomics and write(2).
void on_signal(int signal_number) noexcept {
  g_last_signal.store(signal_number, std::memory_order_relaxed);
  g_stop_requested.store(true, std::memory_order_release);
  const int fd = g_wake_write_fd.load(std::memory_order_acquire);
  if (fd >= 0) {
    const char byte = 1;
    const int saved_errno = errno;
    [[maybe_unused]] const auto written = ::write(fd, &byte, 1);
    errno = saved_errno;
  }
}

bool set_nonblocking_cloexec(int fd) noexcept {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return false;
  }
  const int fd_flags = ::fcntl(fd, F_GETFD, 0);
  return fd_flags >= 0 && ::fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) >= 0;
}

}  // namespace

Result<void> SignalHandler::install() {
  if (g_wake_read_fd.load() < 0) {
    int fds[2] = {-1, -1};  // NOLINT(modernize-avoid-c-arrays) pipe(2) API
    if (::pipe(fds) != 0) {
      return make_error(ErrorCode::Internal, std::string{"pipe() failed: "} + errno_text(errno));
    }
    if (!set_nonblocking_cloexec(fds[0]) || !set_nonblocking_cloexec(fds[1])) {
      ::close(fds[0]);
      ::close(fds[1]);
      return make_error(ErrorCode::Internal, "fcntl() on signal pipe failed");
    }
    g_wake_read_fd.store(fds[0]);
    g_wake_write_fd.store(fds[1]);
  }
  struct sigaction action {};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  if (::sigaction(SIGTERM, &action, nullptr) != 0 || ::sigaction(SIGINT, &action, nullptr) != 0) {
    return make_error(ErrorCode::Internal, std::string{"sigaction() failed: "} + errno_text(errno));
  }
  struct sigaction ignore {};
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  ::sigaction(SIGPIPE, &ignore, nullptr);
  return ok();
}

void SignalHandler::uninstall() {
  struct sigaction dfl {};
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  ::sigaction(SIGTERM, &dfl, nullptr);
  ::sigaction(SIGINT, &dfl, nullptr);
  reset();
}

bool SignalHandler::stop_requested() noexcept {
  return g_stop_requested.load(std::memory_order_acquire);
}

int SignalHandler::last_signal() noexcept {
  return g_last_signal.load(std::memory_order_relaxed);
}

int SignalHandler::wake_fd() noexcept {
  return g_wake_read_fd.load(std::memory_order_acquire);
}

bool SignalHandler::wait(std::chrono::milliseconds timeout) {
  if (stop_requested()) {
    return true;
  }
  const int fd = wake_fd();
  if (fd < 0) {
    return false;
  }
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLIN;
  const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
  if (ready > 0) {
    char buffer[16];  // NOLINT(modernize-avoid-c-arrays) read(2) scratch buffer
    while (::read(fd, buffer, sizeof(buffer)) > 0) {
    }
  }
  return stop_requested();
}

void SignalHandler::reset() noexcept {
  g_stop_requested.store(false, std::memory_order_release);
  g_last_signal.store(0, std::memory_order_relaxed);
  const int fd = wake_fd();
  if (fd >= 0) {
    char buffer[16];  // NOLINT(modernize-avoid-c-arrays) read(2) scratch buffer
    while (::read(fd, buffer, sizeof(buffer)) > 0) {
    }
  }
}

}  // namespace corebank
