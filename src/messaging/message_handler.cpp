#include "corebank/messaging/message_handler.hpp"

#include "corebank/infrastructure/logger.hpp"

#include <chrono>
#include <cstddef>
#include <future>
#include <string>
#include <string_view>
#include <vector>

namespace corebank {
namespace {

using Fields = std::vector<std::pair<std::string, std::string>>;

Result<Money> parse_money(const protocol::Message& message) {
  auto amount_text = message.require("amount");
  if (!amount_text) {
    return amount_text.error();
  }
  auto currency_text = message.require("ccy");
  if (!currency_text) {
    return currency_text.error();
  }
  auto currency = CurrencyCode::parse(currency_text.value());
  if (!currency) {
    return currency.error();
  }
  return Money::parse(amount_text.value(), currency.value());
}

Result<JournalReference> parse_reference(const protocol::Message& message) {
  return JournalReference::parse(message.get("ref").value_or(""));
}

template <typename Id>
Result<Id> parse_id(const protocol::Message& message, std::string_view key) {
  auto text = message.require(key);
  if (!text) {
    return text.error();
  }
  return Id::parse(text.value());
}

}  // namespace

std::string receipt_response(const TransactionReceipt& receipt) {
  const auto& txn = receipt.transaction;
  Fields fields{{"txn_id", txn.id.str()},
                {"type", std::string{to_string(txn.type)}},
                {"status", std::string{to_string(txn.status)}},
                {"amount", txn.amount.format_amount()},
                {"ccy", txn.amount.currency().str()},
                {"replayed", receipt.replayed ? "1" : "0"}};
  if (txn.rejection) {
    fields.emplace_back("reason_code", std::string{to_string(txn.rejection->code)});
    fields.emplace_back("reason", txn.rejection->message);
  }
  return protocol::ok_response(fields);
}

MessageHandler::MessageHandler(TransactionService& service, TransactionProcessor& processor,
                               std::chrono::milliseconds request_timeout,
                               std::size_t max_line_bytes)
    : service_(service),
      processor_(processor),
      request_timeout_(request_timeout),
      max_line_bytes_(max_line_bytes) {}

std::string MessageHandler::handle(std::string_view line) {
  auto parsed = protocol::parse(line, max_line_bytes_);
  if (!parsed) {
    Logger::instance().warn("message.rejected", {{"error", to_string(parsed.error().code)}});
    return protocol::error_response(parsed.error());
  }
  auto response = dispatch(parsed.value());
  if (!response) {
    return protocol::error_response(response.error());
  }
  return response.value();
}

Result<std::string> MessageHandler::dispatch(const protocol::Message& message) {
  const auto& type = message.type;
  if (type == "PING") {
    return handle_ping();
  }
  if (type == "ACCOUNT_CREATE") {
    return handle_account_create(message);
  }
  if (type == "BALANCE") {
    return handle_balance(message);
  }
  if (type == "TXN_GET") {
    return handle_transaction_get(message);
  }
  if (type == "CREDIT" || type == "DEBIT" || type == "TRANSFER" || type == "REVERSE") {
    return handle_financial(message);
  }
  return make_error(ErrorCode::UnknownMessageType, "unknown message type '" + type + "'");
}

Result<std::string> MessageHandler::handle_ping() const {
  const auto stats = processor_.stats();
  return protocol::ok_response({{"pong", "1"},
                                {"version", COREBANK_VERSION},
                                {"protocol", std::string{protocol::kVersion}},
                                {"accepting", stats.accepting ? "1" : "0"},
                                {"queued", std::to_string(stats.queued)}});
}

Result<std::string> MessageHandler::handle_account_create(const protocol::Message& message) {
  auto id = parse_id<AccountId>(message, "id");
  if (!id) {
    return id.error();
  }
  auto name = message.require("name");
  if (!name) {
    return name.error();
  }
  auto currency_text = message.require("ccy");
  if (!currency_text) {
    return currency_text.error();
  }
  auto currency = CurrencyCode::parse(currency_text.value());
  if (!currency) {
    return currency.error();
  }
  auto type = parse_account_type(message.get("type").value_or("CUSTOMER"));
  if (!type) {
    return type.error();
  }
  const bool overdraft = message.get("overdraft").value_or("0") == "1";
  auto account = service_.create_account(CreateAccountRequest{.id = id.value(),
                                                              .name = std::string{name.value()},
                                                              .type = type.value(),
                                                              .currency = currency.value(),
                                                              .allow_overdraft = overdraft});
  if (!account) {
    return account.error();
  }
  return protocol::ok_response({{"account_id", account.value().id.str()},
                                {"balance", account.value().balance.format_amount()},
                                {"ccy", account.value().currency().str()},
                                {"status", std::string{to_string(account.value().status)}}});
}

Result<std::string> MessageHandler::handle_balance(const protocol::Message& message) {
  auto id = parse_id<AccountId>(message, "account");
  if (!id) {
    return id.error();
  }
  auto account = service_.get_account(id.value());
  if (!account) {
    return account.error();
  }
  return protocol::ok_response({{"account_id", account.value().id.str()},
                                {"balance", account.value().balance.format_amount()},
                                {"ccy", account.value().currency().str()},
                                {"status", std::string{to_string(account.value().status)}},
                                {"version", std::to_string(account.value().version)}});
}

Result<std::string> MessageHandler::handle_transaction_get(const protocol::Message& message) {
  auto id = parse_id<TransactionId>(message, "txn");
  if (!id) {
    return id.error();
  }
  auto txn = service_.get_transaction(id.value());
  if (!txn) {
    return txn.error();
  }
  const auto& t = txn.value();
  Fields fields{{"txn_id", t.id.str()},
                {"type", std::string{to_string(t.type)}},
                {"status", std::string{to_string(t.status)}},
                {"amount", t.amount.format_amount()},
                {"ccy", t.amount.currency().str()},
                {"from", t.source ? t.source->str() : ""},
                {"to", t.destination ? t.destination->str() : ""},
                {"ref", t.reference.str()},
                {"business_date", t.business_date.str()}};
  if (t.reversal_of) {
    fields.emplace_back("reversal_of", t.reversal_of->str());
  }
  if (t.reversed_by) {
    fields.emplace_back("reversed_by", t.reversed_by->str());
  }
  if (t.rejection) {
    fields.emplace_back("reason_code", std::string{to_string(t.rejection->code)});
  }
  return protocol::ok_response(fields);
}

Result<std::string> MessageHandler::handle_financial(const protocol::Message& message) {
  auto key = parse_id<IdempotencyKey>(message, "key");
  if (!key) {
    return key.error();
  }
  auto reference = parse_reference(message);
  if (!reference) {
    return reference.error();
  }
  const auto& type = message.type;
  if (type == "REVERSE") {
    auto original = parse_id<TransactionId>(message, "txn");
    if (!original) {
      return original.error();
    }
    return await(processor_.submit(ReverseRequest{
        .key = key.value(), .original = original.value(), .reference = reference.value()}));
  }
  auto amount = parse_money(message);
  if (!amount) {
    return amount.error();
  }
  if (type == "CREDIT" || type == "DEBIT") {
    auto account = parse_id<AccountId>(message, "account");
    if (!account) {
      return account.error();
    }
    if (type == "CREDIT") {
      return await(processor_.submit(CreditRequest{.key = key.value(),
                                                   .account = account.value(),
                                                   .amount = amount.value(),
                                                   .reference = reference.value()}));
    }
    return await(processor_.submit(DebitRequest{.key = key.value(),
                                                .account = account.value(),
                                                .amount = amount.value(),
                                                .reference = reference.value()}));
  }
  auto from = parse_id<AccountId>(message, "from");
  if (!from) {
    return from.error();
  }
  auto to = parse_id<AccountId>(message, "to");
  if (!to) {
    return to.error();
  }
  return await(processor_.submit(TransferRequest{.key = key.value(),
                                                 .source = from.value(),
                                                 .destination = to.value(),
                                                 .amount = amount.value(),
                                                 .reference = reference.value()}));
}

Result<std::string> MessageHandler::await(std::future<Result<TransactionReceipt>> future) {
  if (future.wait_for(request_timeout_) != std::future_status::ready) {
    // The job still completes server-side; the client must retry with the same key.
    return make_error(ErrorCode::Timeout, "request did not complete within " +
                                              std::to_string(request_timeout_.count()) +
                                              " ms; retry with the same key");
  }
  auto outcome = future.get();
  if (!outcome) {
    return outcome.error();
  }
  return receipt_response(outcome.value());
}

}  // namespace corebank
