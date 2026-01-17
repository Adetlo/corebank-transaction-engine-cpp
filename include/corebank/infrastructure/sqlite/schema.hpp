#pragma once

#include "corebank/domain/result.hpp"

#include <string_view>

namespace corebank::sqlite {

class Connection;

inline constexpr int kSchemaVersion = 1;

// Creates every table, index and immutability trigger if absent. Idempotent.
[[nodiscard]] Result<void> apply_schema(Connection& connection);
[[nodiscard]] std::string_view schema_sql();

}  // namespace corebank::sqlite
