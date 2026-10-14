#include "corebank/application/transaction_processor.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class ProcessorTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("CUST-A", 1'000'000);
  }
  TransferRequest request(std::int64_t minor = 10) {
    return TransferRequest{.key = key(),
                           .source = acc("CUST-A"),
                           .destination = acc("CUST-B"),
                           .amount = gbp(minor),
                           .reference = ref("p")};
  }
};

TEST_F(ProcessorTest, ProcessesSubmittedWorkOnWorkerThreads) {
  TransactionProcessor processor{
      *service,
      ProcessorOptions{
          .worker_count = 4, .queue_capacity = 64, .enqueue_timeout = std::chrono::seconds{5}}};
  processor.start();
  std::vector<std::future<Result<TransactionReceipt>>> futures;
  for (int i = 0; i < 200; ++i) {
    futures.push_back(processor.submit(request()));
  }
  int posted = 0;
  for (auto& future : futures) {
    auto outcome = future.get();
    ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
    posted += outcome.value().posted() ? 1 : 0;
  }
  EXPECT_EQ(posted, 200);
  EXPECT_EQ(balance("CUST-B"), 2'000);
  const auto stats = processor.stats();
  EXPECT_EQ(stats.completed, 200U);
  EXPECT_EQ(stats.submitted, 200U);
  EXPECT_TRUE(stats.accepting);
  processor.shutdown_gracefully();
  EXPECT_FALSE(processor.accepting());
}

TEST_F(ProcessorTest, SubmitAfterShutdownFailsFast) {
  TransactionProcessor processor{
      *service, ProcessorOptions{.worker_count = 1,
                                 .queue_capacity = 4,
                                 .enqueue_timeout = std::chrono::milliseconds{50}}};
  processor.start();
  processor.shutdown_gracefully();
  auto outcome = processor.submit(request()).get();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().code, ErrorCode::ShuttingDown);
  EXPECT_EQ(processor.stats().rejected_shutting_down, 1U);
}

TEST_F(ProcessorTest, BoundedQueueAppliesBackPressure) {
  // No workers started: nothing drains the queue, so capacity+timeout decide the outcome.
  TransactionProcessor processor{
      *service, ProcessorOptions{.worker_count = 0,
                                 .queue_capacity = 2,
                                 .enqueue_timeout = std::chrono::milliseconds{30}}};
  processor.start();
  auto f1 = processor.submit(request());
  auto f2 = processor.submit(request());
  auto f3 = processor.submit(request());
  EXPECT_EQ(f3.wait_for(std::chrono::seconds{2}), std::future_status::ready);
  auto overflow = f3.get();
  ASSERT_FALSE(overflow.has_value());
  EXPECT_EQ(overflow.error().code, ErrorCode::QueueFull);
  EXPECT_EQ(processor.stats().rejected_queue_full, 1U);
  EXPECT_EQ(processor.stats().queued, 2U);
  processor.shutdown_now();
  for (auto* f : {&f1, &f2}) {
    auto outcome = f->get();
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().code, ErrorCode::ShuttingDown);
  }
  EXPECT_EQ(balance("CUST-B"), 0);
}

TEST_F(ProcessorTest, GracefulShutdownDrainsQueuedWork) {
  TransactionProcessor processor{
      *service,
      ProcessorOptions{
          .worker_count = 2, .queue_capacity = 512, .enqueue_timeout = std::chrono::seconds{5}}};
  processor.start();
  std::vector<std::future<Result<TransactionReceipt>>> futures;
  for (int i = 0; i < 100; ++i) {
    futures.push_back(processor.submit(request(1)));
  }
  processor.shutdown_gracefully();  // returns only after the queue is empty
  for (auto& future : futures) {
    auto outcome = future.get();
    ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
    EXPECT_TRUE(outcome.value().posted());
  }
  EXPECT_EQ(balance("CUST-B"), 100);
  EXPECT_EQ(processor.stats().completed, 100U);
}

TEST_F(ProcessorTest, DestructorStopsWorkersWithoutLeakingPromises) {
  std::future<Result<TransactionReceipt>> pending;
  {
    TransactionProcessor processor{
        *service, ProcessorOptions{.worker_count = 0,
                                   .queue_capacity = 8,
                                   .enqueue_timeout = std::chrono::milliseconds{10}}};
    processor.start();
    pending = processor.submit(request());
  }
  auto outcome = pending.get();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().code, ErrorCode::ShuttingDown);
}

}  // namespace
}  // namespace corebank::testing
