#include "corebank/application/transaction_processor.hpp"
#include "corebank/infrastructure/signal_handler.hpp"
#include "corebank/messaging/message_handler.hpp"
#include "corebank/messaging/tcp_server.hpp"

#include <chrono>
#include <csignal>
#include <future>
#include <thread>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

using namespace std::chrono_literals;

TEST(SignalHandler, SigtermSetsStopFlagAndWakesWaiter) {
  ASSERT_TRUE(SignalHandler::install().has_value());
  SignalHandler::reset();
  EXPECT_FALSE(SignalHandler::stop_requested());
  EXPECT_FALSE(SignalHandler::wait(50ms));
  std::thread sender([] {
    std::this_thread::sleep_for(50ms);
    std::raise(SIGTERM);
  });
  const auto started = std::chrono::steady_clock::now();
  EXPECT_TRUE(SignalHandler::wait(5s));
  EXPECT_LT(std::chrono::steady_clock::now() - started, 4s);
  sender.join();
  EXPECT_TRUE(SignalHandler::stop_requested());
  EXPECT_EQ(SignalHandler::last_signal(), SIGTERM);
  SignalHandler::uninstall();
  EXPECT_FALSE(SignalHandler::stop_requested());
}

TEST(SignalHandler, SigintAlsoStops) {
  ASSERT_TRUE(SignalHandler::install().has_value());
  SignalHandler::reset();
  std::raise(SIGINT);
  EXPECT_TRUE(SignalHandler::wait(1s));
  EXPECT_EQ(SignalHandler::last_signal(), SIGINT);
  SignalHandler::uninstall();
}

// End-to-end graceful shutdown: a signal arrives, the server stops accepting, queued
// work is finished, nothing is lost or double-posted.
TEST(GracefulShutdown, SignalDrivenShutdownFinishesQueuedWork) {
  TempDir temp;
  auto repository =
      sqlite::SqliteRepository::open(
          temp.db_path(), sqlite::SqliteOptions{.pool_size = 6, .busy_timeout_ms = 10000})
          .value();
  ASSERT_TRUE(repository->migrate().has_value());
  SystemClock clock;
  IdGenerator ids;
  TransactionService service{*repository, clock, ids};
  ASSERT_TRUE(service
                  .create_account(CreateAccountRequest{.id = acc("A"),
                                                       .name = "A",
                                                       .type = AccountType::Customer,
                                                       .currency = CurrencyCode::unchecked("GBP"),
                                                       .allow_overdraft = false})
                  .has_value());
  ASSERT_TRUE(service
                  .create_account(CreateAccountRequest{.id = acc("B"),
                                                       .name = "B",
                                                       .type = AccountType::Customer,
                                                       .currency = CurrencyCode::unchecked("GBP"),
                                                       .allow_overdraft = false})
                  .has_value());
  ASSERT_TRUE(service
                  .credit(CreditRequest{.key = ids.next_idempotency_key(),
                                        .account = acc("A"),
                                        .amount = gbp(100'000),
                                        .reference = ref("seed")})
                  .value()
                  .posted());

  TransactionProcessor processor{
      service, ProcessorOptions{.worker_count = 2, .queue_capacity = 256, .enqueue_timeout = 5s}};
  processor.start();
  MessageHandler handler{service, processor, 5s, 1024};
  TcpServer server{TcpServerOptions{.address = "127.0.0.1",
                                    .port = 0,
                                    .max_connections = 8,
                                    .idle_timeout = 5s,
                                    .max_line_bytes = 1024},
                   handler};
  ASSERT_TRUE(server.start().has_value());
  ASSERT_TRUE(SignalHandler::install().has_value());
  SignalHandler::reset();

  std::vector<std::future<Result<TransactionReceipt>>> futures;
  for (int i = 0; i < 60; ++i) {
    futures.push_back(processor.submit(TransferRequest{.key = ids.next_idempotency_key(),
                                                       .source = acc("A"),
                                                       .destination = acc("B"),
                                                       .amount = gbp(1),
                                                       .reference = ref("q")}));
  }
  std::raise(SIGTERM);
  ASSERT_TRUE(SignalHandler::wait(2s));

  server.stop();
  processor.shutdown_gracefully();
  EXPECT_FALSE(processor.accepting());
  int posted = 0;
  for (auto& future : futures) {
    auto outcome = future.get();
    ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
    posted += outcome.value().posted() ? 1 : 0;
  }
  EXPECT_EQ(posted, 60);
  EXPECT_EQ(service.get_account(acc("B")).value().balance.minor(), 60);
  auto late = processor.submit(CreditRequest{.key = ids.next_idempotency_key(),
                                             .account = acc("A"),
                                             .amount = gbp(1),
                                             .reference = ref("late")});
  EXPECT_EQ(late.get().error().code, ErrorCode::ShuttingDown);
  SignalHandler::uninstall();
}

}  // namespace
}  // namespace corebank::testing
