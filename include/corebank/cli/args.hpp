// Dependency-free argument parsing: positionals plus --name value / --flag options.
#pragma once

#include "corebank/domain/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corebank::cli {

class Args {
 public:
  [[nodiscard]] static Result<Args> parse(int argc, const char* const* argv,
                                          const std::vector<std::string>& flag_names);

  [[nodiscard]] const std::vector<std::string>& positionals() const noexcept {
    return positionals_;
  }
  [[nodiscard]] std::optional<std::string> option(std::string_view name) const;
  [[nodiscard]] Result<std::string> required(std::string_view name) const;
  [[nodiscard]] bool flag(std::string_view name) const;
  [[nodiscard]] Result<std::int64_t> integer(std::string_view name, std::int64_t fallback) const;

  [[nodiscard]] std::string positional(std::size_t index) const {
    return index < positionals_.size() ? positionals_[index] : std::string{};
  }

 private:
  std::vector<std::string> positionals_;
  std::vector<std::pair<std::string, std::string>> options_;
  std::vector<std::string> flags_;
};

}  // namespace corebank::cli
