#include "corebank/application/transaction_processor.hpp"
#include "corebank/cli/app.hpp"
#include "corebank/infrastructure/logger.hpp"
#include "corebank/infrastructure/signal_handler.hpp"
#include "corebank/messaging/message_handler.hpp"
#include "corebank/messaging/tcp_server.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace corebank::cli {

int cmd_serve(Context& ctx, const Args& args) {
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  if (const auto address = args.option("address")) {
    ctx.config.listen_address = *address;
  }
  auto port = args.integer("port", ctx.config.listen_port);
  if (!port) {
    return report(ctx, port.error());
  }
  if (auto installed = SignalHandler::install(); !installed) {
    return report(ctx, installed.error());
  }

  TransactionProcessor processor{
      *ctx.service, ProcessorOptions{.worker_count = ctx.config.worker_count,
                                     .queue_capacity = ctx.config.queue_size,
                                     .enqueue_timeout = std::chrono::milliseconds{200}}};
  processor.start();
  MessageHandler handler{*ctx.service, processor,
                         std::chrono::milliseconds{ctx.config.request_timeout_ms},
                         ctx.config.max_message_bytes};
  TcpServer server{
      TcpServerOptions{.address = ctx.config.listen_address,
                       .port = static_cast<std::uint16_t>(port.value()),
                       .max_connections = ctx.config.max_connections,
                       .idle_timeout = std::chrono::seconds{ctx.config.idle_timeout_seconds},
                       .max_line_bytes = ctx.config.max_message_bytes},
      handler};
  if (auto started = server.start(); !started) {
    processor.shutdown_now();
    return report(ctx, started.error());
  }
  {
    auto uow = ctx.repository->begin(TransactionMode::ReadWrite);
    if (uow) {
      AuditEvent event{.id = 0,
                       .occurred_at = ctx.clock.now(),
                       .type = AuditEventType::EngineStarted,
                       .entity_type = "engine",
                       .entity_id = ctx.config.listen_address + ":" + std::to_string(server.port()),
                       .detail = ctx.config.to_json(),
                       .batch_id = std::nullopt};
      if (uow.value()->append_audit(event)) {
        [[maybe_unused]] const auto committed = uow.value()->commit();
      }
    }
  }
  Logger::instance().info("engine.started",
                          {{"address", ctx.config.listen_address},
                           {"port", static_cast<std::int64_t>(server.port())},
                           {"workers", static_cast<std::int64_t>(ctx.config.worker_count)},
                           {"database", ctx.config.database_path}});
  if (!ctx.out.json) {
    std::cout << "corebank engine listening on " << ctx.config.listen_address << ":"
              << server.port() << " (SIGTERM/SIGINT to stop)\n";
  }

  while (!SignalHandler::wait(std::chrono::milliseconds{500})) {
  }
  Logger::instance().info("engine.stopping", {{"signal", SignalHandler::last_signal()}});

  // Graceful order: stop accepting connections, finish queued work, then persist the stop.
  server.stop();
  processor.shutdown_gracefully();
  {
    auto uow = ctx.repository->begin(TransactionMode::ReadWrite);
    if (uow) {
      const auto stats = processor.stats();
      AuditEvent event{.id = 0,
                       .occurred_at = ctx.clock.now(),
                       .type = AuditEventType::EngineStopped,
                       .entity_type = "engine",
                       .entity_id = ctx.config.listen_address + ":" + std::to_string(server.port()),
                       .detail = "{\"signal\":" + std::to_string(SignalHandler::last_signal()) +
                                 ",\"completed\":" + std::to_string(stats.completed) + "}",
                       .batch_id = std::nullopt};
      if (uow.value()->append_audit(event)) {
        [[maybe_unused]] const auto committed = uow.value()->commit();
      }
    }
  }
  Logger::instance().info("engine.stopped", {{"completed", processor.stats().completed}});
  SignalHandler::uninstall();
  return kOk;
}

}  // namespace corebank::cli
