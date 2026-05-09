#include "corebank/cli/app.hpp"

#include "corebank/infrastructure/logger.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace corebank::cli {

std::string usage_text() {
  return R"(corebank - CoreBank Transaction Engine C++ (educational, synthetic data only)

Usage: corebank [global options] <command> [subcommand] [options]

Global options:
  --db PATH            SQLite database path (COREBANK_DB, default var/corebank.db)
  --config PATH        Flat YAML configuration file (COREBANK_CONFIG)
  --log-level LEVEL    DEBUG | INFO | WARN | ERROR
  --json               Machine-readable JSON output
  --business-date D    Override the business date (YYYY-MM-DD)

Commands:
  account create   --id ID --name NAME --currency CCY [--type TYPE] [--allow-overdraft]
  account show     --id ID
  account history  --id ID [--limit N]
  account list
  transaction credit  --account ID --amount A --currency CCY [--reference R] [--key K]
  transaction debit   --account ID --amount A --currency CCY [--reference R] [--key K]
  transaction show    --id TXN
  transaction reverse --id TXN [--reference R] [--key K]
  transfer         --from ID --to ID --amount A --currency CCY [--reference R] [--key K]
  reconcile        [--date YYYY-MM-DD]
  eod run          [--date YYYY-MM-DD]
  eod status       [--date YYYY-MM-DD]
  eod history      [--limit N]
  audit            [--entity ID] [--batch ID] [--type EVENT] [--limit N]
  health           [--host H --port P]   (TCP ping when host/port given, else database check)
  config show
  serve            [--address A] [--port P]
  send             --host H --port P MESSAGE
  version

Exit codes: 0 ok, 1 usage, 2 business rejection, 3 control failure, 4 system error.
)";
}

Result<void> Context::open_database() {
  if (repository) {
    return ok();
  }
  auto opened = sqlite::SqliteRepository::open(
      config.database_path, sqlite::SqliteRepository::Options{.pool_size = config.worker_count + 2,
                                                              .busy_timeout_ms = 5000});
  if (!opened) {
    return opened.error();
  }
  repository = std::move(opened.value());
  if (auto migrated = repository->migrate(); !migrated) {
    return migrated.error();
  }
  service = std::make_unique<TransactionService>(
      *repository, clock, ids,
      ServiceLimits{.max_transaction_minor = config.max_transaction_minor});
  if (config.business_date) {
    auto date = BusinessDate::parse(*config.business_date);
    if (!date) {
      return date.error();
    }
    service->set_business_date_override(date.value());
  }
  eod = std::make_unique<EodService>(*repository, clock, ids,
                                     EodOptions{.batch_dir = config.batch_dir,
                                                .fail_stage = config.fail_stage,
                                                .stale_pending_after = std::chrono::seconds{60}});
  return ok();
}

int report(const Context& ctx, const Error& error) {
  ctx.out.error(error);
  return exit_code_for(error.code);
}

int report_receipt(const Context& ctx, const TransactionReceipt& receipt) {
  ctx.out.receipt(receipt);
  return receipt.posted() ? kOk : kRejected;
}

Result<Money> parse_amount(const Args& args) {
  auto amount = args.required("amount");
  if (!amount) {
    return amount.error();
  }
  auto currency_text = args.required("currency");
  if (!currency_text) {
    return currency_text.error();
  }
  auto currency = CurrencyCode::parse(currency_text.value());
  if (!currency) {
    return currency.error();
  }
  return Money::parse(amount.value(), currency.value());
}

Result<IdempotencyKey> idempotency_key(Context& ctx, const Args& args) {
  if (const auto key = args.option("key")) {
    return IdempotencyKey::parse(*key);
  }
  // No key supplied: generate one so that the stored transaction still carries it.
  return ctx.ids.next_idempotency_key();
}

int run(int argc, const char* const* argv) {
  auto parsed = Args::parse(argc, argv, {"json", "allow-overdraft", "help", "version"});
  if (!parsed) {
    std::cerr << "error: " << parsed.error().message << "\n\n" << usage_text();
    return kUsage;
  }
  const Args& args = parsed.value();
  const std::string command = args.positional(0);
  if (args.flag("help") || command.empty() || command == "help") {
    std::cout << usage_text();
    return command.empty() && !args.flag("help") ? kUsage : kOk;
  }
  if (args.flag("version") || command == "version") {
    std::cout << "corebank " << COREBANK_VERSION << '\n';
    return kOk;
  }

  Context ctx;
  ctx.out.json = args.flag("json");

  std::optional<std::string> config_path = args.option("config");
  if (!config_path) {
    if (const auto env = Config::system_env()("COREBANK_CONFIG")) {
      config_path = *env;
    }
  }
  auto config = Config::load(config_path);
  if (!config) {
    return report(ctx, config.error());
  }
  ctx.config = config.value();
  if (const auto db = args.option("db")) {
    ctx.config.database_path = *db;
  }
  if (const auto date = args.option("business-date")) {
    ctx.config.business_date = *date;
  }
  if (const auto level = args.option("log-level")) {
    auto parsed_level = parse_log_level(*level);
    if (!parsed_level) {
      return report(ctx, parsed_level.error());
    }
    ctx.config.log_level = parsed_level.value();
  }
  Logger::instance().set_level(ctx.config.log_level);
  if (!ctx.config.log_file.empty()) {
    if (auto file = Logger::instance().set_file(ctx.config.log_file); !file) {
      return report(ctx, file.error());
    }
  }

  if (command == "account") {
    return cmd_account(ctx, args);
  }
  if (command == "transaction") {
    return cmd_transaction(ctx, args);
  }
  if (command == "transfer") {
    return cmd_transfer(ctx, args);
  }
  if (command == "reconcile") {
    return cmd_reconcile(ctx, args);
  }
  if (command == "eod") {
    return cmd_eod(ctx, args);
  }
  if (command == "audit") {
    return cmd_audit(ctx, args);
  }
  if (command == "health") {
    return cmd_health(ctx, args);
  }
  if (command == "config") {
    return cmd_config(ctx, args);
  }
  if (command == "serve") {
    return cmd_serve(ctx, args);
  }
  if (command == "send") {
    return cmd_send(ctx, args);
  }
  std::cerr << "error: unknown command '" << command << "'\n\n" << usage_text();
  return kUsage;
}

}  // namespace corebank::cli
