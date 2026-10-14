#include "corebank/application/account_lock_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace corebank {
namespace {

AccountId id(const std::string& text) {
  return AccountId::parse(text).value();
}

TEST(AccountLockManager, DeduplicatesStripesAndIsStable) {
  AccountLockManager locks{8};
  EXPECT_EQ(locks.stripe_count(), 8U);
  EXPECT_EQ(locks.stripe_of(id("A")), locks.stripe_of(id("A")));
  const std::vector<AccountId> same{id("A"), id("A")};
  EXPECT_EQ(locks.lock_exclusive(same).lock_count(), 1U);
  std::vector<AccountId> many;
  for (int i = 0; i < 100; ++i) {
    many.push_back(id("ACC-" + std::to_string(i)));
  }
  EXPECT_LE(locks.lock_exclusive(many).lock_count(), 8U);
}

TEST(AccountLockManager, ExclusiveLocksSerialiseOverlappingSets) {
  AccountLockManager locks{64};
  std::atomic<int> inside{0};
  std::atomic<int> max_inside{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&, t] {
      while (!go.load()) {
      }
      for (int i = 0; i < 200; ++i) {
        // Every thread touches the shared account "HUB" plus a private one, in varying order.
        std::vector<AccountId> ids =
            t % 2 == 0 ? std::vector<AccountId>{id("HUB"), id("P" + std::to_string(t))}
                       : std::vector<AccountId>{id("P" + std::to_string(t)), id("HUB")};
        const auto guard = locks.lock_exclusive(ids);
        const int now = inside.fetch_add(1) + 1;
        int seen = max_inside.load();
        while (now > seen && !max_inside.compare_exchange_weak(seen, now)) {
        }
        inside.fetch_sub(1);
      }
    });
  }
  go.store(true);
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(max_inside.load(), 1);
}

TEST(AccountLockManager, SharedLocksCoexistButExcludeWriters) {
  AccountLockManager locks{4};
  auto reader1 = locks.lock_shared(id("X"));
  auto reader2 = locks.lock_shared(id("X"));
  std::atomic<bool> writer_done{false};
  std::thread writer([&] {
    const std::vector<AccountId> ids{id("X")};
    const auto guard = locks.lock_exclusive(ids);
    writer_done.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  EXPECT_FALSE(writer_done.load());
  { auto release1 = std::move(reader1); }
  { auto release2 = std::move(reader2); }
  writer.join();
  EXPECT_TRUE(writer_done.load());
}

}  // namespace
}  // namespace corebank
