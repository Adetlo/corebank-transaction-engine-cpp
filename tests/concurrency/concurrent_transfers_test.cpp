// Proves balance consistency under contention. Run under ThreadSanitizer in CI.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class ConcurrentTransfersTest : public EngineFixture {
 protected:
  std::int64_t total_balance(const std::vector<std::string>& accounts) {
    std::int64_t sum = 0;
    for (const auto& id : accounts) {
      sum += balance(id);
    }
    return sum;
  }
};

TEST_F(ConcurrentTransfersTest, SameAccountContentionNeverCorruptsBalances) {
  (void)make_account("HUB");
  for (int i = 0; i < 8; ++i) {
    (void)make_account("SPOKE-" + std::to_string(i));
  }
  (void)fund("HUB", 1'000'000);
  constexpr int kThreads = 8;
  constexpr int kPerThread = 50;
  std::atomic<int> posted{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        auto receipt = service->transfer(TransferRequest{
            .key =
                IdempotencyKey::parse("C-" + std::to_string(t) + "-" + std::to_string(i)).value(),
            .source = acc("HUB"),
            .destination = acc("SPOKE-" + std::to_string(t)),
            .amount = gbp(100),
            .reference = ref("contention")});
        ASSERT_TRUE(receipt.has_value()) << receipt.error().message;
        if (receipt.value().posted()) {
          posted.fetch_add(1);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(posted.load(), kThreads * kPerThread);
  EXPECT_EQ(balance("HUB"), 1'000'000 - 100 * kThreads * kPerThread);
  for (int t = 0; t < kThreads; ++t) {
    EXPECT_EQ(balance("SPOKE-" + std::to_string(t)), 100 * kPerThread);
  }
}

TEST_F(ConcurrentTransfersTest, OppositeDirectionTransfersConserveMoney) {
  (void)make_account("LEFT");
  (void)make_account("RIGHT");
  (void)fund("LEFT", 50'000);
  (void)fund("RIGHT", 50'000);
  constexpr int kRounds = 150;
  std::thread left_to_right([&] {
    for (int i = 0; i < kRounds; ++i) {
      ASSERT_TRUE(service
                      ->transfer(TransferRequest{.key = key(),
                                                 .source = acc("LEFT"),
                                                 .destination = acc("RIGHT"),
                                                 .amount = gbp(70),
                                                 .reference = ref("l2r")})
                      .has_value());
    }
  });
  std::thread right_to_left([&] {
    for (int i = 0; i < kRounds; ++i) {
      ASSERT_TRUE(service
                      ->transfer(TransferRequest{.key = key(),
                                                 .source = acc("RIGHT"),
                                                 .destination = acc("LEFT"),
                                                 .amount = gbp(30),
                                                 .reference = ref("r2l")})
                      .has_value());
    }
  });
  left_to_right.join();
  right_to_left.join();
  EXPECT_EQ(balance("LEFT") + balance("RIGHT"), 100'000);
  EXPECT_EQ(balance("LEFT"), 50'000 - 70 * kRounds + 30 * kRounds);
  EXPECT_GE(balance("LEFT"), 0);
  EXPECT_GE(balance("RIGHT"), 0);
}

TEST_F(ConcurrentTransfersTest, RandomisedMeshKeepsTotalAndNonNegativeBalances) {
  const std::vector<std::string> accounts{"M0", "M1", "M2", "M3", "M4", "M5"};
  for (const auto& id : accounts) {
    (void)make_account(id);
    (void)fund(id, 10'000);
  }
  const std::int64_t expected_total = 60'000;
  constexpr int kThreads = 6;
  constexpr int kOps = 120;
  std::atomic<int> rejected{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      std::mt19937 rng(static_cast<unsigned>(1000 + t));
      std::uniform_int_distribution<std::size_t> pick(0, accounts.size() - 1);
      std::uniform_int_distribution<std::int64_t> amount(1, 4'000);
      for (int i = 0; i < kOps; ++i) {
        const auto from = pick(rng);
        auto to = pick(rng);
        if (to == from) {
          to = (to + 1) % accounts.size();
        }
        auto receipt = service->transfer(TransferRequest{.key = key(),
                                                         .source = acc(accounts[from]),
                                                         .destination = acc(accounts[to]),
                                                         .amount = gbp(amount(rng)),
                                                         .reference = ref("mesh")});
        ASSERT_TRUE(receipt.has_value()) << receipt.error().message;
        if (!receipt.value().posted()) {
          EXPECT_EQ(receipt.value().transaction.rejection->code, ErrorCode::InsufficientFunds);
          rejected.fetch_add(1);
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(total_balance(accounts), expected_total);
  for (const auto& id : accounts) {
    EXPECT_GE(balance(id), 0) << id;
  }
  // Balances must also agree with the journal, not just with each other.
  for (const auto& id : accounts) {
    std::int64_t derived = 0;
    const auto history = service->account_history(acc(id), 100'000).value();
    for (const auto& posting : history) {
      derived += signed_minor(posting);
    }
    EXPECT_EQ(derived, balance(id)) << id;
  }
  SUCCEED() << rejected.load() << " transfers were rejected for insufficient funds";
}

}  // namespace
}  // namespace corebank::testing
