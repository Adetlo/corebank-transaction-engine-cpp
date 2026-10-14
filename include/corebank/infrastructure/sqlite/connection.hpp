// RAII wrapper around one sqlite3 connection.
#pragma once

#include "corebank/domain/result.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct sqlite3;

namespace corebank::sqlite {

class Statement;

class Connection {
 public:
  [[nodiscard]] static Result<std::unique_ptr<Connection>> open(const std::string& path);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;
  ~Connection();

  [[nodiscard]] Result<void> exec(std::string_view sql);
  [[nodiscard]] Result<Statement> prepare(std::string_view sql);
  [[nodiscard]] std::int64_t last_insert_rowid() const noexcept;
  [[nodiscard]] int changes() const noexcept;
  [[nodiscard]] Error last_error(std::string_view context) const;
  [[nodiscard]] sqlite3* raw() const noexcept { return db_; }

 private:
  explicit Connection(sqlite3* db) noexcept : db_(db) {}
  sqlite3* db_;
};

}  // namespace corebank::sqlite
