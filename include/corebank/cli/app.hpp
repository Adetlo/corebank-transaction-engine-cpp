// CLI application wiring: configuration, repository, services, command dispatch.
#pragma once

#include "corebank/application/id_generator.hpp"
#include "corebank/application/transaction_service.hpp"
#include "corebank/batch/eod_service.hpp"
#include "corebank/cli/args.hpp"
#include "corebank/cli/output.hpp"
#include "corebank/domain/time.hpp"
#include "corebank/infrastructure/config.hpp"
#include "corebank/infrastructure/sqlite/sqlite_repository.hpp"

#include <memory>
#include <string>

namespace corebank::cli {

struct Context {
  Config config;
  Printer out;
  SystemClock clock;
  IdGenerator ids;
  std::unique_ptr<sqlite::SqliteRepository> repository;
  std::unique_ptr<TransactionService> service;
  std::unique_ptr<EodService> eod;

  // Opens (and migrates) the database lazily; commands that need no database skip it.
  [[nodiscard]] Result<void> open_database();
};

[[nodiscard]] int run(int argc, const char* const* argv);
[[nodiscard]] std::string usage_text();

// Command handlers (one translation unit each).
[[nodiscard]] int cmd_account(Context& ctx, const Args& args);
[[nodiscard]] int cmd_transaction(Context& ctx, const Args& args);
[[nodiscard]] int cmd_transfer(Context& ctx, const Args& args);
[[nodiscard]] int cmd_reconcile(Context& ctx, const Args& args);
[[nodiscard]] int cmd_eod(Context& ctx, const Args& args);
[[nodiscard]] int cmd_audit(Context& ctx, const Args& args);
[[nodiscard]] int cmd_health(Context& ctx, const Args& args);
[[nodiscard]] int cmd_config(const Context& ctx, const Args& args);
[[nodiscard]] int cmd_serve(Context& ctx, const Args& args);
[[nodiscard]] int cmd_send(const Context& ctx, const Args& args);

// Shared helpers for command files.
[[nodiscard]] int report(const Context& ctx, const Error& error);
[[nodiscard]] int report_receipt(const Context& ctx, const TransactionReceipt& receipt);
[[nodiscard]] Result<Money> parse_amount(const Args& args);
[[nodiscard]] Result<IdempotencyKey> idempotency_key(Context& ctx, const Args& args);

}  // namespace corebank::cli
