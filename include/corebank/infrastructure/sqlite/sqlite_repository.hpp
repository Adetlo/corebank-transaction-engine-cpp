// SQLite implementation of the Repository/UnitOfWork interfaces.
#pragma once

#include "corebank/application/repository.hpp"
#include "corebank/infrastructure/sqlite/connection.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace corebank::sqlite {

// Test-only fault injection points (see tests/integration/rollback_test.cpp).
enum class Fault { None, FailCommit, FailInsertPosting };

struct SqliteOptions {
  std::size_t pool_size{4};
  int busy_timeout_ms{5000};
};

class SqliteRepository final : public Repository {
 public:
  using Options = SqliteOptions;

  [[nodiscard]] static Result<std::unique_ptr<SqliteRepository>> open(const std::string& path,
                                                                      Options options = {});
  ~SqliteRepository() override;

  [[nodiscard]] Result<std::unique_ptr<UnitOfWork>> begin(TransactionMode mode) override;
  [[nodiscard]] Result<void> migrate() override;
  [[nodiscard]] Result<void> health_check() override;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  void inject_fault(Fault fault) noexcept { fault_.store(fault); }
  [[nodiscard]] Fault fault() const noexcept { return fault_.load(); }

  // Connection pool (used by the unit of work).
  [[nodiscard]] std::unique_ptr<Connection> acquire();
  void release(std::unique_ptr<Connection> connection);
  // SQLite allows one writer at a time; serialising writers here lets threads block on a
  // mutex instead of spinning in SQLite's busy handler. Readers never take it.
  [[nodiscard]] std::mutex& writer_mutex() noexcept { return writer_mutex_; }

 private:
  SqliteRepository(std::string path, Options options);
  [[nodiscard]] Result<std::unique_ptr<Connection>> open_connection() const;

  std::string path_;
  Options options_;
  std::mutex writer_mutex_;
  std::mutex pool_mutex_;
  std::condition_variable pool_available_;
  std::vector<std::unique_ptr<Connection>> pool_;
  std::size_t outstanding_{0};
  std::atomic<Fault> fault_{Fault::None};
};

}  // namespace corebank::sqlite
