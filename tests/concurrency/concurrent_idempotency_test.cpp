#include <atomic>
#include <barrier>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class ConcurrentIdempotencyTest : public EngineFixture {};

TEST_F(ConcurrentIdempotencyTest, ManyThreadsSameKeyCommitExactlyOnce) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 10'000);
  constexpr int kThreads = 12;
  std::barrier sync(kThreads);
  std::atomic<int> replayed{0};
  std::mutex ids_mutex;
  std::set<std::string> transaction_ids;
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      sync.arrive_and_wait();  // maximise overlap
      auto receipt = service->transfer(TransferRequest{.key = key("SAME-KEY"),
                                                       .source = acc("CUST-A"),
                                                       .destination = acc("CUST-B"),
                                                       .amount = gbp(1'000),
                                                       .reference = ref("dup")});
      ASSERT_TRUE(receipt.has_value()) << receipt.error().message;
      EXPECT_TRUE(receipt.value().posted());
      if (receipt.value().replayed) {
        replayed.fetch_add(1);
      }
      const std::lock_guard lock(ids_mutex);
      transaction_ids.insert(receipt.value().transaction.id.str());
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(transaction_ids.size(), 1U);
  EXPECT_EQ(replayed.load(), kThreads - 1);
  EXPECT_EQ(balance("CUST-A"), 9'000);
  EXPECT_EQ(balance("CUST-B"), 1'000);
  EXPECT_EQ(service->account_transactions(acc("CUST-B")).value().size(), 1U);
  EXPECT_EQ(service->gate().in_flight(), 0U);
}

TEST_F(ConcurrentIdempotencyTest, ConcurrentConflictingRequestsYieldOneCommitAndConflicts) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 10'000);
  constexpr int kThreads = 8;
  std::barrier sync(kThreads);
  std::atomic<int> posted{0};
  std::atomic<int> conflicts{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      sync.arrive_and_wait();
      auto receipt = service->transfer(TransferRequest{.key = key("CONFLICT-KEY"),
                                                       .source = acc("CUST-A"),
                                                       .destination = acc("CUST-B"),
                                                       .amount = gbp(100 + t),  // each differs
                                                       .reference = ref("c")});
      if (receipt) {
        if (!receipt.value().replayed) {
          posted.fetch_add(1);
        }
      } else {
        EXPECT_EQ(receipt.error().code, ErrorCode::IdempotencyConflict);
        conflicts.fetch_add(1);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(posted.load(), 1);
  EXPECT_EQ(conflicts.load(), kThreads - 1);
  EXPECT_EQ(service->account_transactions(acc("CUST-B")).value().size(), 1U);
}

}  // namespace
}  // namespace corebank::testing
