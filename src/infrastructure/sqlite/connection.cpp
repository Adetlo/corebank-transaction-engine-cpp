#include "corebank/infrastructure/sqlite/connection.hpp"

#include "corebank/infrastructure/sqlite/statement.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace corebank::sqlite {

Result<std::unique_ptr<Connection>> Connection::open(const std::string& path) {
  sqlite3* db = nullptr;
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
  const int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
  if (rc != SQLITE_OK) {
    const std::string message = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return make_error(ErrorCode::PersistenceError,
                      "cannot open database '" + path + "': " + message);
  }
  return std::unique_ptr<Connection>(new Connection(db));
}

Connection::~Connection() {
  if (db_ != nullptr) {
    sqlite3_close_v2(db_);
  }
}

Result<void> Connection::exec(std::string_view sql) {
  char* error_text = nullptr;
  const std::string owned{sql};
  const int rc = sqlite3_exec(db_, owned.c_str(), nullptr, nullptr, &error_text);
  if (rc != SQLITE_OK) {
    std::string message = error_text != nullptr ? error_text : sqlite3_errstr(rc);
    sqlite3_free(error_text);
    return make_error(ErrorCode::PersistenceError, "sqlite exec failed: " + message);
  }
  return ok();
}

Result<Statement> Connection::prepare(std::string_view sql) {
  sqlite3_stmt* stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return last_error("prepare");
  }
  return Statement{*this, stmt};
}

std::int64_t Connection::last_insert_rowid() const noexcept {
  return sqlite3_last_insert_rowid(db_);
}

int Connection::changes() const noexcept {
  return sqlite3_changes(db_);
}

Error Connection::last_error(std::string_view context) const {
  return make_error(ErrorCode::PersistenceError,
                    "sqlite " + std::string{context} + " failed: " + sqlite3_errmsg(db_));
}

}  // namespace corebank::sqlite
