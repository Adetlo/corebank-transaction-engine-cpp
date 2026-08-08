// Deadlock safety: opposite-direction transfers over overlapping account sets must always
// finish. A hang here would be a lock-ordering bug; the test has a hard deadline.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class DeadlockTest : public EngineFixture {};

TEST_F(DeadlockTest, OppositeDirectionPairsCompleteWithinDeadline) {
  const std::vector<std::string> accounts{"D0", "D1", "D2", "D3"};
  for (const auto& id : accounts) {
    (void)make_account(id);
    (void)fund(id, 100'000);
  }
  constexpr int kRounds = 100;
  std::atomic<int> completed{0};
  auto worker = [&](std::string from, std::string to) {
    for (int i = 0; i < kRounds; ++i) {
      ASSERT_TRUE(service
                      ->transfer(TransferRequest{.key = key(),
                                                 .source = acc(from),
                                                 .destination = acc(to),
                                                 .amount = gbp(1),
                                                 .reference = ref("dl")})
                      .has_value());
      completed.fetch_add(1);
    }
  };
  auto all = std::async(std::launch::async, [&] {
    std::vector<std::thread> threads;
    threads.emplace_back(worker, "D0", "D1");
    threads.emplace_back(worker, "D1", "D0");
    threads.emplace_back(worker, "D1", "D2");
    threads.emplace_back(worker, "D2", "D1");
    threads.emplace_back(worker, "D2", "D3");
    threads.emplace_back(worker, "D3", "D2");
    threads.emplace_back(worker, "D3", "D0");
    threads.emplace_back(worker, "D0", "D3");
    for (auto& thread : threads) {
      thread.join();
    }
  });
  ASSERT_EQ(all.wait_for(std::chrono::seconds{120}), std::future_status::ready)
      << "possible deadlock: only " << completed.load() << " transfers completed";
  EXPECT_EQ(completed.load(), 8 * kRounds);
  std::int64_t total = 0;
  for (const auto& id : accounts) {
    total += balance(id);
  }
  EXPECT_EQ(total, 400'000);
}

TEST_F(DeadlockTest, ReadersDoNotStarveWriters) {
  (void)make_account("R0");
  (void)make_account("R1");
  (void)fund("R0", 10'000);
  std::atomic<bool> stop{false};
  std::thread reader([&] {
    while (!stop.load()) {
      ASSERT_TRUE(service->account_history(acc("R0"), 5).has_value());
    }
  });
  for (int i = 0; i < 50; ++i) {
    ASSERT_TRUE(transfer("R0", "R1", 1).posted());
  }
  stop.store(true);
  reader.join();
  EXPECT_EQ(balance("R1"), 50);
}

}  // namespace
}  // namespace corebank::testing
