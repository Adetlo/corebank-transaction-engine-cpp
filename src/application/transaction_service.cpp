#include "corebank/application/transaction_service.hpp"

#include "corebank/infrastructure/json_writer.hpp"
#include "corebank/infrastructure/logger.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corebank {
namespace {

std::string signature_of(std::string_view type, std::string_view a, std::string_view b,
                         const Money& amount, std::string_view reference) {
  std::string out;
  out.reserve(64 + a.size() + b.size() + reference.size());
  out += type;
  out += '|';
  out += a;
  out += '|';
  out += b;
  out += '|';
  out += amount.to_string();
  out += '|';
  out += reference;
  return out;
}

class ScopedTimer {
 public:
  ScopedTimer() : start_(std::chrono::steady_clock::now()) {}
  [[nodiscard]] double elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
        .count();
  }

 private:
  std::chrono::steady_clock::time_point start_;
};

}  // namespace

std::string request_signature(const CreditRequest& request) {
  return signature_of("CREDIT", "", request.account.str(), request.amount, request.reference.str());
}

std::string request_signature(const DebitRequest& request) {
  return signature_of("DEBIT", request.account.str(), "", request.amount, request.reference.str());
}

std::string request_signature(const TransferRequest& request) {
  return signature_of("TRANSFER", request.source.str(), request.destination.str(), request.amount,
                      request.reference.str());
}

std::string request_signature(const ReverseRequest& request) {
  return "REVERSAL|" + request.original.str() + "||" + request.reference.str();
}

TransactionService::TransactionService(Repository& repository, const Clock& clock, IdGenerator& ids,
                                       ServiceLimits limits)
    : repository_(repository), clock_(clock), ids_(ids), limits_(limits) {}

void TransactionService::set_business_date_override(std::optional<BusinessDate> date) {
  business_date_override_ = std::move(date);
}

BusinessDate TransactionService::current_business_date() const {
  if (business_date_override_) {
    return *business_date_override_;
  }
  return BusinessDate::from_timestamp(clock_.now());
}

Result<void> TransactionService::audit(UnitOfWork& uow, AuditEventType type,
                                       const std::string& entity_type, const std::string& entity_id,
                                       std::string detail) {
  AuditEvent event{};
  event.occurred_at = clock_.now();
  event.type = type;
  event.entity_type = entity_type;
  event.entity_id = entity_id;
  event.detail = std::move(detail);
  return uow.append_audit(event);
}

Result<void> TransactionService::validate_amount(const Money& amount) const {
  if (!amount.is_positive()) {
    return make_error(ErrorCode::InvalidAmount, "amount must be greater than zero");
  }
  if (amount.minor() > limits_.max_transaction_minor) {
    return make_error(ErrorCode::InvalidAmount,
                      "amount exceeds the configured transaction limit of " +
                          Money{limits_.max_transaction_minor, amount.currency()}.to_string());
  }
  return ok();
}

// ---------------------------------------------------------------------------
// Accounts
// ---------------------------------------------------------------------------

Result<Account> TransactionService::create_account(const CreateAccountRequest& request) {
  const ScopedTimer timer;
  if (auto name_check = validate_account_name(request.name); !name_check) {
    return name_check.error();
  }
  auto uow_result = repository_.begin(TransactionMode::ReadWrite);
  if (!uow_result) {
    return uow_result.error();
  }
  auto& uow = *uow_result.value();

  auto existing = uow.find_account(request.id);
  if (!existing) {
    return existing.error();
  }
  if (existing.value().has_value()) {
    return make_error(ErrorCode::AccountExists, "account " + request.id.str() + " already exists");
  }

  const auto now = clock_.now();
  Account account{
      .id = request.id,
      .name = request.name,
      .type = request.type,
      .balance = Money::zero(request.currency),
      .allow_overdraft = request.allow_overdraft,
      .status = AccountStatus::Active,
      .version = 0,
      .created_at = now,
      .updated_at = now,
  };
  if (auto inserted = uow.insert_account(account); !inserted) {
    return inserted.error();
  }
  JsonWriter detail;
  detail.string("name", account.name)
      .string("type", to_string(account.type))
      .string("currency", account.currency().code())
      .boolean("allow_overdraft", account.allow_overdraft);
  if (auto audited =
          audit(uow, AuditEventType::AccountCreated, "account", account.id.str(), detail.str());
      !audited) {
    return audited.error();
  }
  if (auto committed = uow.commit(); !committed) {
    return committed.error();
  }
  Logger::instance().info("account.created", {{"account_id", account.id.str()},
                                              {"currency", std::string{account.currency().code()}},
                                              {"duration_ms", timer.elapsed_ms()}});
  return account;
}

Result<Account> TransactionService::get_account(const AccountId& id) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  auto found = uow_result.value()->find_account(id);
  if (!found) {
    return found.error();
  }
  if (!found.value()) {
    return make_error(ErrorCode::AccountNotFound, "account " + id.str() + " not found");
  }
  return *found.value();
}

Result<std::vector<Account>> TransactionService::list_accounts() {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  return uow_result.value()->list_accounts();
}

// ---------------------------------------------------------------------------
// Financial operations
// ---------------------------------------------------------------------------

Result<TransactionReceipt> TransactionService::credit(const CreditRequest& request) {
  if (auto check = validate_amount(request.amount); !check) {
    return check.error();
  }
  const Plan plan{.type = TransactionType::Credit,
                  .amount = request.amount,
                  .source = std::nullopt,
                  .destination = request.account,
                  .reference = request.reference,
                  .reversal_of = std::nullopt};
  return execute(request.key, request_signature(request), plan);
}

Result<TransactionReceipt> TransactionService::debit(const DebitRequest& request) {
  if (auto check = validate_amount(request.amount); !check) {
    return check.error();
  }
  const Plan plan{.type = TransactionType::Debit,
                  .amount = request.amount,
                  .source = request.account,
                  .destination = std::nullopt,
                  .reference = request.reference,
                  .reversal_of = std::nullopt};
  return execute(request.key, request_signature(request), plan);
}

Result<TransactionReceipt> TransactionService::transfer(const TransferRequest& request) {
  if (auto check = validate_amount(request.amount); !check) {
    return check.error();
  }
  if (request.source == request.destination) {
    return make_error(ErrorCode::SameAccountTransfer,
                      "source and destination accounts must differ");
  }
  const Plan plan{.type = TransactionType::Transfer,
                  .amount = request.amount,
                  .source = request.source,
                  .destination = request.destination,
                  .reference = request.reference,
                  .reversal_of = std::nullopt};
  return execute(request.key, request_signature(request), plan);
}

Result<TransactionReceipt> TransactionService::reverse(const ReverseRequest& request) {
  const std::string signature = request_signature(request);
  const auto token = gate_.acquire(request.key);

  auto replay = replay_if_stored(request.key, signature);
  if (!replay) {
    return replay.error();
  }
  if (replay.value()) {
    return *replay.value();
  }

  // Look up the original outside the write transaction to build the plan.
  auto original = get_transaction(request.original);
  if (!original) {
    if (original.error().code == ErrorCode::TransactionNotFound) {
      return make_error(ErrorCode::InvalidReversal,
                        "original transaction " + request.original.str() + " not found");
    }
    return original.error();
  }
  const auto& orig = original.value();
  if (orig.type == TransactionType::Reversal) {
    return make_error(ErrorCode::InvalidReversal, "a reversal cannot itself be reversed");
  }
  if (orig.status != TransactionStatus::Posted) {
    return make_error(ErrorCode::InvalidReversal, "transaction " + orig.id.str() + " is " +
                                                      std::string{to_string(orig.status)} +
                                                      ", only POSTED can be reversed");
  }
  const Plan plan{.type = TransactionType::Reversal,
                  .amount = orig.amount,
                  .source = orig.destination,  // swap legs
                  .destination = orig.source,
                  .reference = request.reference,
                  .reversal_of = orig.id};
  return execute(request.key, signature, plan);
}

Result<std::optional<TransactionReceipt>> TransactionService::replay_if_stored(
    const IdempotencyKey& key, const std::string& signature) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  auto& uow = *uow_result.value();
  auto record = uow.find_idempotency(key);
  if (!record) {
    return record.error();
  }
  if (!record.value()) {
    return std::optional<TransactionReceipt>{};
  }
  if (record.value()->request_signature != signature) {
    return make_error(ErrorCode::IdempotencyConflict,
                      "idempotency key " + key.str() + " was already used for a different request");
  }
  auto txn = uow.find_transaction(record.value()->transaction_id);
  if (!txn) {
    return txn.error();
  }
  if (!txn.value()) {
    return make_error(ErrorCode::PersistenceError,
                      "idempotency record points to a missing transaction");
  }
  Logger::instance().info("transaction.replayed", {{"transaction_id", txn.value()->id.str()},
                                                   {"idempotency_key", key.str()}});
  return std::optional<TransactionReceipt>{
      TransactionReceipt{.transaction = *txn.value(), .replayed = true}};
}

Result<TransactionReceipt> TransactionService::execute(const IdempotencyKey& key,
                                                       const std::string& signature,
                                                       const Plan& plan) {
  const ScopedTimer timer;
  // Lock order (never varied): idempotency gate -> account stripes -> database write txn.
  std::optional<IdempotencyGate::Token> token;
  if (plan.type != TransactionType::Reversal) {
    token.emplace(gate_.acquire(key));
    auto replay = replay_if_stored(key, signature);
    if (!replay) {
      return replay.error();
    }
    if (replay.value()) {
      return *replay.value();
    }
  }

  std::vector<AccountId> involved;
  if (plan.source) {
    involved.push_back(*plan.source);
  }
  if (plan.destination) {
    involved.push_back(*plan.destination);
  }
  const auto guard = locks_.lock_exclusive(involved);

  auto uow_result = repository_.begin(TransactionMode::ReadWrite);
  if (!uow_result) {
    return uow_result.error();
  }
  auto& uow = *uow_result.value();

  const auto now = clock_.now();
  Transaction txn{
      .id = ids_.next_transaction_id(),
      .type = plan.type,
      .status = TransactionStatus::Received,
      .amount = plan.amount,
      .source = plan.source,
      .destination = plan.destination,
      .reference = plan.reference,
      .idempotency_key = key,
      .reversal_of = plan.reversal_of,
      .reversed_by = std::nullopt,
      .rejection = std::nullopt,
      .business_date = current_business_date(),
      .created_at = now,
      .updated_at = now,
  };
  auto outcome = post(uow, txn, plan, key, signature);
  if (!outcome) {
    // A concurrent writer (other process) may have stored the same key first.
    if (outcome.error().code == ErrorCode::DuplicateRequest) {
      if (auto rolled = uow.rollback(); !rolled) {
        return rolled.error();
      }
      auto replay = replay_if_stored(key, signature);
      if (!replay) {
        return replay.error();
      }
      if (replay.value()) {
        return *replay.value();
      }
    }
    Logger::instance().error("transaction.failed", {{"transaction_id", txn.id.str()},
                                                    {"error", to_string(outcome.error().code)},
                                                    {"duration_ms", timer.elapsed_ms()}});
    return outcome.error();
  }
  const auto& receipt = outcome.value();
  Logger::instance().info(
      receipt.posted() ? "transaction.posted" : "transaction.rejected",
      {{"transaction_id", receipt.transaction.id.str()},
       {"type", to_string(receipt.transaction.type)},
       {"status", to_string(receipt.transaction.status)},
       {"account_id", plan.source ? plan.source->str() : plan.destination->str()},
       {"amount", receipt.transaction.amount.to_string()},
       {"duration_ms", timer.elapsed_ms()}});
  return outcome;
}

Result<void> TransactionService::record_rejection(UnitOfWork& uow, Transaction& txn,
                                                  const Error& reason, const IdempotencyKey& key,
                                                  const std::string& signature) {
  if (auto rejected = txn.reject(reason, clock_.now()); !rejected) {
    return rejected.error();
  }
  if (auto updated = uow.update_transaction(txn); !updated) {
    return updated.error();
  }
  JsonWriter detail;
  detail.string("code", to_string(reason.code)).string("reason", reason.message);
  if (auto audited = audit(uow, AuditEventType::TransactionRejected, "transaction", txn.id.str(),
                           detail.str());
      !audited) {
    return audited.error();
  }
  const IdempotencyRecord record{.key = key,
                                 .request_signature = signature,
                                 .transaction_id = txn.id,
                                 .created_at = clock_.now()};
  if (auto stored = uow.insert_idempotency(record); !stored) {
    return stored.error();
  }
  return uow.commit();
}

Result<void> TransactionService::apply_legs(UnitOfWork& uow, Transaction& txn,
                                            const std::vector<Leg>& legs) {
  for (const auto& leg : legs) {
    auto found = uow.find_account(leg.account);
    if (!found) {
      return found.error();
    }
    if (!found.value()) {
      return make_error(ErrorCode::AccountNotFound, "account " + leg.account.str() + " not found");
    }
    const Account& account = *found.value();
    auto next_balance = leg.direction == PostingDirection::Debit
                            ? account.balance_after_debit(txn.amount)
                            : account.balance_after_credit(txn.amount);
    if (!next_balance) {
      return next_balance.error();
    }
    const Posting posting{
        .id = 0,
        .transaction_id = txn.id,
        .account_id = account.id,
        .direction = leg.direction,
        .amount = txn.amount,
        .balance_after_minor = next_balance.value().minor(),
        .created_at = clock_.now(),
    };
    if (auto inserted = uow.insert_posting(posting); !inserted) {
      return inserted.error();
    }
    if (auto updated = uow.update_account_balance(account.id, next_balance.value().minor(),
                                                  account.version, clock_.now());
        !updated) {
      return updated.error();
    }
  }
  return ok();
}

Result<TransactionReceipt> TransactionService::post(UnitOfWork& uow, Transaction& txn,
                                                    const Plan& plan, const IdempotencyKey& key,
                                                    const std::string& signature) {
  if (auto inserted = uow.insert_transaction(txn); !inserted) {
    return inserted.error();
  }
  {
    JsonWriter detail;
    detail.string("type", to_string(txn.type))
        .string("amount", txn.amount.to_string())
        .string("reference", txn.reference.str());
    if (txn.source) {
      detail.string("source", txn.source->str());
    }
    if (txn.destination) {
      detail.string("destination", txn.destination->str());
    }
    if (txn.reversal_of) {
      detail.string("reversal_of", txn.reversal_of->str());
    }
    if (auto audited = audit(uow, AuditEventType::TransactionReceived, "transaction", txn.id.str(),
                             detail.str());
        !audited) {
      return audited.error();
    }
  }

  // RECEIVED -> VALIDATED: structural checks that do not need balances.
  std::vector<Leg> legs;
  if (plan.source) {
    legs.push_back(Leg{*plan.source, PostingDirection::Debit});
  }
  if (plan.destination) {
    legs.push_back(Leg{*plan.destination, PostingDirection::Credit});
  }
  for (const auto& leg : legs) {
    auto found = uow.find_account(leg.account);
    if (!found) {
      return found.error();
    }
    if (!found.value()) {
      auto reason =
          make_error(ErrorCode::AccountNotFound, "account " + leg.account.str() + " not found");
      if (auto recorded = record_rejection(uow, txn, reason, key, signature); !recorded) {
        return recorded.error();
      }
      return TransactionReceipt{.transaction = txn, .replayed = false};
    }
    const Account& account = *found.value();
    if (!account.is_active() || account.currency() != txn.amount.currency()) {
      auto reason =
          !account.is_active()
              ? make_error(ErrorCode::AccountInactive, "account " + account.id.str() + " is " +
                                                           std::string{to_string(account.status)})
              : make_error(ErrorCode::CurrencyMismatch,
                           "account " + account.id.str() + " is denominated in " +
                               account.currency().str() + ", not " + txn.amount.currency().str());
      if (auto recorded = record_rejection(uow, txn, reason, key, signature); !recorded) {
        return recorded.error();
      }
      return TransactionReceipt{.transaction = txn, .replayed = false};
    }
  }
  if (auto moved = txn.transition_to(TransactionStatus::Validated, clock_.now()); !moved) {
    return moved.error();
  }

  // Reversal-specific: the original must still be POSTED inside this transaction.
  std::optional<Transaction> original;
  if (plan.reversal_of) {
    auto found = uow.find_transaction(*plan.reversal_of);
    if (!found) {
      return found.error();
    }
    if (!found.value() || found.value()->status != TransactionStatus::Posted) {
      auto reason =
          make_error(ErrorCode::InvalidReversal, "transaction " + plan.reversal_of->str() +
                                                     " is not POSTED (already reversed?)");
      if (auto recorded = record_rejection(uow, txn, reason, key, signature); !recorded) {
        return recorded.error();
      }
      return TransactionReceipt{.transaction = txn, .replayed = false};
    }
    original = *found.value();
  }

  // VALIDATED -> PROCESSING -> POSTED: balance checks and postings.
  if (auto moved = txn.transition_to(TransactionStatus::Processing, clock_.now()); !moved) {
    return moved.error();
  }
  if (auto applied = apply_legs(uow, txn, legs); !applied) {
    const auto code = applied.error().code;
    const bool business_rejection =
        code == ErrorCode::InsufficientFunds || code == ErrorCode::AccountInactive ||
        code == ErrorCode::CurrencyMismatch || code == ErrorCode::Overflow ||
        code == ErrorCode::AccountNotFound;
    if (!business_rejection) {
      return applied.error();  // persistence failure: caller rolls back
    }
    if (auto recorded = record_rejection(uow, txn, applied.error(), key, signature); !recorded) {
      return recorded.error();
    }
    return TransactionReceipt{.transaction = txn, .replayed = false};
  }
  if (auto moved = txn.transition_to(TransactionStatus::Posted, clock_.now()); !moved) {
    return moved.error();
  }
  if (auto updated = uow.update_transaction(txn); !updated) {
    return updated.error();
  }
  {
    JsonWriter detail;
    detail.string("amount", txn.amount.to_string())
        .number("legs", static_cast<std::int64_t>(legs.size()));
    if (auto audited = audit(uow, AuditEventType::TransactionPosted, "transaction", txn.id.str(),
                             detail.str());
        !audited) {
      return audited.error();
    }
  }
  if (original) {
    if (auto moved = original->transition_to(TransactionStatus::Reversed, clock_.now()); !moved) {
      return moved.error();
    }
    original->reversed_by = txn.id;
    if (auto updated = uow.update_transaction(*original); !updated) {
      return updated.error();
    }
    JsonWriter detail;
    detail.string("reversed_by", txn.id.str()).string("reference", txn.reference.str());
    if (auto audited = audit(uow, AuditEventType::TransactionReversed, "transaction",
                             original->id.str(), detail.str());
        !audited) {
      return audited.error();
    }
  }
  const IdempotencyRecord record{.key = key,
                                 .request_signature = signature,
                                 .transaction_id = txn.id,
                                 .created_at = clock_.now()};
  if (auto stored = uow.insert_idempotency(record); !stored) {
    return stored.error();
  }
  if (auto committed = uow.commit(); !committed) {
    return committed.error();
  }
  return TransactionReceipt{.transaction = txn, .replayed = false};
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

Result<Transaction> TransactionService::get_transaction(const TransactionId& id) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  auto found = uow_result.value()->find_transaction(id);
  if (!found) {
    return found.error();
  }
  if (!found.value()) {
    return make_error(ErrorCode::TransactionNotFound, "transaction " + id.str() + " not found");
  }
  return *found.value();
}

Result<std::vector<Posting>> TransactionService::transaction_postings(const TransactionId& id) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  return uow_result.value()->postings_for_transaction(id);
}

Result<std::vector<Posting>> TransactionService::account_history(const AccountId& id,
                                                                 std::size_t limit) {
  const auto guard = locks_.lock_shared(id);
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  auto found = uow_result.value()->find_account(id);
  if (!found) {
    return found.error();
  }
  if (!found.value()) {
    return make_error(ErrorCode::AccountNotFound, "account " + id.str() + " not found");
  }
  return uow_result.value()->postings_for_account(id, limit);
}

Result<std::vector<Transaction>> TransactionService::account_transactions(const AccountId& id,
                                                                          std::size_t limit) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  return uow_result.value()->account_transactions(id, limit);
}

Result<std::vector<AuditEvent>> TransactionService::audit_trail(const AuditQuery& query) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  return uow_result.value()->list_audit(query);
}

}  // namespace corebank
