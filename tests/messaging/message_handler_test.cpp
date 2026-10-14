#include "corebank/messaging/message_handler.hpp"

#include "corebank/application/transaction_processor.hpp"

#include <chrono>
#include <memory>
#include <string>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class MessageHandlerTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    processor = std::make_unique<TransactionProcessor>(
        *service,
        ProcessorOptions{
            .worker_count = 2, .queue_capacity = 16, .enqueue_timeout = std::chrono::seconds{2}});
    processor->start();
    handler = std::make_unique<MessageHandler>(*service, *processor, std::chrono::seconds{5}, 4096);
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("CUST-A", 10'000);
  }
  void TearDown() override {
    handler.reset();
    processor->shutdown_gracefully();
    processor.reset();
    EngineFixture::TearDown();
  }
  static bool starts_with(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
  }
  std::unique_ptr<TransactionProcessor> processor;
  std::unique_ptr<MessageHandler> handler;
};

TEST_F(MessageHandlerTest, PingReportsVersionAndProcessorState) {
  const auto response = handler->handle("PING");
  EXPECT_TRUE(starts_with(response, "OK|pong=1|version="));
  EXPECT_NE(response.find("|accepting=1"), std::string::npos);
}

TEST_F(MessageHandlerTest, TransferCreditDebitAndLookups) {
  const auto transfer =
      handler->handle("TRANSFER|key=M1|from=CUST-A|to=CUST-B|amount=12.50|ccy=GBP|ref=rent");
  EXPECT_TRUE(starts_with(transfer, "OK|txn_id=TXN-")) << transfer;
  EXPECT_NE(transfer.find("|status=POSTED|"), std::string::npos);
  EXPECT_NE(transfer.find("|replayed=0"), std::string::npos);
  EXPECT_TRUE(starts_with(handler->handle("CREDIT|key=M2|account=CUST-B|amount=1|ccy=GBP"), "OK|"));
  EXPECT_TRUE(
      starts_with(handler->handle("DEBIT|key=M3|account=CUST-B|amount=0.50|ccy=GBP"), "OK|"));
  const auto balance = handler->handle("BALANCE|account=CUST-B");
  EXPECT_NE(balance.find("|balance=13.00|"), std::string::npos) << balance;
  const auto id = transfer.substr(10, transfer.find('|', 10) - 10);
  const auto get = handler->handle("TXN_GET|txn=" + id);
  EXPECT_NE(
      get.find("|type=TRANSFER|status=POSTED|amount=12.50|ccy=GBP|from=CUST-A|to=CUST-B|ref=rent"),
      std::string::npos)
      << get;
}

TEST_F(MessageHandlerTest, DuplicateMessageIsReplayedAndConflictIsReported) {
  const auto first = handler->handle("TRANSFER|key=DUP|from=CUST-A|to=CUST-B|amount=5|ccy=GBP");
  const auto second = handler->handle("TRANSFER|key=DUP|from=CUST-A|to=CUST-B|amount=5|ccy=GBP");
  EXPECT_NE(second.find("|replayed=1"), std::string::npos);
  EXPECT_EQ(first.substr(0, first.find("|replayed")), second.substr(0, second.find("|replayed")));
  const auto conflict = handler->handle("TRANSFER|key=DUP|from=CUST-A|to=CUST-B|amount=6|ccy=GBP");
  EXPECT_TRUE(starts_with(conflict, "ERR|code=IDEMPOTENCY_CONFLICT|"));
  EXPECT_EQ(balance("CUST-B"), 500);
}

TEST_F(MessageHandlerTest, StructuredErrorsForBadInput) {
  EXPECT_TRUE(starts_with(handler->handle(""), "ERR|code=MALFORMED_MESSAGE|"));
  EXPECT_TRUE(starts_with(handler->handle("transfer|key=1"), "ERR|code=MALFORMED_MESSAGE|"));
  EXPECT_TRUE(
      starts_with(handler->handle("TRANSFER|key=1|from=CUST-A"), "ERR|code=MISSING_FIELD|"));
  EXPECT_TRUE(starts_with(handler->handle("TRANSFER|from=CUST-A|to=CUST-B|amount=1|ccy=GBP"),
                          "ERR|code=MISSING_FIELD|"));
  EXPECT_TRUE(starts_with(handler->handle("TRANSFER|key=1|from=CUST-A|to=CUST-B|amount=1|ccy=XXX"),
                          "ERR|code=INVALID_CURRENCY|"));
  EXPECT_TRUE(
      starts_with(handler->handle("TRANSFER|key=1|from=CUST-A|to=CUST-B|amount=1.234|ccy=GBP"),
                  "ERR|code=INVALID_AMOUNT|"));
  EXPECT_TRUE(starts_with(handler->handle("TRANSFER|key=1|from=CUST-A|to=CUST-B|amount=-1|ccy=GBP"),
                          "ERR|code=INVALID_AMOUNT|"));
  EXPECT_TRUE(starts_with(handler->handle("TRANSFER|key=1|from=CUST-A|to=CUST-A|amount=1|ccy=GBP"),
                          "ERR|code=SAME_ACCOUNT_TRANSFER|"));
  EXPECT_TRUE(starts_with(handler->handle("TRANSFER|key=1|from=bad id|to=CUST-A|amount=1|ccy=GBP"),
                          "ERR|code=INVALID_IDENTIFIER|"));
  EXPECT_TRUE(starts_with(handler->handle("FROBNICATE|x=1"), "ERR|code=UNKNOWN_MESSAGE_TYPE|"));
  EXPECT_TRUE(starts_with(handler->handle(std::string(5000, 'A')), "ERR|code=PAYLOAD_TOO_LARGE|"));
  EXPECT_TRUE(starts_with(handler->handle("BALANCE|account=GHOST"), "ERR|code=ACCOUNT_NOT_FOUND|"));
  EXPECT_TRUE(
      starts_with(handler->handle("TXN_GET|txn=TXN-NONE"), "ERR|code=TRANSACTION_NOT_FOUND|"));
}

TEST_F(MessageHandlerTest, BusinessRejectionsComeBackAsOkWithReason) {
  const auto response = handler->handle("TRANSFER|key=NSF|from=CUST-B|to=CUST-A|amount=1|ccy=GBP");
  EXPECT_TRUE(starts_with(response, "OK|txn_id=")) << response;
  EXPECT_NE(response.find("|status=REJECTED|"), std::string::npos);
  EXPECT_NE(response.find("|reason_code=INSUFFICIENT_FUNDS|"), std::string::npos);
}

TEST_F(MessageHandlerTest, AccountCreateAndReverse) {
  const auto created =
      handler->handle("ACCOUNT_CREATE|id=CUST-C|name=Customer C|ccy=GBP|type=CUSTOMER");
  EXPECT_TRUE(starts_with(created, "OK|account_id=CUST-C|balance=0.00|ccy=GBP|status=ACTIVE"))
      << created;
  EXPECT_TRUE(starts_with(handler->handle("ACCOUNT_CREATE|id=CUST-C|name=Again|ccy=GBP"),
                          "ERR|code=ACCOUNT_EXISTS|"));
  const auto credit = handler->handle("CREDIT|key=R1|account=CUST-C|amount=9|ccy=GBP");
  const auto id = credit.substr(10, credit.find('|', 10) - 10);
  const auto reversal = handler->handle("REVERSE|key=R2|txn=" + id + "|ref=oops");
  EXPECT_NE(reversal.find("|type=REVERSAL|status=POSTED|"), std::string::npos) << reversal;
  EXPECT_TRUE(
      starts_with(handler->handle("REVERSE|key=R3|txn=" + id), "ERR|code=INVALID_REVERSAL|"));
  EXPECT_EQ(balance("CUST-C"), 0);
}

TEST_F(MessageHandlerTest, TimeoutWhenProcessorCannotAnswerInTime) {
  TransactionProcessor stalled{*service,
                               ProcessorOptions{.worker_count = 0,
                                                .queue_capacity = 4,
                                                .enqueue_timeout = std::chrono::milliseconds{10}}};
  stalled.start();
  MessageHandler slow{*service, stalled, std::chrono::milliseconds{50}, 4096};
  const auto response = slow.handle("CREDIT|key=T1|account=CUST-A|amount=1|ccy=GBP");
  EXPECT_TRUE(starts_with(response, "ERR|code=TIMEOUT|")) << response;
  stalled.shutdown_now();
}

}  // namespace
}  // namespace corebank::testing
