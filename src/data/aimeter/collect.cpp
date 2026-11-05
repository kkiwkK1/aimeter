#include "collect.h"

#include "commandcode.h"
#include "providers.h"
#include "token_usage.h"

#include "widget/secrets.h"

#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

namespace aimeter {
namespace {

constexpr const wchar_t* kCredTarget = L"AiMeter/DeepSeek";

// Updates the daily balance snapshot and returns today's spend in cents.
// The actual ini write happens once, at the end of Refresh.
long long SpendToday(long long cents, wg::Store& store) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t today[16];
    swprintf(today, 16, L"%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);

    std::wstring day = store.Get(L"day");
    long long start = (long long)store.GetInt(L"start", 0);
    long long last = (long long)store.GetInt(L"last", 0);

    // Day rollover: yesterday's closing balance becomes today's opening one,
    // so a machine that was off overnight still reports today's spend.
    if (day != today) {
        day = today;
        start = last > 0 ? last : cents;
    }
    last = cents;
    store.Set(L"day", day);
    store.SetInt(L"start", start);
    store.SetInt(L"last", last);
    return start - cents;
}

std::wstring SpendText(long long spent) {
    const wchar_t* sign = spent > 0 ? L"-" : (spent < 0 ? L"+" : L"");
    const long long magnitude = spent < 0 ? -spent : spent;
    return std::wstring(L"TODAY ") + sign + Money(magnitude);
}

void FormatPercent(const CcWindow& window, wchar_t* text, size_t size, double& maxPct) {
    if (!window.present || window.cap <= 0.0) {
        swprintf(text, size, L"--");
        return;
    }
    const double pct = window.percent();
    if (pct > maxPct) maxPct = pct;
    swprintf(text, size, L"%d%%", (int)std::floor(pct + 1e-9));
}

} // namespace

std::vector<wg::Row> InitialRows() {
    return {
        wg::Row{L"DEEPSEEK", L"...", L"", -1.0f, 0, L"", 0},
        wg::Row{L"CODEX", L"...", L"", -1.0f, 0, L"", 0},
        wg::Row{L"COMMANDCODE", L"...", L"", -1.0f, 0, L"", 0},
    };
}

void Refresh(AimeterData& data, wg::Store& store, const wg::Theme& theme) {
    const std::wstring key = wg::LoadSecret(kCredTarget);

    data.balanceOk = false;
    data.balanceCents = 0;
    data.spendCents = 0;
    data.balanceText.clear();

    if (key.empty()) {
        data.balanceText = L"NO KEY";
    } else {
        const Balance balance = FetchDeepSeek(key);
        data.balanceText = balance.text;
        data.balanceOk = balance.ok;
        if (balance.ok) {
            data.balanceCents = balance.cents;
            data.spendCents = SpendToday(balance.cents, store);
        }
    }

    std::vector<DshSessionUsage> sessions;
    if (ReadDshSessions(sessions)) {
        data.tokensOk = true;
        data.tokens = UpdateTodayTokens(sessions, store, LocalDay());
    } else {
        data.tokensOk = false;
        data.tokens = {};
    }

    if (data.rows.size() != 3) data.rows = InitialRows();
    wg::Row& deepseek = data.rows[0];
    deepseek.value = data.balanceText;
    deepseek.accent = data.balanceOk ? 0 : theme.bad;
    deepseek.frac = -1.0f;
    deepseek.noteLeft = data.balanceOk ? SpendText(data.spendCents) : L"";
    if (data.tokensOk) {
        deepseek.note = FormatTokens(data.tokens.total()) + L" TOK  LOCAL";
        deepseek.noteColor = theme.label;
    } else {
        deepseek.note.clear();
        deepseek.noteColor = 0;
    }

    wg::Row& codex = data.rows[1];
    codex.noteLeft.clear();
    codex.note.clear();
    codex.noteColor = 0;
    codex.accent = 0;

    float primary = 0.0f, secondary = 0.0f;
    if (ReadCodex(primary, secondary)) {
        wchar_t value[64];
        swprintf(value, 64, L"%d%% / %d%%", (int)(primary + 0.5f), (int)(secondary + 0.5f));
        codex.value = value;
        codex.frac = primary / 100.0f;
        codex.accent = primary >= 90 ? theme.bad : (primary >= 70 ? theme.warn : 0);
    } else {
        codex.value = L"NO DATA";
        codex.frac = -1.0f;
    }

    wg::Row& commandcode = data.rows[2];
    commandcode.noteLeft.clear();
    commandcode.note.clear();
    commandcode.noteColor = 0;
    commandcode.valueColor = 0;
    commandcode.accent = 0;

    const std::wstring ccKey = LoadCommandCodeKey();
    const CommandCodeUsage cc = FetchCommandCode(ccKey);
    data.ccOk = cc.ok;
    data.cc = cc;

    if (cc.ok) {
        double maxPct = 0.0;
        wchar_t five[16], weekly[16];
        FormatPercent(cc.fiveHour, five, 16, maxPct);
        FormatPercent(cc.weekly, weekly, 16, maxPct);

        wchar_t value[64];
        swprintf(value, 64, L"5H %ls  W %ls", five, weekly);
        commandcode.value = value;
        commandcode.frac = (float)((maxPct > 100.0 ? 100.0 : maxPct) / 100.0);
        commandcode.accent =
            maxPct >= 90.0 ? theme.bad : (maxPct >= 70.0 ? theme.warn : 0);
        if (cc.fiveHour.exceeded || cc.weekly.exceeded) {
            commandcode.valueColor = theme.bad;
        }
    } else {
        commandcode.value = cc.text;
        commandcode.frac = -1.0f;
    }

    store.Save();
}

} // namespace aimeter
