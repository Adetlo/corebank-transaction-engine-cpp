#include "corebank/infrastructure/sqlite/connection.hpp"
#include "corebank/infrastructure/sqlite/schema.hpp"
#include "corebank/infrastructure/sqlite/sqlite_repository.hpp"
#include "corebank/infrastructure/sqlite/statement.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

TEST(Persistence, MigrateIsIdempotentAndRecordsSchemaVersion) {
  TempDir temp;
  auto repository = sqlite::SqliteRepository::open(temp.db_path()).value();
  ASSERT_TRUE(repository->migrate().has_value());
  ASSERT_TRUE(repository->migrate().has_value());
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  auto stmt = connection->prepare("SELECT COUNT(*), MAX(version) FROM schema_version").value();
  ASSERT_TRUE(stmt.step().value());
  EXPECT_EQ(stmt.column_int64(0), 1);
  EXPECT_EQ(stmt.column_int64(1), sqlite::kSchemaVersion);
  EXPECT_TRUE(repository->health_check().has_value());
}

TEST(Persistence, CreatesParentDirectoriesAndUsesWal) {
  TempDir temp;
  const auto nested = (temp.path() / "deep" / "er" / "engine.db").string();
  auto repository = sqlite::SqliteRepository::open(nested).value();
  ASSERT_TRUE(repository->migrate().has_value());
  auto connection = sqlite::Connection::open(nested).value();
  auto stmt = connection->prepare("PRAGMA journal_mode").value();
  ASSERT_TRUE(stmt.step().value());
  EXPECT_EQ(stmt.column_text(0), "wal");
}

TEST(Persistence, OpenFailsForUnwritableLocation) {
  const auto opened = sqlite::SqliteRepository::open("/proc/definitely/not/writable/x.db");
  if (opened.has_value()) {
    GTEST_SKIP() << "platform allowed the path";
  }
  EXPECT_EQ(opened.error().code, ErrorCode::PersistenceError);
}

TEST(Persistence, UncommittedUnitOfWorkRollsBackOnDestruction) {
  TempDir temp;
  auto repository = sqlite::SqliteRepository::open(temp.db_path()).value();
  ASSERT_TRUE(repository->migrate().has_value());
  const Account account{.id = AccountId::parse("ACC-1").value(),
                        .name = "n",
                        .type = AccountType::Customer,
                        .balance = gbp(0),
                        .allow_overdraft = false,
                        .status = AccountStatus::Active,
                        .version = 0,
                        .created_at = SystemClock{}.now(),
                        .updated_at = SystemClock{}.now()};
  {
    auto uow = repository->begin(TransactionMode::ReadWrite).value();
    ASSERT_TRUE(uow->insert_account(account).has_value());
    // no commit
  }
  auto uow = repository->begin(TransactionMode::ReadOnly).value();
  EXPECT_FALSE(uow->find_account(account.id).value().has_value());
}

TEST(Persistence, OptimisticVersionCheckRejectsStaleWrites) {
  TempDir temp;
  auto repository = sqlite::SqliteRepository::open(temp.db_path()).value();
  ASSERT_TRUE(repository->migrate().has_value());
  const auto id = AccountId::parse("ACC-1").value();
  {
    auto uow = repository->begin(TransactionMode::ReadWrite).value();
    ASSERT_TRUE(uow->insert_account(Account{.id = id,
                                            .name = "n",
                                            .type = AccountType::Customer,
                                            .balance = gbp(0),
                                            .allow_overdraft = false,
                                            .status = AccountStatus::Active,
                                            .version = 0,
                                            .created_at = {},
                                            .updated_at = {}})
                    .has_value());
    ASSERT_TRUE(uow->commit().has_value());
  }
  auto uow = repository->begin(TransactionMode::ReadWrite).value();
  EXPECT_TRUE(uow->update_account_balance(id, 5, 0, {}).has_value());
  const auto stale = uow->update_account_balance(id, 6, 0, {});
  ASSERT_FALSE(stale.has_value());
  EXPECT_EQ(stale.error().code, ErrorCode::PersistenceError);
  EXPECT_EQ(uow->find_account(id).value()->version, 1);
}

TEST(Persistence, ConnectionPoolBlocksUntilAConnectionIsReturned) {
  TempDir temp;
  auto repository =
      sqlite::SqliteRepository::open(temp.db_path(),
                                     sqlite::SqliteOptions{.pool_size = 1, .busy_timeout_ms = 100})
          .value();
  ASSERT_TRUE(repository->migrate().has_value());
  auto first = repository->begin(TransactionMode::ReadOnly).value();
  std::atomic<bool> acquired{false};
  std::thread waiter([&] {
    auto second = repository->begin(TransactionMode::ReadOnly).value();
    acquired.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds{30});
  EXPECT_FALSE(acquired.load());
  first.reset();
  waiter.join();
  EXPECT_TRUE(acquired.load());
}

TEST(Persistence, SchemaRejectsNegativeBalanceWithoutOverdraftAndBadEnums) {
  TempDir temp;
  auto repository = sqlite::SqliteRepository::open(temp.db_path()).value();
  ASSERT_TRUE(repository->migrate().has_value());
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  EXPECT_FALSE(
      connection
          ->exec("INSERT INTO accounts(account_id,name,account_type,currency,balance_minor,"
                 "allow_overdraft,status,version,created_at,updated_at) VALUES "
                 "('A','n','CUSTOMER','GBP',-1,0,'ACTIVE',0,'t','t')")
          .has_value());
  EXPECT_FALSE(
      connection
          ->exec("INSERT INTO accounts(account_id,name,account_type,currency,balance_minor,"
                 "allow_overdraft,status,version,created_at,updated_at) VALUES "
                 "('A','n','VAULT','GBP',0,0,'ACTIVE',0,'t','t')")
          .has_value());
  EXPECT_FALSE(connection
                   ->exec("INSERT INTO postings(transaction_id,account_id,direction,currency,"
                          "amount_minor,balance_after_minor,created_at) VALUES "
                          "('T','A','CREDIT','GBP',0,0,'t')")
                   .has_value());  // amount must be > 0 (and FK targets are missing)
}

TEST(Persistence, StatementBindingIsTypedAndParameterised) {
  TempDir temp;
  auto repository = sqlite::SqliteRepository::open(temp.db_path()).value();
  ASSERT_TRUE(repository->migrate().has_value());
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  // A value that would be dangerous if concatenated is stored verbatim when bound.
  const std::string hostile = "x'); DROP TABLE accounts; --";
  auto insert = connection
                    ->prepare(
                        "INSERT INTO accounts(account_id,name,account_type,currency,balance_minor,"
                        "allow_overdraft,status,version,created_at,updated_at) VALUES "
                        "(?,?,'CUSTOMER','GBP',0,0,'ACTIVE',0,'t','t')")
                    .value();
  ASSERT_TRUE(insert.bind(1, std::string_view{"ACC-H"}).has_value());
  ASSERT_TRUE(insert.bind(2, std::string_view{hostile}).has_value());
  ASSERT_TRUE(insert.execute().has_value());
  auto select = connection->prepare("SELECT name FROM accounts WHERE account_id = ?").value();
  ASSERT_TRUE(select.bind(1, std::string_view{"ACC-H"}).has_value());
  ASSERT_TRUE(select.step().value());
  EXPECT_EQ(select.column_text(0), hostile);
  auto count = connection->prepare("SELECT COUNT(*) FROM accounts").value();
  ASSERT_TRUE(count.step().value());
  EXPECT_EQ(count.column_int64(0), 1);
}

}  // namespace
}  // namespace corebank::testing
