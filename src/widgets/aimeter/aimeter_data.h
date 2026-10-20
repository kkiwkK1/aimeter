#pragma once

#include "token_usage.h"
#include "widget/view.h"

#include <string>
#include <vector>

namespace aimeter {

// One immutable snapshot, produced by RefreshData on the worker thread.
struct AimeterData {
    std::vector<wg::Row> rows;

    bool balanceOk = false;
    std::wstring balanceText;      // "CNY 6.75", "NO KEY", "NET ERR", ...
    long long balanceCents = 0;
    long long spendCents = 0;      // account-wide, today

    bool tokensOk = false;         // local DSH logs were readable
    TokenUsage tokens;             // today's local DSH tokens
};

} // namespace aimeter
