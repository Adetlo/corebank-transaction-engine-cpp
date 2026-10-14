// Drives the real `corebank` binary: exit codes, JSON output, end-to-end flows.
#include <sys/wait.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

struct CommandResult {
  int exit_code{-1};
  std::string output;
};

CommandResult run_shell(const std::string& command) {
  std::array<char, 512> buffer{};
  std::string output;
  FILE* pipe = ::popen((command + " 2>&1").c_str(), "r");  // NOLINT(cert-env33-c) test harness only
  if (pipe == nullptr) {
    return {};
  }
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  int status = ::pclose(pipe);
  return CommandResult{.exit_code = WEXITSTATUS(status), .output = output};
}

CommandResult run_cli(const std::string& db, const std::string& args) {
  return run_shell(std::string{COREBANK_CLI_PATH} + " --db '" + db + "' " + args);
}

class CliTest : public ::testing::Test {
 protected:
  CommandResult cli(const std::string& args) { return run_cli(temp.db_path(), args); }
  TempDir temp;
};

TEST_F(CliTest, UsageAndVersion) {
  const auto no_args = run_cli(temp.db_path(), "");
  EXPECT_EQ(no_args.exit_code, 1);
  EXPECT_NE(no_args.output.find("Usage: corebank"), std::string::npos);
  EXPECT_EQ(cli("--help").exit_code, 0);
  const auto version = cli("version");
  EXPECT_EQ(version.exit_code, 0);
  EXPECT_NE(version.output.find("corebank 1."), std::string::npos);
  EXPECT_EQ(cli("frobnicate").exit_code, 1);
  EXPECT_EQ(cli("account create --id ACC-1").exit_code, 1);  // missing options
}

TEST_F(CliTest, FullFlowWithExitCodes) {
  EXPECT_EQ(cli("account create --id SETTLE --name 'Settlement' --currency GBP --type SETTLEMENT")
                .exit_code,
            0);
  EXPECT_EQ(cli("account create --id CUST-A --name 'Customer A' --currency GBP").exit_code, 0);
  EXPECT_EQ(cli("account create --id CUST-A --name 'Customer A' --currency GBP").exit_code, 2);
  EXPECT_EQ(
      cli("transaction credit --account SETTLE --amount 1000.00 --currency GBP --key K1").exit_code,
      0);
  EXPECT_EQ(
      cli("transfer --from SETTLE --to CUST-A --amount 250.00 --currency GBP --key T1").exit_code,
      0);
  const auto replay =
      cli("transfer --from SETTLE --to CUST-A --amount 250.00 --currency GBP --key T1");
  EXPECT_EQ(replay.exit_code, 0);
  EXPECT_NE(replay.output.find("(replayed)"), std::string::npos);
  EXPECT_EQ(
      cli("transfer --from SETTLE --to CUST-A --amount 251.00 --currency GBP --key T1").exit_code,
      2);
  const auto nsf = cli("transfer --from CUST-A --to SETTLE --amount 9999.00 --currency GBP");
  EXPECT_EQ(nsf.exit_code, 2);
  EXPECT_NE(nsf.output.find("INSUFFICIENT_FUNDS"), std::string::npos);
  EXPECT_EQ(cli("transaction debit --account CUST-A --amount abc --currency GBP").exit_code, 1);
  const auto show = cli("--json account show --id CUST-A");
  EXPECT_EQ(show.exit_code, 0);
  EXPECT_NE(show.output.find("\"balance\":\"250.00\""), std::string::npos) << show.output;
  EXPECT_EQ(cli("account history --id CUST-A").exit_code, 0);
  EXPECT_EQ(cli("account history --id GHOST").exit_code, 2);
  EXPECT_EQ(cli("account list").exit_code, 0);
  EXPECT_EQ(cli("reconcile").exit_code, 0);
  EXPECT_EQ(cli("health").exit_code, 0);
  EXPECT_EQ(cli("audit --limit 5").exit_code, 0);
  EXPECT_EQ(cli("config show").exit_code, 0);
}

TEST_F(CliTest, ReverseAndShowTransaction) {
  ASSERT_EQ(cli("account create --id A --name A --currency GBP").exit_code, 0);
  ASSERT_EQ(cli("account create --id B --name B --currency GBP").exit_code, 0);
  ASSERT_EQ(cli("transaction credit --account A --amount 10 --currency GBP").exit_code, 0);
  const auto transfer = cli("--json transfer --from A --to B --amount 4 --currency GBP");
  ASSERT_EQ(transfer.exit_code, 0);
  const auto pos = transfer.output.find("\"transaction_id\":\"");
  ASSERT_NE(pos, std::string::npos);
  const auto start = pos + 18;
  const auto id = transfer.output.substr(start, transfer.output.find('"', start) - start);
  EXPECT_EQ(cli("transaction show --id " + id).exit_code, 0);
  EXPECT_EQ(cli("transaction reverse --id " + id + " --reference undo").exit_code, 0);
  EXPECT_EQ(cli("transaction reverse --id " + id).exit_code, 2);
  const auto shown = cli("--json transaction show --id " + id);
  EXPECT_NE(shown.output.find("\"status\":\"REVERSED\""), std::string::npos);
  EXPECT_EQ(cli("transaction show --id TXN-NONE").exit_code, 2);
}

TEST_F(CliTest, EodRunStatusAndFailureInjection) {
  const std::string batch = "COREBANK_BATCH_DIR='" + (temp.path() / "batch").string() + "' ";
  const std::string bin = std::string{COREBANK_CLI_PATH} + " --db '" + temp.db_path() + "' ";
  ASSERT_EQ(cli("account create --id A --name A --currency GBP").exit_code, 0);
  ASSERT_EQ(cli("transaction credit --account A --amount 1 --currency GBP").exit_code, 0);
  EXPECT_EQ(cli("eod status").exit_code, 0);

  const auto failed =
      run_shell("COREBANK_FAIL_STAGE=ARCHIVE " + batch + bin + "eod run --date 2026-10-15");
  EXPECT_EQ(failed.exit_code, 3);
  EXPECT_NE(failed.output.find("ARCHIVE"), std::string::npos) << failed.output;

  const auto status = run_shell(batch + bin + "eod status --date 2026-10-15");
  EXPECT_EQ(status.exit_code, 3);
  EXPECT_NE(status.output.find("FAILED"), std::string::npos) << status.output;

  const auto resumed = run_shell(batch + bin + "eod run --date 2026-10-15");
  EXPECT_EQ(resumed.exit_code, 0);
  EXPECT_NE(resumed.output.find("resumed"), std::string::npos) << resumed.output;
  EXPECT_EQ(run_shell(batch + bin + "eod run --date 2026-10-15").exit_code, 0);
  EXPECT_EQ(cli("eod history").exit_code, 0);
}

TEST_F(CliTest, ReconcileFailsWithControlExitCodeOnCorruption) {
  ASSERT_EQ(cli("account create --id A --name A --currency GBP").exit_code, 0);
  ASSERT_EQ(cli("transaction credit --account A --amount 5 --currency GBP").exit_code, 0);
  const std::string tamper =
      "sqlite3 '" + temp.db_path() + "' \"UPDATE accounts SET balance_minor = 999\"";
  if (std::system("command -v sqlite3 >/dev/null 2>&1") !=
      0) {  // NOLINT(cert-env33-c,concurrency-mt-unsafe)
    GTEST_SKIP() << "sqlite3 CLI not available";
  }
  ASSERT_EQ(std::system(tamper.c_str()), 0);  // NOLINT(cert-env33-c,concurrency-mt-unsafe)
  const auto result = cli("reconcile");
  EXPECT_EQ(result.exit_code, 3);
  EXPECT_NE(result.output.find("FAIL"), std::string::npos);
}

TEST_F(CliTest, HealthAgainstUnreachableEngineIsSystemError) {
  const auto result = cli("health --host 127.0.0.1 --port 1");
  EXPECT_EQ(result.exit_code, 4);
}

}  // namespace
}  // namespace corebank::testing
