# ADR-0002: Money as `int64_t` minor units plus a currency code

**Status:** Accepted (2026-10-14)

## Context

A ledger must add, subtract and compare amounts exactly, reject inputs that cannot be
represented, and never silently wrap. Amounts arrive as decimal text from the CLI and
the wire protocol (`"1250.00"`), and leave as decimal text in receipts, archives and
logs. Different currencies have different numbers of decimal places (two for most,
zero for JPY in the supported table).

## Decision

Represent money as a value object with two fields and no implicit conversions
(`include/corebank/domain/money.hpp`):

```cpp
class Money {
  std::int64_t minor_;
  CurrencyCode currency_;
};
```

- `CurrencyCode` is three upper-case letters plus a minor-unit exponent, parsed from
  an 8-entry `constexpr` table in `src/domain/currency.cpp`. Unknown codes are
  rejected with `INVALID_CURRENCY`.
- `Money::parse` accepts an optional sign, an integer part, and an optional fraction.
  It rejects more decimals than the currency allows, exponent notation, trailing
  characters and anything that overflows 64 bits, returning `INVALID_AMOUNT`.
- `add`, `subtract`, `negated` and `multiply` use `__builtin_add_overflow`,
  `__builtin_sub_overflow` and `__builtin_mul_overflow` and return `OVERFLOW` on
  failure. `negated` guards `INT64_MIN` explicitly.
- Arithmetic and comparison across currencies return `CURRENCY_MISMATCH`.
  `compare` returns `Result<std::strong_ordering>` rather than a plain ordering so
  that the mismatch cannot be ignored.
- `format_amount` works on the unsigned magnitude so that `INT64_MIN` prints
  correctly, and zero-pads the fraction to the currency's exponent.
- No floating-point type appears anywhere in money handling.

The database stores `amount_minor` and `balance_minor` as `INTEGER` with CHECK
constraints (`src/infrastructure/sqlite/schema.cpp`), and the service enforces a
configurable upper bound `max_transaction_minor` before any posting.

## Consequences

Positive:

- Every arithmetic result is either exact or an explicit error. There is no rounding
  mode to document.
- `Money` is trivially copyable and cheap to compare, so it can be used in
  designated-initialised aggregates and `std::optional` without cost.
- Parsing and formatting round-trip for every supported currency, which the tests in
  `tests/unit/money_test.cpp` verify.

Negative:

- Callers must handle a `Result` on every operation, including additions that are
  mathematically safe. This is the price of making overflow impossible to ignore.
- The range is bounded by `int64_t` minor units. The service limit of
  10^12 minor units (default) is well below that, but the type itself cannot
  represent arbitrarily large totals.
- `multiply` takes an integer factor only; there is no fractional interest or FX
  calculation, which is consistent with the engine not implementing those features.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| `double` or `long double` | Binary floating point cannot represent most decimal fractions exactly; sums would drift and comparisons would need tolerances. |
| A decimal library | Adds a dependency for a problem that fixed-point integers already solve for a ledger with per-currency exponents. |
| `__int128` minor units | Not portable across all compilers and ABIs, and the extra range is not needed for a bounded educational engine. |
| Per-currency template types (`Money<GBP>`) | Over-engineering: currency would become a compile-time property, but currency arrives at runtime from user input, forcing a type-erasing layer anyway. |
| Storing amounts as decimal text | Would push arithmetic into SQL or string parsing on every read. |

## References

- `include/corebank/domain/money.hpp`, `src/domain/money.cpp`
- `include/corebank/domain/currency.hpp`, `src/domain/currency.cpp`
- `src/domain/account.cpp` (`balance_after_debit`, `balance_after_credit`)
- `src/infrastructure/sqlite/schema.cpp`
- `tests/unit/money_test.cpp`, `tests/unit/currency_test.cpp`
- [ARCHITECTURE.md](../ARCHITECTURE.md), [LIMITATIONS.md](../LIMITATIONS.md)
