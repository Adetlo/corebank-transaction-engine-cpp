#include "corebank/cli/app.hpp"

#include <iostream>
#include <string>

namespace corebank::cli {

int cmd_transaction(Context& ctx, const Args& args) {
  const std::string sub = args.positional(1);
  if (sub != "credit" && sub != "debit" && sub != "show" && sub != "reverse") {
    std::cerr << "error: transaction expects credit | debit | show | reverse\n";
    return kUsage;
  }
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  if (sub == "show") {
    auto id_text = args.required("id");
    if (!id_text) {
      return report(ctx, id_text.error());
    }
    auto id = TransactionId::parse(id_text.value());
    if (!id) {
      return report(ctx, id.error());
    }
    auto txn = ctx.service->get_transaction(id.value());
    if (!txn) {
      return report(ctx, txn.error());
    }
    auto postings = ctx.service->transaction_postings(id.value());
    if (!postings) {
      return report(ctx, postings.error());
    }
    ctx.out.transaction(txn.value(), postings.value());
    return kOk;
  }
  auto key = idempotency_key(ctx, args);
  if (!key) {
    return report(ctx, key.error());
  }
  auto reference = JournalReference::parse(args.option("reference").value_or(""));
  if (!reference) {
    return report(ctx, reference.error());
  }
  if (sub == "reverse") {
    auto id_text = args.required("id");
    if (!id_text) {
      return report(ctx, id_text.error());
    }
    auto id = TransactionId::parse(id_text.value());
    if (!id) {
      return report(ctx, id.error());
    }
    auto receipt = ctx.service->reverse(
        ReverseRequest{.key = key.value(), .original = id.value(), .reference = reference.value()});
    if (!receipt) {
      return report(ctx, receipt.error());
    }
    return report_receipt(ctx, receipt.value());
  }
  auto account_text = args.required("account");
  if (!account_text) {
    return report(ctx, account_text.error());
  }
  auto account = AccountId::parse(account_text.value());
  if (!account) {
    return report(ctx, account.error());
  }
  auto amount = parse_amount(args);
  if (!amount) {
    return report(ctx, amount.error());
  }
  Result<TransactionReceipt> receipt =
      sub == "credit" ? ctx.service->credit(CreditRequest{.key = key.value(),
                                                          .account = account.value(),
                                                          .amount = amount.value(),
                                                          .reference = reference.value()})
                      : ctx.service->debit(DebitRequest{.key = key.value(),
                                                        .account = account.value(),
                                                        .amount = amount.value(),
                                                        .reference = reference.value()});
  if (!receipt) {
    return report(ctx, receipt.error());
  }
  return report_receipt(ctx, receipt.value());
}

int cmd_transfer(Context& ctx, const Args& args) {
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  auto from_text = args.required("from");
  if (!from_text) {
    return report(ctx, from_text.error());
  }
  auto to_text = args.required("to");
  if (!to_text) {
    return report(ctx, to_text.error());
  }
  auto from = AccountId::parse(from_text.value());
  if (!from) {
    return report(ctx, from.error());
  }
  auto to = AccountId::parse(to_text.value());
  if (!to) {
    return report(ctx, to.error());
  }
  auto amount = parse_amount(args);
  if (!amount) {
    return report(ctx, amount.error());
  }
  auto key = idempotency_key(ctx, args);
  if (!key) {
    return report(ctx, key.error());
  }
  auto reference = JournalReference::parse(args.option("reference").value_or(""));
  if (!reference) {
    return report(ctx, reference.error());
  }
  auto receipt = ctx.service->transfer(TransferRequest{.key = key.value(),
                                                       .source = from.value(),
                                                       .destination = to.value(),
                                                       .amount = amount.value(),
                                                       .reference = reference.value()});
  if (!receipt) {
    return report(ctx, receipt.error());
  }
  return report_receipt(ctx, receipt.value());
}

}  // namespace corebank::cli
