#include "corebank/infrastructure/errno_text.hpp"

#include <netdb.h>

#include <array>
#include <cstring>
#include <string>

namespace corebank {

std::string errno_text(int error_number) {
  std::array<char, 256> buffer{};
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
  // GNU strerror_r returns the message pointer (may point into the buffer).
  const char* text = ::strerror_r(error_number, buffer.data(), buffer.size());
  return text != nullptr ? std::string{text} : std::string{"unknown error"};
#else
  // POSIX strerror_r fills the buffer and returns 0 on success.
  if (::strerror_r(error_number, buffer.data(), buffer.size()) != 0) {
    return "errno " + std::to_string(error_number);
  }
  return std::string{buffer.data()};
#endif
}

std::string gai_error_text(int gai_code) {
  // gai_strerror returns pointers to immutable static strings on every platform
  // this project targets (glibc, musl, macOS).
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* text = ::gai_strerror(gai_code);
  return text != nullptr ? std::string{text} : std::string{"unknown resolver error"};
}

}  // namespace corebank
