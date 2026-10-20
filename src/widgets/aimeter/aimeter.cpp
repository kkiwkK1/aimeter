// AiMeter on top of the widget framework: DeepSeek balance + local token usage
// + Codex quotas.
#include "aimeter_data.h"
#include "details.h"
#include "providers.h"
#include "token_usage.h"

#include "widget/host.h"
#include "widget/prompt.h"
#include "widget/secrets.h"
#include "widget/view.h"

#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kCredTarget = L"AiMeter/DeepSeek";
constexpr const wchar_t* kCredUser = L"deepseek";

class Aimeter : public wg::WidgetT<aimeter::AimeterData> {
public:
    wg::Desc Describe() override { return wg::Desc{L"aimeter", L"AiMeter", 280}; }

    bool HasSettings() const override { return true; }

    void OnSettings() override {
        wg::PromptSpec spec;
        spec.label = L"DEEPSEEK API KEY";
        spec.mask = true;
        spec.saved = !wg::LoadSecret(kCredTarget).empty();

        std::wstring key;
        if (wg::PromptText(spec, key)) wg::SaveSecret(kCredTarget, kCredUser, key);
    }

    bool HasDetails() const override { return true; }

    void ShowDetails() override { details_.Toggle(shown(), theme_, store()); }

    bool TrayIcon(wg::Canvas& canvas) override {
        const wg::Color bars[3] = {theme_.ok, theme_.warn, theme_.bad};
        const int heights[3] = {5, 9, 13};
        for (int i = 0; i < 3; ++i) {
            canvas.Fill(3 + i * 4, 14 - heights[i], 2, heights[i] + 1, bars[i]);
        }
        return true;
    }

protected:
    void LoadData(aimeter::AimeterData& data) override {
        data.rows = {
            wg::Row{L"DEEPSEEK", L"...", L"", -1.0f, 0, L"", 0},
            wg::Row{L"CODEX", L"...", L"", -1.0f, 0, L"", 0},
        };
    }

    void RefreshData(aimeter::AimeterData& data) override {
        const std::wstring key = wg::LoadSecret(kCredTarget);

        data.balanceOk = false;
        data.balanceCents = 0;
        data.spendCents = 0;
        data.balanceText.clear();

        // Account-wide money. This is the only source of truth for spend.
        if (key.empty()) {
            data.balanceText = L"NO KEY";
        } else {
            const aimeter::Balance balance = aimeter::FetchDeepSeek(key);
            data.balanceText = balance.text;
            data.balanceOk = balance.ok;
            if (balance.ok) {
                data.balanceCents = balance.cents;
                data.spendCents = SpendToday(balance.cents);
            }
        }

        // Machine-local tokens from DSH session logs. Independent of the key:
        // even if the balance call fails we can still show today's local usage.
        std::vector<aimeter::DshSessionUsage> sessions;
        if (aimeter::ReadDshSessions(sessions)) {
            data.tokensOk = true;
            data.tokens = aimeter::UpdateTodayTokens(sessions, store(), aimeter::LocalDay());
        } else {
            data.tokensOk = false;
            data.tokens = {};
        }

        wg::Row& deepseek = data.rows[0];
        deepseek.value = data.balanceText;
        deepseek.accent = data.balanceOk ? 0 : theme_.bad;
        deepseek.frac = -1.0f;
        deepseek.noteLeft = data.balanceOk ? SpendText(data.spendCents) : L"";
        if (data.tokensOk) {
            deepseek.note = aimeter::FormatTokens(data.tokens.total()) + L" TOK  LOCAL";
            deepseek.noteColor = theme_.label;
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
        if (aimeter::ReadCodex(primary, secondary)) {
            wchar_t value[64];
            swprintf(value, 64, L"%d%% / %d%%", (int)(primary + 0.5f), (int)(secondary + 0.5f));
            codex.value = value;
            codex.frac = primary / 100.0f;
            codex.accent = primary >= 90 ? theme_.bad : (primary >= 70 ? theme_.warn : 0);
        } else {
            codex.value = L"NO DATA";
            codex.frac = -1.0f;
        }

        store().Save();
    }

    int HeightOf(const aimeter::AimeterData& data) const override {
        return wg::ListHeight(style_, data.rows.size());
    }

    void PaintData(const aimeter::AimeterData& data, wg::Canvas& canvas) override {
        wg::DrawList(canvas, data.rows, style_, theme_, editing());
    }

    void OnCommit() override {
        if (details_.visible()) details_.Refresh(shown(), theme_);
    }

private:
    // Updates the daily balance snapshot and returns today's spend in cents.
    // The actual ini write happens once, at the end of RefreshData.
    long long SpendToday(long long cents) {
        wg::Store& s = store();

        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t today[16];
        swprintf(today, 16, L"%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);

        std::wstring day = s.Get(L"day");
        long long start = (long long)s.GetInt(L"start", 0);
        long long last = (long long)s.GetInt(L"last", 0);

        // Day rollover: yesterday's closing balance becomes today's opening one,
        // so a machine that was off overnight still reports today's spend.
        if (day != today) {
            day = today;
            start = last > 0 ? last : cents;
        }
        last = cents;
        s.Set(L"day", day);
        s.SetInt(L"start", start);
        s.SetInt(L"last", last);
        return start - cents;
    }

    std::wstring SpendText(long long spent) const {
        const wchar_t* sign = spent > 0 ? L"-" : (spent < 0 ? L"+" : L"");
        const long long magnitude = spent < 0 ? -spent : spent;
        return std::wstring(L"TODAY ") + sign + aimeter::Money(magnitude);
    }

    DetailsPanel details_;
    wg::ListStyle style_;
    wg::Theme theme_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return wg::Run(std::make_unique<Aimeter>());
}
