#include "corebank/cli/app.hpp"
#include "corebank/infrastructure/json_writer.hpp"
#include "corebank/messaging/tcp_client.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>

namespace corebank::cli {
namespace {

Result<std::optional<BusinessDate>> optional_date(const Args& args) {
  if (const auto text = args.option("date")) {
    auto date = BusinessDate::parse(*text);
    if (!date) {
      return date.error();
    }
    return std::optional<BusinessDate>{date.value()};
  }
  return std::optional<BusinessDate>{};
}

}  // namespace

int cmd_reconcile(Context& ctx, const Args& args) {
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  auto date = optional_date(args);
  if (!date) {
    return report(ctx, date.error());
  }
  ReconciliationService reconciliation{*ctx.repository, ctx.clock};
  auto result = reconciliation.run(date.value());
  if (!result) {
    return report(ctx, result.error());
  }
  ctx.out.reconciliation(result.value());
  return result.value().passed ? kOk : kControlFailed;
}

int cmd_eod(Context& ctx, const Args& args) {
  const std::string sub = args.positional(1);
  if (sub != "run" && sub != "status" && sub != "history") {
    std::cerr << "error: eod expects run | status | history\n";
    return kUsage;
  }
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  auto date = optional_date(args);
  if (!date) {
    return report(ctx, date.error());
  }
  if (sub == "history") {
    auto limit = args.integer("limit", 20);
    if (!limit) {
      return report(ctx, limit.error());
    }
    auto runs = ctx.eod->history(static_cast<std::size_t>(limit.value()));
    if (!runs) {
      return report(ctx, runs.error());
    }
    ctx.out.batches(runs.value());
    return kOk;
  }
  if (sub == "status") {
    auto status = ctx.eod->status(date.value());
    if (!status) {
      return report(ctx, status.error());
    }
    if (!status.value()) {
      ctx.out.message("no batch run recorded", "status");
      return kOk;
    }
    ctx.out.batch(*status.value());
    return status.value()->status == BatchStatus::Failed ? kControlFailed : kOk;
  }
  const BusinessDate business_date =
      date.value() ? *date.value() : ctx.service->current_business_date();
  auto outcome = ctx.eod->run(business_date);
  if (!outcome) {
    // Show the recorded state so operators see which stage failed.
    if (auto status = ctx.eod->status(business_date); status && status.value()) {
      ctx.out.batch(*status.value());
    }
    return report(ctx, outcome.error());
  }
  ctx.out.batch(outcome.value().run);
  if (!ctx.out.json) {
    if (outcome.value().already_completed) {
      std::cout << "batch already completed for " << business_date.str() << ", nothing to do\n";
    } else if (outcome.value().resumed) {
      std::cout << "batch resumed and completed (attempt " << outcome.value().run.attempt << ")\n";
    }
  }
  return kOk;
}

int cmd_audit(Context& ctx, const Args& args) {
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  AuditQuery query;
  query.entity_id = args.option("entity");
  if (const auto batch = args.option("batch")) {
    auto id = BatchId::parse(*batch);
    if (!id) {
      return report(ctx, id.error());
    }
    query.batch_id = id.value();
  }
  if (const auto type = args.option("type")) {
    auto parsed = parse_audit_event_type(*type);
    if (!parsed) {
      return report(ctx, parsed.error());
    }
    query.type = parsed.value();
  }
  auto limit = args.integer("limit", 100);
  if (!limit) {
    return report(ctx, limit.error());
  }
  query.limit = static_cast<std::size_t>(limit.value());
  auto events = ctx.service->audit_trail(query);
  if (!events) {
    return report(ctx, events.error());
  }
  ctx.out.audit(events.value());
  return kOk;
}

int cmd_health(Context& ctx, const Args& args) {
  if (const auto host = args.option("host")) {
    auto port = args.integer("port", ctx.config.listen_port);
    if (!port) {
      return report(ctx, port.error());
    }
    auto client = TcpClient::connect(*host, static_cast<std::uint16_t>(port.value()),
                                     std::chrono::milliseconds{2000});
    if (!client) {
      return report(ctx, client.error());
    }
    auto response = client.value().request("PING", std::chrono::milliseconds{2000});
    if (!response) {
      return report(ctx, response.error());
    }
    if (!response.value().starts_with("OK|")) {
      return report(
          ctx, make_error(ErrorCode::NetworkError, "unexpected response: " + response.value()));
    }
    if (ctx.out.json) {
      JsonWriter w;
      w.boolean("ok", true).string("transport", "tcp").string("response", response.value());
      std::cout << w.str() << '\n';
    } else {
      std::cout << "engine healthy: " << response.value() << '\n';
    }
    return kOk;
  }
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  if (auto healthy = ctx.repository->health_check(); !healthy) {
    return report(ctx, healthy.error());
  }
  if (ctx.out.json) {
    JsonWriter w;
    w.boolean("ok", true)
        .string("transport", "database")
        .string("database", ctx.config.database_path);
    std::cout << w.str() << '\n';
  } else {
    std::cout << "database healthy: " << ctx.config.database_path << '\n';
  }
  return kOk;
}

int cmd_config(const Context& ctx, const Args& args) {
  if (args.positional(1) != "show") {
    std::cerr << "error: config expects show\n";
    return kUsage;
  }
  if (ctx.out.json) {
    std::cout << ctx.config.to_json() << '\n';
  } else {
    std::cout << ctx.config.describe();
  }
  return kOk;
}

int cmd_send(const Context& ctx, const Args& args) {
  auto host = args.required("host");
  if (!host) {
    return report(ctx, host.error());
  }
  auto port = args.integer("port", ctx.config.listen_port);
  if (!port) {
    return report(ctx, port.error());
  }
  const std::string message = args.positional(1);
  if (message.empty()) {
    return report(ctx, make_error(ErrorCode::ValidationError, "send requires a MESSAGE argument"));
  }
  auto client = TcpClient::connect(host.value(), static_cast<std::uint16_t>(port.value()),
                                   std::chrono::milliseconds{2000});
  if (!client) {
    return report(ctx, client.error());
  }
  auto response = client.value().request(
      message, std::chrono::milliseconds{ctx.config.request_timeout_ms + 1000});
  if (!response) {
    return report(ctx, response.error());
  }
  if (ctx.out.json) {
    JsonWriter w;
    w.boolean("ok", response.value().starts_with("OK|")).string("response", response.value());
    std::cout << w.str() << '\n';
  } else {
    std::cout << response.value() << '\n';
  }
  return response.value().starts_with("OK|") ? kOk : kRejected;
}

}  // namespace corebank::cli
