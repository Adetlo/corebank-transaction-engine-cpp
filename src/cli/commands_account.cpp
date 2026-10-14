#include "corebank/cli/app.hpp"

#include <cstddef>
#include <iostream>
#include <string>

namespace corebank::cli {

int cmd_account(Context& ctx, const Args& args) {
  const std::string sub = args.positional(1);
  if (sub != "create" && sub != "show" && sub != "history" && sub != "list") {
    std::cerr << "error: account expects create | show | history | list\n";
    return kUsage;
  }
  if (auto opened = ctx.open_database(); !opened) {
    return report(ctx, opened.error());
  }
  if (sub == "list") {
    auto accounts = ctx.service->list_accounts();
    if (!accounts) {
      return report(ctx, accounts.error());
    }
    ctx.out.accounts(accounts.value());
    return kOk;
  }
  auto id_text = args.required("id");
  if (!id_text) {
    return report(ctx, id_text.error());
  }
  auto id = AccountId::parse(id_text.value());
  if (!id) {
    return report(ctx, id.error());
  }
  if (sub == "create") {
    auto name = args.required("name");
    if (!name) {
      return report(ctx, name.error());
    }
    auto currency_text = args.required("currency");
    if (!currency_text) {
      return report(ctx, currency_text.error());
    }
    auto currency = CurrencyCode::parse(currency_text.value());
    if (!currency) {
      return report(ctx, currency.error());
    }
    auto type = parse_account_type(args.option("type").value_or("CUSTOMER"));
    if (!type) {
      return report(ctx, type.error());
    }
    auto account = ctx.service->create_account(
        CreateAccountRequest{.id = id.value(),
                             .name = name.value(),
                             .type = type.value(),
                             .currency = currency.value(),
                             .allow_overdraft = args.flag("allow-overdraft")});
    if (!account) {
      return report(ctx, account.error());
    }
    ctx.out.account(account.value());
    return kOk;
  }
  if (sub == "show") {
    auto account = ctx.service->get_account(id.value());
    if (!account) {
      return report(ctx, account.error());
    }
    ctx.out.account(account.value());
    return kOk;
  }
  auto limit = args.integer("limit", 50);
  if (!limit) {
    return report(ctx, limit.error());
  }
  auto history = ctx.service->account_history(id.value(), static_cast<std::size_t>(limit.value()));
  if (!history) {
    return report(ctx, history.error());
  }
  ctx.out.postings(history.value());
  return kOk;
}

}  // namespace corebank::cli
