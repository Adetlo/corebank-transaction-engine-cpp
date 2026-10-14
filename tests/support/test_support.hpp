// Shared fixtures: temporary SQLite database, fixed clock, seeded accounts.
#pragma once

#include "corebank/application/id_generator.hpp"
#include "corebank/application/transaction_service.hpp"
#include "corebank/domain/time.hpp"
#include "corebank/infrastructure/sqlite/sqlite_repository.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace corebank::testing {

class FakeClock final : public Clock {
 public:
  explicit FakeClock(Timestamp start) : now_(start) {}
  [[nodiscard]] Timestamp now() const override { return now_; }
  void advance(std::chrono::microseconds delta) { now_ += delta; }
  void set(Timestamp ts) { now_ = ts; }

 private:
  Timestamp now_;
};

// Creates a unique temporary directory and removes it on destruction.
class TempDir {
 public:
  TempDir();
  ~TempDir();
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  [[nodiscard]] std::string db_path() const { return (path_ / "test.db").string(); }

 private:
  std::filesystem::path path_;
};

// Fixture with an open, migrated repository and a service wired to a real clock.
class EngineFixture : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;

  [[nodiscard]] Account make_account(const std::string& id, std::string_view ccy = "GBP",
                                     bool overdraft = false,
                                     AccountType type = AccountType::Customer);
  [[nodiscard]] TransactionReceipt fund(const std::string& id, std::int64_t minor,
                                        std::string_view ccy = "GBP");
  [[nodiscard]] TransactionReceipt transfer(const std::string& from, const std::string& to,
                                            std::int64_t minor, const std::string& key = "",
                                            std::string_view ccy = "GBP");
  [[nodiscard]] std::int64_t balance(const std::string& id);
  [[nodiscard]] IdempotencyKey key(const std::string& text = "");

  TempDir temp;
  std::unique_ptr<sqlite::SqliteRepository> repository;
  SystemClock clock;
  IdGenerator ids{42};
  std::unique_ptr<TransactionService> service;
};

[[nodiscard]] AccountId acc(const std::string& text);
[[nodiscard]] Money gbp(std::int64_t minor);
[[nodiscard]] Money money(std::int64_t minor, std::string_view ccy);
[[nodiscard]] JournalReference ref(const std::string& text);

}  // namespace corebank::testing
