#include "corebank/messaging/tcp_server.hpp"

#include "corebank/application/transaction_processor.hpp"
#include "corebank/messaging/message_handler.hpp"
#include "corebank/messaging/tcp_client.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

using namespace std::chrono_literals;

class TcpServerTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    processor = std::make_unique<TransactionProcessor>(
        *service, ProcessorOptions{.worker_count = 2, .queue_capacity = 32, .enqueue_timeout = 2s});
    processor->start();
    handler = std::make_unique<MessageHandler>(*service, *processor, 5s, 256);
    server = std::make_unique<TcpServer>(TcpServerOptions{.address = "127.0.0.1",
                                                          .port = 0,
                                                          .max_connections = 2,
                                                          .idle_timeout = 1s,
                                                          .max_line_bytes = 256},
                                         *handler);
    ASSERT_TRUE(server->start().has_value());
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("CUST-A", 10'000);
  }
  void TearDown() override {
    server->stop();
    server.reset();
    handler.reset();
    processor->shutdown_gracefully();
    processor.reset();
    EngineFixture::TearDown();
  }
  TcpClient connect() {
    auto client = TcpClient::connect("127.0.0.1", server->port(), 2s);
    EXPECT_TRUE(client.has_value()) << client.error().message;
    return std::move(client.value());
  }
  std::unique_ptr<TransactionProcessor> processor;
  std::unique_ptr<MessageHandler> handler;
  std::unique_ptr<TcpServer> server;
};

TEST_F(TcpServerTest, PingAndTransferOverTheWire) {
  auto client = connect();
  const auto pong = client.request("PING", 2s).value();
  EXPECT_EQ(pong.rfind("OK|pong=1", 0), 0U) << pong;
  const auto transfer =
      client.request("TRANSFER|key=W1|from=CUST-A|to=CUST-B|amount=2.00|ccy=GBP", 5s).value();
  EXPECT_NE(transfer.find("|status=POSTED|"), std::string::npos) << transfer;
  EXPECT_EQ(balance("CUST-B"), 200);
  EXPECT_EQ(server->total_connections(), 1U);
}

TEST_F(TcpServerTest, MultipleMessagesOnOneConnectionAndPipelining) {
  auto client = connect();
  ASSERT_TRUE(client.send("PING\nPING\r\nBALANCE|account=CUST-A\n").has_value());
  EXPECT_EQ(client.receive_line(2s).value().rfind("OK|pong", 0), 0U);
  EXPECT_EQ(client.receive_line(2s).value().rfind("OK|pong", 0), 0U);
  EXPECT_NE(client.receive_line(2s).value().find("|balance=100.00|"), std::string::npos);
}

TEST_F(TcpServerTest, MalformedAndOversizedInputGetStructuredErrors) {
  auto client = connect();
  EXPECT_EQ(client.request("not a message", 2s).value().rfind("ERR|code=MALFORMED_MESSAGE", 0), 0U);
  EXPECT_EQ(client.request("TRANSFER|key=1", 2s).value().rfind("ERR|code=MISSING_FIELD", 0), 0U);
  EXPECT_EQ(client.request("TRANSFER|key=1|from=CUST-A|to=CUST-B|amount=1|ccy=ZZZ", 2s)
                .value()
                .rfind("ERR|code=INVALID_CURRENCY", 0),
            0U);
  // A line longer than max_line_bytes without a newline is cut off and the socket closed.
  ASSERT_TRUE(client.send(std::string(300, 'X')).has_value());
  const auto response = client.receive_line(2s).value();
  EXPECT_EQ(response.rfind("ERR|code=PAYLOAD_TOO_LARGE", 0), 0U) << response;
  EXPECT_FALSE(client.receive_line(500ms).has_value());  // closed by server
}

TEST_F(TcpServerTest, IdleConnectionsTimeOut) {
  auto client = connect();
  const auto started = std::chrono::steady_clock::now();
  const auto response = client.receive_line(5s);
  ASSERT_TRUE(response.has_value()) << response.error().message;
  EXPECT_EQ(response.value().rfind("ERR|code=TIMEOUT", 0), 0U);
  EXPECT_GE(std::chrono::steady_clock::now() - started, 900ms);
}

TEST_F(TcpServerTest, ConnectionLimitIsEnforced) {
  auto first = connect();
  auto second = connect();
  ASSERT_TRUE(first.request("PING", 2s).has_value());
  ASSERT_TRUE(second.request("PING", 2s).has_value());
  auto third = connect();
  const auto refused = third.receive_line(2s);
  ASSERT_TRUE(refused.has_value()) << refused.error().message;
  EXPECT_EQ(refused.value().rfind("ERR|code=TOO_MANY_CONNECTIONS", 0), 0U);
  first.close();
  std::this_thread::sleep_for(300ms);  // let the acceptor reap the closed connection
  auto fourth = connect();
  EXPECT_TRUE(fourth.request("PING", 2s).has_value());
}

TEST_F(TcpServerTest, DuplicateMessageOverTheWireIsReplayed) {
  auto client = connect();
  const auto a = client.request("CREDIT|key=NET-DUP|account=CUST-B|amount=3|ccy=GBP", 5s).value();
  const auto b = client.request("CREDIT|key=NET-DUP|account=CUST-B|amount=3|ccy=GBP", 5s).value();
  EXPECT_NE(a.find("|replayed=0"), std::string::npos);
  EXPECT_NE(b.find("|replayed=1"), std::string::npos);
  EXPECT_EQ(balance("CUST-B"), 300);
}

TEST_F(TcpServerTest, ConcurrentClientsAreServed) {
  std::vector<std::thread> clients;
  std::atomic<int> ok{0};
  for (int c = 0; c < 2; ++c) {
    clients.emplace_back([&, c] {
      auto client = TcpClient::connect("127.0.0.1", server->port(), 2s).value();
      for (int i = 0; i < 20; ++i) {
        const auto key = "CC-" + std::to_string(c) + "-" + std::to_string(i);
        auto response =
            client.request("TRANSFER|key=" + key + "|from=CUST-A|to=CUST-B|amount=1|ccy=GBP", 5s);
        if (response && response.value().find("|status=POSTED|") != std::string::npos) {
          ok.fetch_add(1);
        }
      }
    });
  }
  for (auto& thread : clients) {
    thread.join();
  }
  EXPECT_EQ(ok.load(), 40);
  EXPECT_EQ(balance("CUST-B"), 4'000);
}

TEST_F(TcpServerTest, StopClosesClientsAndIsIdempotent) {
  auto client = connect();
  ASSERT_TRUE(client.request("PING", 2s).has_value());
  server->stop();
  server->stop();
  EXPECT_FALSE(server->running());
  EXPECT_FALSE(client.request("PING", 500ms).has_value());
  EXPECT_FALSE(TcpClient::connect("127.0.0.1", server->port(), 500ms).has_value());
}

TEST(TcpServerStandalone, RejectsInvalidListenAddress) {
  TempDir temp;
  auto repository = sqlite::SqliteRepository::open(temp.db_path()).value();
  ASSERT_TRUE(repository->migrate().has_value());
  SystemClock clock;
  IdGenerator ids;
  TransactionService service{*repository, clock, ids};
  TransactionProcessor processor{service, ProcessorOptions{}};
  MessageHandler handler{service, processor, 1s, 256};
  TcpServer server{TcpServerOptions{.address = "not-an-ip",
                                    .port = 0,
                                    .max_connections = 1,
                                    .idle_timeout = 1s,
                                    .max_line_bytes = 256},
                   handler};
  const auto started = server.start();
  ASSERT_FALSE(started.has_value());
  EXPECT_EQ(started.error().code, ErrorCode::ConfigurationError);
}

}  // namespace
}  // namespace corebank::testing
