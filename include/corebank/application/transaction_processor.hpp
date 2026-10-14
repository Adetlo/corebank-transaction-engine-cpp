// Worker pool in front of the TransactionService: bounded queue, N std::jthread
// workers, graceful shutdown that drains or fails pending work deterministically.
#pragma once

#include "corebank/application/bounded_queue.hpp"
#include "corebank/application/requests.hpp"
#include "corebank/application/transaction_service.hpp"
#include "corebank/domain/result.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace corebank {

using FinancialRequest = std::variant<CreditRequest, DebitRequest, TransferRequest, ReverseRequest>;

struct ProcessorOptions {
  std::size_t worker_count{4};
  std::size_t queue_capacity{1024};
  // How long submit() waits for queue space before failing with QueueFull.
  std::chrono::milliseconds enqueue_timeout{200};
};

struct ProcessorStats {
  std::uint64_t submitted{0};
  std::uint64_t completed{0};
  std::uint64_t rejected_queue_full{0};
  std::uint64_t rejected_shutting_down{0};
  std::size_t queued{0};
  std::size_t workers{0};
  bool accepting{false};
};

class TransactionProcessor {
 public:
  TransactionProcessor(TransactionService& service, ProcessorOptions options);
  TransactionProcessor(const TransactionProcessor&) = delete;
  TransactionProcessor& operator=(const TransactionProcessor&) = delete;
  TransactionProcessor(TransactionProcessor&&) = delete;
  TransactionProcessor& operator=(TransactionProcessor&&) = delete;
  ~TransactionProcessor();

  void start();

  // Queues the request; the future resolves with the service outcome.
  [[nodiscard]] std::future<Result<TransactionReceipt>> submit(FinancialRequest request);

  // Stops accepting work, lets workers finish what is queued, joins them.
  void shutdown_gracefully();
  // Stops accepting work, fails whatever is still queued with ShuttingDown, joins workers.
  void shutdown_now();

  [[nodiscard]] bool accepting() const noexcept { return accepting_.load(); }
  [[nodiscard]] ProcessorStats stats() const;

 private:
  struct Job {
    explicit Job(FinancialRequest r) : request(std::move(r)) {}
    FinancialRequest request;
    std::promise<Result<TransactionReceipt>> promise;
  };

  void worker_loop(const std::stop_token& stop);
  void fail_pending();

  TransactionService& service_;
  ProcessorOptions options_;
  BoundedQueue<std::unique_ptr<Job>> queue_;
  std::vector<std::jthread> workers_;
  std::atomic<bool> accepting_{false};
  std::atomic<bool> started_{false};
  std::atomic<std::size_t> worker_count_{0};
  std::atomic<std::uint64_t> submitted_{0};
  std::atomic<std::uint64_t> completed_{0};
  std::atomic<std::uint64_t> rejected_queue_full_{0};
  std::atomic<std::uint64_t> rejected_shutting_down_{0};
};

}  // namespace corebank
