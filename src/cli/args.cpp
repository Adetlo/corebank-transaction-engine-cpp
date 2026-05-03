#include "corebank/cli/args.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corebank::cli {

Result<Args> Args::parse(int argc, const char* const* argv,
                         const std::vector<std::string>& flag_names) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string token = argv[i];
    if (token == "--") {
      for (int j = i + 1; j < argc; ++j) {
        args.positionals_.emplace_back(argv[j]);
      }
      break;
    }
    if (token.size() > 2 && token.starts_with("--")) {
      std::string name = token.substr(2);
      std::optional<std::string> inline_value;
      if (const auto eq = name.find('='); eq != std::string::npos) {
        inline_value = name.substr(eq + 1);
        name.resize(eq);
      }
      const bool is_flag =
          std::find(flag_names.begin(), flag_names.end(), name) != flag_names.end();
      if (is_flag) {
        args.flags_.push_back(name);
        continue;
      }
      if (inline_value) {
        args.options_.emplace_back(name, *inline_value);
        continue;
      }
      if (i + 1 >= argc) {
        return make_error(ErrorCode::ValidationError, "option --" + name + " requires a value");
      }
      args.options_.emplace_back(name, argv[++i]);
      continue;
    }
    args.positionals_.push_back(token);
  }
  return args;
}

std::optional<std::string> Args::option(std::string_view name) const {
  for (const auto& [key, value] : options_) {
    if (key == name) {
      return value;
    }
  }
  return std::nullopt;
}

Result<std::string> Args::required(std::string_view name) const {
  if (auto value = option(name)) {
    return *value;
  }
  return make_error(ErrorCode::ValidationError, "missing required option --" + std::string{name});
}

bool Args::flag(std::string_view name) const {
  return std::find(flags_.begin(), flags_.end(), name) != flags_.end();
}

Result<std::int64_t> Args::integer(std::string_view name, std::int64_t fallback) const {
  const auto text = option(name);
  if (!text) {
    return fallback;
  }
  if (text->empty() || !std::all_of(text->begin(), text->end(), [](char ch) {
        return std::isdigit(static_cast<unsigned char>(ch)) != 0;
      })) {
    return make_error(ErrorCode::ValidationError,
                      "option --" + std::string{name} + " must be a non-negative integer");
  }
  if (text->size() > 18) {
    return make_error(ErrorCode::ValidationError,
                      "option --" + std::string{name} + " is too large");
  }
  return std::stoll(*text);
}

}  // namespace corebank::cli
