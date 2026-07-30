#include "corebank/application/bounded_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace corebank {
namespace {

TEST(BoundedQueue, FifoWithinCapacity) {
  BoundedQueue<int> queue{2};
  EXPECT_TRUE(queue.try_push(1));
  EXPECT_TRUE(queue.try_push(2));
  EXPECT_FALSE(queue.try_push(3));
  EXPECT_EQ(queue.size(), 2U);
  const std::stop_source source;
  EXPECT_EQ(queue.pop(source.get_token()).value(), 1);
  EXPECT_EQ(queue.pop(source.get_token()).value(), 2);
  EXPECT_EQ(queue.size(), 0U);
}

TEST(BoundedQueue, TryPushForTimesOutWhenFullAndKeepsItem) {
  BoundedQueue<std::string> queue{1};
  std::string first = "a";
  std::string second = "b";
  EXPECT_TRUE(queue.try_push_for(first, std::chrono::milliseconds{10}));
  EXPECT_FALSE(queue.try_push_for(second, std::chrono::milliseconds{20}));
  EXPECT_EQ(second, "b");  // not moved from
}

TEST(BoundedQueue, PopWakesOnStopRequest) {
  BoundedQueue<int> queue{4};
  std::stop_source source;
  std::atomic<bool> returned{false};
  std::thread consumer([&] {
    const auto item = queue.pop(source.get_token());
    EXPECT_FALSE(item.has_value());
    returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  EXPECT_FALSE(returned.load());
  source.request_stop();
  consumer.join();
  EXPECT_TRUE(returned.load());
}

TEST(BoundedQueue, CloseDrainsRemainingItemsThenReturnsNullopt) {
  BoundedQueue<int> queue{4};
  EXPECT_TRUE(queue.try_push(7));
  queue.close();
  EXPECT_FALSE(queue.try_push(8));
  const std::stop_source source;
  EXPECT_EQ(queue.pop(source.get_token()).value(), 7);
  EXPECT_FALSE(queue.pop(source.get_token()).has_value());
  EXPECT_TRUE(queue.closed());
}

TEST(BoundedQueue, ProducersAndConsumersSeeEveryItemOnce) {
  BoundedQueue<int> queue{8};
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 500;
  std::atomic<long long> sum{0};
  std::atomic<int> consumed{0};
  std::stop_source source;
  std::vector<std::thread> consumers;
  for (int c = 0; c < 3; ++c) {
    consumers.emplace_back([&] {
      while (auto item = queue.pop(source.get_token())) {
        sum.fetch_add(*item);
        consumed.fetch_add(1);
      }
    });
  }
  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        int value = p * kPerProducer + i;
        while (!queue.try_push_for(value, std::chrono::milliseconds{50})) {
        }
      }
    });
  }
  for (auto& t : producers) {
    t.join();
  }
  queue.close();
  for (auto& t : consumers) {
    t.join();
  }
  const long long n = kProducers * kPerProducer;
  EXPECT_EQ(consumed.load(), n);
  EXPECT_EQ(sum.load(), n * (n - 1) / 2);
}

}  // namespace
}  // namespace corebank
