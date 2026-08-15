// Deterministic randomised invariant tests (seeded std::mt19937) in place of a
// property-testing dependency. See docs/TESTING.md for the rationale.
#include "corebank/application/reconciliation_service.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class InvariantsTest : public EngineFixture {
 protected:
  std::vector<std::string> seed_accounts(std::mt19937& rng, int count,
                                         const std::string& prefix = "P") {
    std::vector<std::string> accounts;
    std::uniform_int_distribution<std::int64_t> opening(0, 5'000);
    for (int i = 0; i < count; ++i) {
      const auto id = prefix + std::to_string(i);
      (void)make_account(id);
      const auto amount = opening(rng);
      if (amount > 0) {
        (void)fund(id, amount);
      }
      accounts.push_back(id);
    }
    return accounts;
  }
  std::int64_t total(const std::vector<std::string>& accounts) {
    std::int64_t sum = 0;
    for (const auto& id : accounts) {
      sum += balance(id);
    }
    return sum;
  }
};

TEST_F(InvariantsTest, MoneyIsConservedAcrossRandomInternalTransfers) {
  for (unsigned seed : {1U, 7U, 42U}) {
    std::mt19937 rng(seed);
    auto accounts = seed_accounts(rng, 5, "S" + std::to_string(seed) + "-");
    const auto before = total(accounts);
    std::uniform_int_distribution<std::size_t> pick(0, accounts.size() - 1);
    std::uniform_int_distribution<std::int64_t> amount(1, 3'000);
    for (int i = 0; i < 300; ++i) {
      const auto from = pick(rng);
      auto to = pick(rng);
      if (to == from) {
        to = (to + 1) % accounts.size();
      }
      auto receipt = service->transfer(TransferRequest{.key = key(),
                                                       .source = acc(accounts[from]),
                                                       .destination = acc(accounts[to]),
                                                       .amount = gbp(amount(rng)),
                                                       .reference = ref("p")});
      ASSERT_TRUE(receipt.has_value());
      EXPECT_EQ(total(accounts), before) << "seed " << seed << " step " << i;
    }
    for (const auto& id : accounts) {
      EXPECT_GE(balance(id), 0) << "overdraft disabled must never go negative (seed " << seed
                                << ")";
    }
    ReconciliationService reconciliation{*repository, clock};
    EXPECT_TRUE(reconciliation.run(std::nullopt).value().passed);
  }
}

TEST_F(InvariantsTest, ReversalRestoresOriginalNetState) {
  std::mt19937 rng(2026);
  auto accounts = seed_accounts(rng, 4);
  std::uniform_int_distribution<std::size_t> pick(0, accounts.size() - 1);
  std::uniform_int_distribution<std::int64_t> amount(1, 2'000);
  for (int i = 0; i < 100; ++i) {
    std::vector<std::int64_t> snapshot;
    for (const auto& id : accounts) {
      snapshot.push_back(balance(id));
    }
    const auto from = pick(rng);
    auto to = pick(rng);
    if (to == from) {
      to = (to + 1) % accounts.size();
    }
    auto receipt = service->transfer(TransferRequest{.key = key(),
                                                     .source = acc(accounts[from]),
                                                     .destination = acc(accounts[to]),
                                                     .amount = gbp(amount(rng)),
                                                     .reference = ref("p")});
    ASSERT_TRUE(receipt.has_value());
    if (!receipt.value().posted()) {
      continue;
    }
    auto reversal = service->reverse(ReverseRequest{
        .key = key(), .original = receipt.value().transaction.id, .reference = ref("undo")});
    ASSERT_TRUE(reversal.has_value());
    ASSERT_TRUE(reversal.value().posted());
    for (std::size_t a = 0; a < accounts.size(); ++a) {
      EXPECT_EQ(balance(accounts[a]), snapshot[a]) << "step " << i << " account " << accounts[a];
    }
  }
}

TEST_F(InvariantsTest, PostingChainAlwaysMatchesBalance) {
  std::mt19937 rng(99);
  auto accounts = seed_accounts(rng, 3);
  std::uniform_int_distribution<int> op(0, 2);
  std::uniform_int_distribution<std::size_t> pick(0, accounts.size() - 1);
  std::uniform_int_distribution<std::int64_t> amount(1, 500);
  for (int i = 0; i < 200; ++i) {
    const auto target = accounts[pick(rng)];
    switch (op(rng)) {
      case 0:
        ASSERT_TRUE(service
                        ->credit(CreditRequest{.key = key(),
                                               .account = acc(target),
                                               .amount = gbp(amount(rng)),
                                               .reference = ref("c")})
                        .has_value());
        break;
      case 1:
        ASSERT_TRUE(service
                        ->debit(DebitRequest{.key = key(),
                                             .account = acc(target),
                                             .amount = gbp(amount(rng)),
                                             .reference = ref("d")})
                        .has_value());
        break;
      default: {
        std::size_t to_index = pick(rng);
        if (accounts[to_index] == target) {
          to_index = (to_index + 1) % accounts.size();
        }
        const auto to = accounts[to_index];
        ASSERT_TRUE(service
                        ->transfer(TransferRequest{.key = key(),
                                                   .source = acc(target),
                                                   .destination = acc(to),
                                                   .amount = gbp(amount(rng)),
                                                   .reference = ref("t")})
                        .has_value());
      }
    }
  }
  for (const auto& id : accounts) {
    const auto history = service->account_history(acc(id), 10'000).value();
    std::int64_t derived = 0;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
      derived += signed_minor(*it);
      EXPECT_EQ(it->balance_after_minor, derived) << id;
    }
    EXPECT_EQ(derived, balance(id)) << id;
    EXPECT_GE(balance(id), 0);
  }
}

}  // namespace
}  // namespace corebank::testing
