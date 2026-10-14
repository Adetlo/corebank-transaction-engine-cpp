// RAII prepared statement with typed binding (sqlite3_bind_*, never string concatenation).
#pragma once

#include "corebank/domain/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3_stmt;

namespace corebank::sqlite {

class Connection;

class Statement {
 public:
  Statement(Connection& connection, sqlite3_stmt* stmt) noexcept;
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&& other) noexcept;
  Statement& operator=(Statement&& other) noexcept;
  ~Statement();

  // Parameters are 1-based, as in SQLite.
  [[nodiscard]] Result<void> bind(int index, std::string_view value);
  [[nodiscard]] Result<void> bind(int index, std::int64_t value);
  [[nodiscard]] Result<void> bind(int index, int value) {
    return bind(index, static_cast<std::int64_t>(value));
  }
  [[nodiscard]] Result<void> bind(int index, bool value) {
    return bind(index, static_cast<std::int64_t>(value ? 1 : 0));
  }
  [[nodiscard]] Result<void> bind_null(int index);
  [[nodiscard]] Result<void> bind_optional(int index, const std::optional<std::string>& value);

  // true when a row is available, false when done.
  [[nodiscard]] Result<bool> step();
  // Runs a statement that returns no rows.
  [[nodiscard]] Result<void> execute();
  [[nodiscard]] Result<void> reset();

  [[nodiscard]] std::string column_text(int index) const;
  [[nodiscard]] std::optional<std::string> column_optional_text(int index) const;
  [[nodiscard]] std::int64_t column_int64(int index) const;
  [[nodiscard]] bool column_is_null(int index) const;
  [[nodiscard]] int column_count() const;

 private:
  Connection* connection_;
  sqlite3_stmt* stmt_;
};

}  // namespace corebank::sqlite
