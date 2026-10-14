// Translates protocol messages into service calls and back into responses.
#pragma once

#include "corebank/application/transaction_processor.hpp"
#include "corebank/application/transaction_service.hpp"
#include "corebank/messaging/protocol.hpp"

#include <chrono>
#include <cstddef>
#include <future>
#include <string>
#include <string_view>

namespace corebank {

class MessageHandler {
 public:
  MessageHandler(TransactionService& service, TransactionProcessor& processor,
                 std::chrono::milliseconds request_timeout, std::size_t max_line_bytes);

  // Returns the response line (without newline). Never throws.
  [[nodiscard]] std::string handle(std::string_view line);

 private:
  [[nodiscard]] Result<std::string> dispatch(const protocol::Message& message);
  [[nodiscard]] Result<std::string> handle_ping() const;
  [[nodiscard]] Result<std::string> handle_account_create(const protocol::Message& message);
  [[nodiscard]] Result<std::string> handle_balance(const protocol::Message& message);
  [[nodiscard]] Result<std::string> handle_transaction_get(const protocol::Message& message);
  [[nodiscard]] Result<std::string> handle_financial(const protocol::Message& message);
  [[nodiscard]] Result<std::string> await(std::future<Result<TransactionReceipt>> future);

  TransactionService& service_;
  TransactionProcessor& processor_;
  std::chrono::milliseconds request_timeout_;
  std::size_t max_line_bytes_;
};

[[nodiscard]] std::string receipt_response(const TransactionReceipt& receipt);

}  // namespace corebank
