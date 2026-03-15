#include "corebank/application/transaction_processor.hpp"

#include "corebank/infrastructure/logger.hpp"

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>

namespace corebank {

TransactionProcessor::TransactionProcessor(TransactionService& service, ProcessorOptions options)
    : service_(service), options_(options), queue_(options.queue_capacity) {}

TransactionProcessor::~TransactionProcessor() {
  if (started_.load()) {
    shutdown_now();
  }
}

void TransactionProcessor::start() {
  if (started_.exchange(true)) {
    return;
  }
  workers_.reserve(options_.worker_count);
  for (std::size_t i = 0; i < options_.worker_count; ++i) {
    workers_.emplace_back([this](const std::stop_token& stop) { worker_loop(stop); });
  }
  worker_count_.store(workers_.size());
  accepting_.store(true);
  Logger::instance().info("processor.started",
                          {{"workers", static_cast<std::int64_t>(options_.worker_count)},
                           {"queue_capacity", static_cast<std::int64_t>(options_.queue_capacity)}});
}

std::future<Result<TransactionReceipt>> TransactionProcessor::submit(FinancialRequest request) {
  auto job = std::make_unique<Job>(std::move(request));
  auto future = job->promise.get_future();
  submitted_.fetch_add(1, std::memory_order_relaxed);

  if (!accepting_.load()) {
    rejected_shutting_down_.fetch_add(1, std::memory_order_relaxed);
    job->promise.set_value(make_error(ErrorCode::ShuttingDown, "processor is shutting down"));
    return future;
  }
  // Bounded wait for queue space: back-pressure instead of unbounded memory growth.
  if (!queue_.try_push_for(job, options_.enqueue_timeout)) {
    rejected_queue_full_.fetch_add(1, std::memory_order_relaxed);
    job->promise.set_value(make_error(ErrorCode::QueueFull, "request queue is full"));
  }
  return future;
}

void TransactionProcessor::worker_loop(const std::stop_token& stop) {
  while (true) {
    auto job = queue_.pop(stop);
    if (!job) {
      return;
    }
    auto& request = (*job)->request;
    Result<TransactionReceipt> outcome = std::visit(
        [this](const auto& typed) -> Result<TransactionReceipt> {
          using R = std::decay_t<decltype(typed)>;
          if constexpr (std::is_same_v<R, CreditRequest>) {
            return service_.credit(typed);
          } else if constexpr (std::is_same_v<R, DebitRequest>) {
            return service_.debit(typed);
          } else if constexpr (std::is_same_v<R, TransferRequest>) {
            return service_.transfer(typed);
          } else {
            return service_.reverse(typed);
          }
        },
        request);
    (*job)->promise.set_value(std::move(outcome));
    completed_.fetch_add(1, std::memory_order_relaxed);
  }
}

void TransactionProcessor::fail_pending() {
  for (const auto& job : queue_.drain()) {
    rejected_shutting_down_.fetch_add(1, std::memory_order_relaxed);
    job->promise.set_value(
        make_error(ErrorCode::ShuttingDown, "processor stopped before the request ran"));
  }
}

void TransactionProcessor::shutdown_gracefully() {
  if (!started_.load()) {
    return;
  }
  accepting_.store(false);
  queue_.close();  // workers drain what is left, then pop() returns nullopt
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
  worker_count_.store(0);
  fail_pending();  // nothing should remain, but never leave a promise unresolved
  started_.store(false);
  Logger::instance().info("processor.stopped",
                          {{"mode", "graceful"}, {"completed", completed_.load()}});
}

void TransactionProcessor::shutdown_now() {
  if (!started_.load()) {
    return;
  }
  accepting_.store(false);
  queue_.close();
  fail_pending();
  for (auto& worker : workers_) {
    worker.request_stop();
  }
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
  worker_count_.store(0);
  fail_pending();
  started_.store(false);
  Logger::instance().info("processor.stopped",
                          {{"mode", "immediate"}, {"completed", completed_.load()}});
}

ProcessorStats TransactionProcessor::stats() const {
  return ProcessorStats{
      .submitted = submitted_.load(),
      .completed = completed_.load(),
      .rejected_queue_full = rejected_queue_full_.load(),
      .rejected_shutting_down = rejected_shutting_down_.load(),
      .queued = queue_.size(),
      .workers = worker_count_.load(),
      .accepting = accepting_.load(),
  };
}

}  // namespace corebank
