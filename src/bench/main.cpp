// Local synthetic benchmark. Numbers describe this machine and this SQLite file only;
// they are not a statement about any production banking system.
#include "corebank/application/id_generator.hpp"
#include "corebank/application/transaction_processor.hpp"
#include "corebank/application/transaction_service.hpp"
#include "corebank/batch/eod_service.hpp"
#include "corebank/infrastructure/json_writer.hpp"
#include "corebank/infrastructure/logger.hpp"
#include "corebank/infrastructure/sqlite/sqlite_repository.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Stopwatch = std::chrono::steady_clock;

struct Options {
  std::string db_path;
  int accounts{32};
  int transactions{5000};
  int threads{4};
  int window{32};  // in-flight requests in the closed-loop multi-thread run
  bool json{false};
};

double percentile(std::vector<double> samples, double p) {
  if (samples.empty()) {
    return 0.0;
  }
  std::sort(samples.begin(), samples.end());
  const auto index = static_cast<std::size_t>(p * static_cast<double>(samples.size() - 1));
  return samples[index];
}

struct Run {
  std::string name;
  double seconds{0};
  int count{0};
  double p50_ms{0};
  double p95_ms{0};
  [[nodiscard]] double per_second() const { return seconds > 0 ? count / seconds : 0; }
};

std::optional<Options> parse(int argc, char** argv) {
  Options options;
  options.db_path = (std::filesystem::temp_directory_path() / "corebank-bench.db").string();
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (arg == "--db") {
      options.db_path = next();
    } else if (arg == "--accounts") {
      options.accounts = std::atoi(next().c_str());
    } else if (arg == "--transactions") {
      options.transactions = std::atoi(next().c_str());
    } else if (arg == "--threads") {
      options.threads = std::atoi(next().c_str());
    } else if (arg == "--window") {
      options.window = std::atoi(next().c_str());
    } else if (arg == "--json") {
      options.json = true;
    } else {
      std::cerr << "usage: corebank-bench [--db PATH] [--accounts N] [--transactions N] "
                   "[--threads N] [--window N] [--json]\n";
      return std::nullopt;
    }
  }
  options.accounts = std::max(2, options.accounts);
  options.transactions = std::max(1, options.transactions);
  options.threads = std::max(1, options.threads);
  options.window = std::max(1, options.window);
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace corebank;
  const auto parsed = parse(argc, argv);
  if (!parsed) {
    return 1;
  }
  const Options& options = *parsed;
  Logger::instance().set_level(LogLevel::Error);

  std::error_code ec;
  std::filesystem::remove(options.db_path, ec);
  std::filesystem::remove(options.db_path + "-wal", ec);
  std::filesystem::remove(options.db_path + "-shm", ec);
  auto repository = sqlite::SqliteRepository::open(
      options.db_path,
      sqlite::SqliteRepository::Options{.pool_size = static_cast<std::size_t>(options.threads) + 2,
                                        .busy_timeout_ms = 10000});
  if (!repository || !repository.value()->migrate()) {
    std::cerr << "cannot open benchmark database\n";
    return 1;
  }
  SystemClock clock;
  IdGenerator ids;
  TransactionService service{*repository.value(), clock, ids};
  const auto gbp = CurrencyCode::unchecked("GBP");

  std::vector<AccountId> accounts;
  for (int i = 0; i < options.accounts; ++i) {
    auto id = AccountId::parse("BENCH-" + std::to_string(i)).value();
    auto created = service.create_account(CreateAccountRequest{.id = id,
                                                               .name = "Bench " + std::to_string(i),
                                                               .type = AccountType::Customer,
                                                               .currency = gbp,
                                                               .allow_overdraft = false});
    if (!created) {
      std::cerr << "account creation failed: " << created.error().message << '\n';
      return 1;
    }
    auto funded = service.credit(CreditRequest{.key = ids.next_idempotency_key(),
                                               .account = id,
                                               .amount = Money{100'000'000, gbp},
                                               .reference = JournalReference::unchecked("seed")});
    if (!funded || !funded.value().posted()) {
      std::cerr << "seed credit failed\n";
      return 1;
    }
    accounts.push_back(id);
  }

  std::vector<Run> runs;
  // 1. Single-threaded transfers through the service.
  {
    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(options.transactions));
    const auto start = Stopwatch::now();
    for (int i = 0; i < options.transactions; ++i) {
      const auto& from = accounts[static_cast<std::size_t>(i) % accounts.size()];
      const auto& to = accounts[static_cast<std::size_t>(i + 1) % accounts.size()];
      const auto t0 = Stopwatch::now();
      auto receipt =
          service.transfer(TransferRequest{.key = ids.next_idempotency_key(),
                                           .source = from,
                                           .destination = to,
                                           .amount = Money{125, gbp},
                                           .reference = JournalReference::unchecked("st")});
      latencies.push_back(std::chrono::duration<double, std::milli>(Stopwatch::now() - t0).count());
      if (!receipt || !receipt.value().posted()) {
        std::cerr << "transfer failed\n";
        return 1;
      }
    }
    const double secs = std::chrono::duration<double>(Stopwatch::now() - start).count();
    runs.push_back(Run{"single_thread_transfers", secs, options.transactions,
                       percentile(latencies, 0.50), percentile(latencies, 0.95)});
  }
  // 2. Multi-threaded transfers through the processor (bounded queue + workers), closed
  //    loop: at most `window` requests in flight, like `window` concurrent clients.
  {
    TransactionProcessor processor{
        service, ProcessorOptions{.worker_count = static_cast<std::size_t>(options.threads),
                                  .queue_capacity = 1024,
                                  .enqueue_timeout = std::chrono::seconds{30}}};
    processor.start();
    std::deque<std::pair<std::future<Result<TransactionReceipt>>, Stopwatch::time_point>> in_flight;
    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(options.transactions));
    auto settle_front = [&]() -> bool {
      auto& [future, submitted_at] = in_flight.front();
      auto outcome = future.get();
      latencies.push_back(
          std::chrono::duration<double, std::milli>(Stopwatch::now() - submitted_at).count());
      in_flight.pop_front();
      return outcome.has_value() && outcome.value().posted();
    };
    const auto start = Stopwatch::now();
    for (int i = 0; i < options.transactions; ++i) {
      const auto& from = accounts[static_cast<std::size_t>(i * 7) % accounts.size()];
      const auto& to = accounts[static_cast<std::size_t>(i * 7 + 3) % accounts.size()];
      in_flight.emplace_back(
          processor.submit(TransferRequest{.key = ids.next_idempotency_key(),
                                           .source = from,
                                           .destination = to,
                                           .amount = Money{125, gbp},
                                           .reference = JournalReference::unchecked("mt")}),
          Stopwatch::now());
      if (in_flight.size() >= static_cast<std::size_t>(options.window) && !settle_front()) {
        std::cerr << "processor transfer failed\n";
        return 1;
      }
    }
    while (!in_flight.empty()) {
      if (!settle_front()) {
        std::cerr << "processor transfer failed\n";
        return 1;
      }
    }
    const double secs = std::chrono::duration<double>(Stopwatch::now() - start).count();
    processor.shutdown_gracefully();
    runs.push_back(Run{"multi_thread_transfers", secs, options.transactions,
                       percentile(latencies, 0.50), percentile(latencies, 0.95)});
  }
  // 3. End-of-day batch over everything posted so far.
  {
    const auto batch_dir = std::filesystem::temp_directory_path() / "corebank-bench-batch";
    EodService eod{*repository.value(), clock, ids,
                   EodOptions{.batch_dir = batch_dir.string(),
                              .fail_stage = std::nullopt,
                              .stale_pending_after = std::chrono::seconds{60}}};
    const auto start = Stopwatch::now();
    auto outcome = eod.run(service.current_business_date());
    const double secs = std::chrono::duration<double>(Stopwatch::now() - start).count();
    if (!outcome) {
      std::cerr << "eod failed: " << outcome.error().message << '\n';
      return 1;
    }
    runs.push_back(Run{"eod_batch", secs, 2 * options.transactions + options.accounts, 0, 0});
    std::filesystem::remove_all(batch_dir, ec);
  }

  if (options.json) {
    JsonArrayWriter arr;
    for (const auto& run : runs) {
      JsonWriter w;
      w.string("name", run.name)
          .number("count", run.count)
          .number("seconds", run.seconds)
          .number("per_second", run.per_second())
          .number("p50_ms", run.p50_ms)
          .number("p95_ms", run.p95_ms);
      arr.raw(w.str());
    }
    JsonWriter doc;
    doc.string("label", "local synthetic benchmark")
        .number("accounts", options.accounts)
        .number("transactions", options.transactions)
        .number("threads", options.threads)
        .number("window", options.window)
        .number("hardware_threads", static_cast<std::int64_t>(std::thread::hardware_concurrency()))
        .raw("runs", arr.str());
    std::cout << doc.str() << '\n';
  } else {
    std::printf("local synthetic benchmark (accounts=%d transactions=%d threads=%d window=%d)\n",
                options.accounts, options.transactions, options.threads, options.window);
    std::printf("%-26s %10s %10s %12s %10s %10s\n", "run", "count", "seconds", "per_second",
                "p50_ms", "p95_ms");
    for (const auto& run : runs) {
      std::printf("%-26s %10d %10.3f %12.1f %10.3f %10.3f\n", run.name.c_str(), run.count,
                  run.seconds, run.per_second(), run.p50_ms, run.p95_ms);
    }
  }
  std::filesystem::remove(options.db_path, ec);
  std::filesystem::remove(options.db_path + "-wal", ec);
  std::filesystem::remove(options.db_path + "-shm", ec);
  return 0;
}
