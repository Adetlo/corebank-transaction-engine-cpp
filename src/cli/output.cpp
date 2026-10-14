#include "corebank/cli/output.hpp"

#include "corebank/infrastructure/json_writer.hpp"

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace corebank::cli {
namespace {

std::string opt(const std::optional<AccountId>& id) {
  return id ? id->str() : "-";
}

void put_line(const std::string& text) {
  std::cout << text << '\n';
}

}  // namespace

int exit_code_for(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::InvalidAmount:
    case ErrorCode::InvalidCurrency:
    case ErrorCode::InvalidIdentifier:
    case ErrorCode::InvalidReference:
    case ErrorCode::ValidationError:
    case ErrorCode::MissingField:
    case ErrorCode::MalformedMessage:
    case ErrorCode::UnknownMessageType:
    case ErrorCode::PayloadTooLarge:
      return kUsage;
    case ErrorCode::CurrencyMismatch:
    case ErrorCode::AccountNotFound:
    case ErrorCode::AccountExists:
    case ErrorCode::AccountInactive:
    case ErrorCode::InsufficientFunds:
    case ErrorCode::SameAccountTransfer:
    case ErrorCode::TransactionNotFound:
    case ErrorCode::InvalidTransition:
    case ErrorCode::InvalidReversal:
    case ErrorCode::DuplicateRequest:
    case ErrorCode::IdempotencyConflict:
    case ErrorCode::Overflow:
      return kRejected;
    case ErrorCode::BatchNotFound:
    case ErrorCode::BatchStageFailed:
    case ErrorCode::BatchAlreadyRunning:
    case ErrorCode::ReconciliationFailed:
      return kControlFailed;
    case ErrorCode::PersistenceError:
    case ErrorCode::ConfigurationError:
    case ErrorCode::QueueFull:
    case ErrorCode::ShuttingDown:
    case ErrorCode::Timeout:
    case ErrorCode::TooManyConnections:
    case ErrorCode::NetworkError:
    case ErrorCode::Internal:
      return kSystemError;
  }
  return kSystemError;
}

std::string account_json(const Account& account) {
  JsonWriter w;
  w.string("account_id", account.id.str())
      .string("name", account.name)
      .string("type", to_string(account.type))
      .string("currency", account.currency().code())
      .string("balance", account.balance.format_amount())
      .number("balance_minor", account.balance.minor())
      .boolean("allow_overdraft", account.allow_overdraft)
      .string("status", to_string(account.status))
      .number("version", account.version)
      .string("created_at", format_timestamp(account.created_at))
      .string("updated_at", format_timestamp(account.updated_at));
  return w.str();
}

std::string transaction_json(const Transaction& txn) {
  JsonWriter w;
  w.string("transaction_id", txn.id.str())
      .string("type", to_string(txn.type))
      .string("status", to_string(txn.status))
      .string("currency", txn.amount.currency().code())
      .string("amount", txn.amount.format_amount())
      .number("amount_minor", txn.amount.minor())
      .string("reference", txn.reference.str())
      .string("business_date", txn.business_date.str())
      .string("created_at", format_timestamp(txn.created_at))
      .string("updated_at", format_timestamp(txn.updated_at));
  if (txn.source) {
    w.string("source_account_id", txn.source->str());
  } else {
    w.null("source_account_id");
  }
  if (txn.destination) {
    w.string("destination_account_id", txn.destination->str());
  } else {
    w.null("destination_account_id");
  }
  if (txn.idempotency_key) {
    w.string("idempotency_key", txn.idempotency_key->str());
  }
  if (txn.reversal_of) {
    w.string("reversal_of", txn.reversal_of->str());
  }
  if (txn.reversed_by) {
    w.string("reversed_by", txn.reversed_by->str());
  }
  if (txn.rejection) {
    JsonWriter r;
    r.string("code", to_string(txn.rejection->code)).string("message", txn.rejection->message);
    w.raw("rejection", r.str());
  }
  return w.str();
}

std::string posting_json(const Posting& posting) {
  JsonWriter w;
  w.number("posting_id", posting.id)
      .string("transaction_id", posting.transaction_id.str())
      .string("account_id", posting.account_id.str())
      .string("direction", to_string(posting.direction))
      .string("currency", posting.amount.currency().code())
      .string("amount", posting.amount.format_amount())
      .number("amount_minor", posting.amount.minor())
      .number("balance_after_minor", posting.balance_after_minor)
      .string("created_at", format_timestamp(posting.created_at));
  return w.str();
}

std::string audit_json(const AuditEvent& event) {
  JsonWriter w;
  w.number("event_id", event.id)
      .string("occurred_at", format_timestamp(event.occurred_at))
      .string("event_type", to_string(event.type))
      .string("entity_type", event.entity_type)
      .string("entity_id", event.entity_id)
      .raw("detail", event.detail.empty() ? "{}" : event.detail);
  if (event.batch_id) {
    w.string("batch_id", event.batch_id->str());
  }
  return w.str();
}

std::string batch_json(const BatchRun& run) {
  JsonArrayWriter stages;
  for (const auto& stage : run.stages) {
    JsonWriter s;
    s.string("stage", to_string(stage.stage))
        .string("status", to_string(stage.status))
        .number("attempts", stage.attempts)
        .string("detail", stage.detail);
    if (stage.started_at) {
      s.string("started_at", format_timestamp(*stage.started_at));
    }
    if (stage.completed_at) {
      s.string("completed_at", format_timestamp(*stage.completed_at));
    }
    stages.raw(s.str());
  }
  JsonWriter w;
  w.string("batch_id", run.id.str())
      .string("business_date", run.business_date.str())
      .string("status", to_string(run.status))
      .number("attempt", run.attempt)
      .string("started_at", format_timestamp(run.started_at))
      .string("updated_at", format_timestamp(run.updated_at));
  if (run.current_stage) {
    w.string("current_stage", to_string(*run.current_stage));
  }
  if (run.cutoff_at) {
    w.string("cutoff_at", format_timestamp(*run.cutoff_at));
  }
  if (run.completed_at) {
    w.string("completed_at", format_timestamp(*run.completed_at));
  }
  if (!run.failure_reason.empty()) {
    w.string("failure_reason", run.failure_reason);
  }
  w.raw("stages", stages.str());
  return w.str();
}

std::string reconciliation_json(const ReconciliationResult& result) {
  JsonArrayWriter checks;
  for (const auto& check : result.checks) {
    JsonWriter c;
    c.string("name", check.name).boolean("passed", check.passed).string("detail", check.detail);
    checks.raw(c.str());
  }
  JsonArrayWriter totals;
  for (const auto& total : result.totals) {
    JsonWriter t;
    t.string("currency", total.currency)
        .number("posted_count", total.posted_count)
        .number("rejected_count", total.rejected_count)
        .number("reversed_count", total.reversed_count)
        .number("credit_total_minor", total.credit_total_minor)
        .number("debit_total_minor", total.debit_total_minor)
        .number("transfer_total_minor", total.transfer_total_minor);
    totals.raw(t.str());
  }
  JsonWriter w;
  w.boolean("passed", result.passed)
      .string("scope", result.business_date)
      .string("run_at", format_timestamp(result.run_at))
      .raw("checks", checks.str())
      .raw("totals", totals.str());
  return w.str();
}

void Printer::error(const Error& err) const {
  if (json) {
    JsonWriter w;
    w.boolean("ok", false).string("error", to_string(err.code)).string("message", err.message);
    std::cout << w.str() << '\n';
  } else {
    std::cerr << "error: " << to_string(err.code) << ": " << err.message << '\n';
  }
}

void Printer::message(const std::string& text, const std::string& json_key) const {
  if (json) {
    JsonWriter w;
    w.boolean("ok", true).string(json_key, text);
    put_line(w.str());
  } else {
    put_line(text);
  }
}

void Printer::account(const Account& account) const {
  if (json) {
    put_line(account_json(account));
    return;
  }
  put_line("account_id:      " + account.id.str());
  put_line("name:            " + account.name);
  put_line("type:            " + std::string{to_string(account.type)});
  put_line("currency:        " + account.currency().str());
  put_line("balance:         " + account.balance.to_string());
  put_line("allow_overdraft: " + std::string{account.allow_overdraft ? "yes" : "no"});
  put_line("status:          " + std::string{to_string(account.status)});
  put_line("version:         " + std::to_string(account.version));
}

void Printer::accounts(const std::vector<Account>& list) const {
  if (json) {
    JsonArrayWriter arr;
    for (const auto& a : list) {
      arr.raw(account_json(a));
    }
    put_line(arr.str());
    return;
  }
  std::printf("%-28s %-12s %-4s %20s  %s\n", "ACCOUNT", "TYPE", "CCY", "BALANCE", "STATUS");
  for (const auto& a : list) {
    std::printf("%-28s %-12s %-4s %20s  %s\n", a.id.str().c_str(),
                std::string{to_string(a.type)}.c_str(), a.currency().str().c_str(),
                a.balance.format_amount().c_str(), std::string{to_string(a.status)}.c_str());
  }
}

void Printer::receipt(const TransactionReceipt& r) const {
  if (json) {
    JsonWriter w;
    w.boolean("ok", r.posted())
        .boolean("replayed", r.replayed)
        .raw("transaction", transaction_json(r.transaction));
    put_line(w.str());
    return;
  }
  const auto& t = r.transaction;
  put_line("transaction_id: " + t.id.str());
  put_line("type:           " + std::string{to_string(t.type)});
  put_line("status:         " + std::string{to_string(t.status)} +
           (r.replayed ? " (replayed)" : ""));
  put_line("amount:         " + t.amount.to_string());
  put_line("source:         " + opt(t.source));
  put_line("destination:    " + opt(t.destination));
  put_line("reference:      " + t.reference.str());
  if (t.idempotency_key) {
    put_line("idempotency:    " + t.idempotency_key->str());
  }
  if (t.reversal_of) {
    put_line("reversal_of:    " + t.reversal_of->str());
  }
  if (t.rejection) {
    put_line("rejection:      " + std::string{to_string(t.rejection->code)} + ": " +
             t.rejection->message);
  }
}

void Printer::transaction(const Transaction& t, const std::vector<Posting>& legs) const {
  if (json) {
    JsonArrayWriter arr;
    for (const auto& p : legs) {
      arr.raw(posting_json(p));
    }
    JsonWriter w;
    w.raw("transaction", transaction_json(t)).raw("postings", arr.str());
    put_line(w.str());
    return;
  }
  receipt(TransactionReceipt{.transaction = t, .replayed = false});
  put_line("business_date:  " + t.business_date.str());
  put_line("created_at:     " + format_timestamp(t.created_at));
  if (t.reversed_by) {
    put_line("reversed_by:    " + t.reversed_by->str());
  }
  if (!legs.empty()) {
    put_line("postings:");
    for (const auto& p : legs) {
      std::printf("  #%-6lld %-6s %-28s %16s  balance_after=%lld\n", static_cast<long long>(p.id),
                  std::string{to_string(p.direction)}.c_str(), p.account_id.str().c_str(),
                  p.amount.format_amount().c_str(), static_cast<long long>(p.balance_after_minor));
    }
  }
}

void Printer::postings(const std::vector<Posting>& list) const {
  if (json) {
    JsonArrayWriter arr;
    for (const auto& p : list) {
      arr.raw(posting_json(p));
    }
    put_line(arr.str());
    return;
  }
  std::printf("%-8s %-27s %-6s %16s %18s  %s\n", "POSTING", "CREATED_AT", "DIR", "AMOUNT",
              "BALANCE_AFTER", "TRANSACTION");
  for (const auto& p : list) {
    const Money after{p.balance_after_minor, p.amount.currency()};
    std::printf("%-8lld %-27s %-6s %16s %18s  %s\n", static_cast<long long>(p.id),
                format_timestamp(p.created_at).c_str(), std::string{to_string(p.direction)}.c_str(),
                p.amount.format_amount().c_str(), after.format_amount().c_str(),
                p.transaction_id.str().c_str());
  }
}

void Printer::audit(const std::vector<AuditEvent>& events) const {
  if (json) {
    JsonArrayWriter arr;
    for (const auto& e : events) {
      arr.raw(audit_json(e));
    }
    put_line(arr.str());
    return;
  }
  for (const auto& e : events) {
    std::printf("%-6lld %s %-26s %-12s %-40s %s\n", static_cast<long long>(e.id),
                format_timestamp(e.occurred_at).c_str(), std::string{to_string(e.type)}.c_str(),
                e.entity_type.c_str(), e.entity_id.c_str(), e.detail.c_str());
  }
}

void Printer::reconciliation(const ReconciliationResult& result) const {
  if (json) {
    put_line(reconciliation_json(result));
    return;
  }
  put_line("reconciliation " + result.summary() + " [scope: " + result.business_date + "]");
  for (const auto& check : result.checks) {
    std::printf("  [%s] %-30s %s\n", check.passed ? "PASS" : "FAIL", check.name.c_str(),
                check.detail.c_str());
  }
  if (!result.totals.empty()) {
    put_line("totals by currency:");
    for (const auto& t : result.totals) {
      std::printf(
          "  %s posted=%lld rejected=%lld reversed=%lld credit=%lld debit=%lld transfer=%lld\n",
          t.currency.c_str(), static_cast<long long>(t.posted_count),
          static_cast<long long>(t.rejected_count), static_cast<long long>(t.reversed_count),
          static_cast<long long>(t.credit_total_minor), static_cast<long long>(t.debit_total_minor),
          static_cast<long long>(t.transfer_total_minor));
    }
  }
}

void Printer::batch(const BatchRun& run) const {
  if (json) {
    put_line(batch_json(run));
    return;
  }
  put_line("batch_id:      " + run.id.str());
  put_line("business_date: " + run.business_date.str());
  put_line("status:        " + std::string{to_string(run.status)});
  put_line("attempt:       " + std::to_string(run.attempt));
  put_line("started_at:    " + format_timestamp(run.started_at));
  if (run.completed_at) {
    put_line("completed_at:  " + format_timestamp(*run.completed_at));
  }
  if (!run.failure_reason.empty()) {
    put_line("failure:       " + run.failure_reason);
  }
  put_line("stages:");
  for (const auto& stage : run.stages) {
    std::printf("  %-20s %-10s attempts=%d  %s\n", std::string{to_string(stage.stage)}.c_str(),
                std::string{to_string(stage.status)}.c_str(), stage.attempts, stage.detail.c_str());
  }
}

void Printer::batches(const std::vector<BatchRun>& runs) const {
  if (json) {
    JsonArrayWriter arr;
    for (const auto& r : runs) {
      arr.raw(batch_json(r));
    }
    put_line(arr.str());
    return;
  }
  std::printf("%-40s %-12s %-10s %-8s %s\n", "BATCH", "DATE", "STATUS", "ATTEMPT", "STARTED_AT");
  for (const auto& r : runs) {
    std::printf("%-40s %-12s %-10s %-8d %s\n", r.id.str().c_str(), r.business_date.str().c_str(),
                std::string{to_string(r.status)}.c_str(), r.attempt,
                format_timestamp(r.started_at).c_str());
  }
}

}  // namespace corebank::cli
