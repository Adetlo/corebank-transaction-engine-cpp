// Thread-safe errno / getaddrinfo error text (strerror() is not reentrant).
#pragma once

#include <string>

namespace corebank {

[[nodiscard]] std::string errno_text(int error_number);
[[nodiscard]] std::string gai_error_text(int gai_code);

}  // namespace corebank
