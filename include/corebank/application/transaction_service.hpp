// Application service: the single entry point for every financial operation.
#pragma once

#include "corebank/application/account_lock_manager.hpp"
#include "corebank/application/id_generator.hpp"
#include "corebank/application/idempotency_gate.hpp"
#include "corebank/application/repository.hpp"
#include "corebank/application/requests.hpp"
#include "corebank/domain/account.hpp"
#include "corebank/domain/audit.hpp"
#include "corebank/domain/posting.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"
#include "corebank/domain/transaction.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace corebank {

struct ServiceLimits {
  // Largest single transaction, in minor units, regardless of currency.
  std::int64_t max_transaction_minor{100'000'000'000};  // 1,000,000,000.00 at 2dp
};

class TransactionService {
 public:
  TransactionService(Repository& repository, const Clock& clock, IdGenerator& ids,
                     ServiceLimits limits = {});

  [[nodiscard]] Result<Account> create_account(const CreateAccountRequest& request);
  [[nodiscard]] Result<Account> get_account(const AccountId& id);
  [[nodiscard]] Result<std::vector<Account>> list_accounts();

  [[nodiscard]] Result<TransactionReceipt> credit(const CreditRequest& request);
  [[nodiscard]] Result<TransactionReceipt> debit(const DebitRequest& request);
  [[nodiscard]] Result<TransactionReceipt> transfer(const TransferRequest& request);
  [[nodiscard]] Result<TransactionReceipt> reverse(const ReverseRequest& request);

  [[nodiscard]] Result<Transaction> get_transaction(const TransactionId& id);
  [[nodiscard]] Result<std::vector<Posting>> transaction_postings(const TransactionId& id);
  [[nodiscard]] Result<std::vector<Posting>> account_history(const AccountId& id,
                                                             std::size_t limit = 100);
  [[nodiscard]] Result<std::vector<Transaction>> account_transactions(const AccountId& id,
                                                                      std::size_t limit = 100);
  [[nodiscard]] Result<std::vector<AuditEvent>> audit_trail(const AuditQuery& query);

  // Overrides the business date derived from the clock (demo/test convenience).
  void set_business_date_override(std::optional<BusinessDate> date);
  [[nodiscard]] BusinessDate current_business_date() const;

  [[nodiscard]] AccountLockManager& locks() noexcept { return locks_; }
  [[nodiscard]] IdempotencyGate& gate() noexcept { return gate_; }

 private:
  struct Leg {
    AccountId account;
    PostingDirection direction{PostingDirection::Credit};
  };

  struct Plan {
    TransactionType type{TransactionType::Transfer};
    Money amount;
    std::optional<AccountId> source;
    std::optional<AccountId> destination;
    JournalReference reference;
    std::optional<TransactionId> reversal_of;
  };

  [[nodiscard]] Result<TransactionReceipt> execute(const IdempotencyKey& key,
                                                   const std::string& signature, const Plan& plan);
  [[nodiscard]] Result<std::optional<TransactionReceipt>> replay_if_stored(
      const IdempotencyKey& key, const std::string& signature);
  [[nodiscard]] Result<TransactionReceipt> post(UnitOfWork& uow, Transaction& txn, const Plan& plan,
                                                const IdempotencyKey& key,
                                                const std::string& signature);
  [[nodiscard]] Result<void> apply_legs(UnitOfWork& uow, Transaction& txn,
                                        const std::vector<Leg>& legs);
  [[nodiscard]] Result<void> validate_amount(const Money& amount) const;
  [[nodiscard]] Result<void> record_rejection(UnitOfWork& uow, Transaction& txn,
                                              const Error& reason, const IdempotencyKey& key,
                                              const std::string& signature);
  [[nodiscard]] Result<void> audit(UnitOfWork& uow, AuditEventType type,
                                   const std::string& entity_type, const std::string& entity_id,
                                   std::string detail);

  Repository& repository_;
  const Clock& clock_;
  IdGenerator& ids_;
  ServiceLimits limits_;
  AccountLockManager locks_;
  IdempotencyGate gate_;
  std::optional<BusinessDate> business_date_override_;
};

}  // namespace corebank
