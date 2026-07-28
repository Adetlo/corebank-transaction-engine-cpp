#include "test_support.hpp"

#include "corebank/infrastructure/logger.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace corebank::testing {

TempDir::TempDir() {
  std::random_device device;
  path_ = std::filesystem::temp_directory_path() /
          ("corebank-test-" + std::to_string(device()) + "-" + std::to_string(device()));
  std::filesystem::create_directories(path_);
}

TempDir::~TempDir() {
  std::error_code ec;
  std::filesystem::remove_all(path_, ec);
}

void EngineFixture::SetUp() {
  Logger::instance().set_level(LogLevel::Error);
  auto opened = sqlite::SqliteRepository::open(
      temp.db_path(), sqlite::SqliteRepository::Options{.pool_size = 8, .busy_timeout_ms = 10000});
  ASSERT_TRUE(opened.has_value()) << opened.error().message;
  repository = std::move(opened.value());
  ASSERT_TRUE(repository->migrate().has_value());
  service = std::make_unique<TransactionService>(*repository, clock, ids);
}

void EngineFixture::TearDown() {
  service.reset();
  repository.reset();
}

Account EngineFixture::make_account(const std::string& id, std::string_view ccy, bool overdraft,
                                    AccountType type) {
  auto created =
      service->create_account(CreateAccountRequest{.id = acc(id),
                                                   .name = "Account " + id,
                                                   .type = type,
                                                   .currency = CurrencyCode::parse(ccy).value(),
                                                   .allow_overdraft = overdraft});
  EXPECT_TRUE(created.has_value()) << created.error().message;
  return created.value();
}

TransactionReceipt EngineFixture::fund(const std::string& id, std::int64_t minor,
                                       std::string_view ccy) {
  auto receipt = service->credit(CreditRequest{
      .key = key(), .account = acc(id), .amount = money(minor, ccy), .reference = ref("fund")});
  EXPECT_TRUE(receipt.has_value()) << receipt.error().message;
  return receipt.value();
}

TransactionReceipt EngineFixture::transfer(const std::string& from, const std::string& to,
                                           std::int64_t minor, const std::string& k,
                                           std::string_view ccy) {
  auto receipt = service->transfer(TransferRequest{.key = key(k),
                                                   .source = acc(from),
                                                   .destination = acc(to),
                                                   .amount = money(minor, ccy),
                                                   .reference = ref("transfer")});
  EXPECT_TRUE(receipt.has_value()) << receipt.error().message;
  return receipt.value();
}

std::int64_t EngineFixture::balance(const std::string& id) {
  auto account = service->get_account(acc(id));
  EXPECT_TRUE(account.has_value()) << account.error().message;
  return account ? account.value().balance.minor() : -1;
}

IdempotencyKey EngineFixture::key(const std::string& text) {
  if (text.empty()) {
    return ids.next_idempotency_key();
  }
  return IdempotencyKey::parse(text).value();
}

AccountId acc(const std::string& text) {
  return AccountId::parse(text).value();
}

Money gbp(std::int64_t minor) {
  return Money{minor, CurrencyCode::unchecked("GBP")};
}

Money money(std::int64_t minor, std::string_view ccy) {
  return Money{minor, CurrencyCode::unchecked(ccy)};
}

JournalReference ref(const std::string& text) {
  return JournalReference::parse(text).value();
}

}  // namespace corebank::testing
