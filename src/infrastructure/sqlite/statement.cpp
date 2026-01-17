#include "corebank/infrastructure/sqlite/statement.hpp"

#include "corebank/infrastructure/sqlite/connection.hpp"

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace corebank::sqlite {

Statement::Statement(Connection& connection, sqlite3_stmt* stmt) noexcept
    : connection_(&connection), stmt_(stmt) {}

Statement::Statement(Statement&& other) noexcept
    : connection_(other.connection_), stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
  if (this != &other) {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
    connection_ = other.connection_;
    stmt_ = std::exchange(other.stmt_, nullptr);
  }
  return *this;
}

Statement::~Statement() {
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
  }
}

Result<void> Statement::bind(int index, std::string_view value) {
  const int rc = sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()),
                                   SQLITE_TRANSIENT);
  return rc == SQLITE_OK ? ok() : Result<void>{connection_->last_error("bind text")};
}

Result<void> Statement::bind(int index, std::int64_t value) {
  const int rc = sqlite3_bind_int64(stmt_, index, value);
  return rc == SQLITE_OK ? ok() : Result<void>{connection_->last_error("bind int")};
}

Result<void> Statement::bind_null(int index) {
  const int rc = sqlite3_bind_null(stmt_, index);
  return rc == SQLITE_OK ? ok() : Result<void>{connection_->last_error("bind null")};
}

Result<void> Statement::bind_optional(int index, const std::optional<std::string>& value) {
  return value ? bind(index, std::string_view{*value}) : bind_null(index);
}

Result<bool> Statement::step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) {
    return true;
  }
  if (rc == SQLITE_DONE) {
    return false;
  }
  if (rc == SQLITE_CONSTRAINT) {
    const int extended = sqlite3_extended_errcode(connection_->raw());
    if (extended == SQLITE_CONSTRAINT_PRIMARYKEY || extended == SQLITE_CONSTRAINT_UNIQUE) {
      return make_error(ErrorCode::DuplicateRequest, std::string{"unique constraint violated: "} +
                                                         sqlite3_errmsg(connection_->raw()));
    }
  }
  return connection_->last_error("step");
}

Result<void> Statement::execute() {
  auto row = step();
  if (!row) {
    return row.error();
  }
  return ok();
}

Result<void> Statement::reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
  return ok();
}

std::string Statement::column_text(int index) const {
  const unsigned char* text = sqlite3_column_text(stmt_, index);
  if (text == nullptr) {
    return {};
  }
  const int bytes = sqlite3_column_bytes(stmt_, index);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char*>(text), static_cast<std::size_t>(bytes)};
}

std::optional<std::string> Statement::column_optional_text(int index) const {
  if (column_is_null(index)) {
    return std::nullopt;
  }
  return column_text(index);
}

std::int64_t Statement::column_int64(int index) const {
  return sqlite3_column_int64(stmt_, index);
}

bool Statement::column_is_null(int index) const {
  return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

int Statement::column_count() const {
  return sqlite3_column_count(stmt_);
}

}  // namespace corebank::sqlite
