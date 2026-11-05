#include "account_view.h"

#include "data/aimeter/providers.h"
#include "data/aimeter/token_usage.h"

#include "widget/skin.h"

#include <algorithm>
#include <cwchar>
#include <string>

namespace account_view {

using namespace wg;

namespace {

constexpr int kDetailsHeight = 119;

const wg::Color kInput = wg::Rgba(0x60A5FA);
const wg::Color kWrite = wg::Rgba(0xA78BFA);

int Rows(const aimeter::AimeterData& data) {
    return (int)std::max<size_t>(1, data.rows.size());
}

std::wstring SpendText(long long cents) {
    const wchar_t* sign = cents > 0 ? L"-" : (cents < 0 ? L"+" : L"");
    const long long magnitude = cents < 0 ? -cents : cents;
    return std::wstring(sign) + aimeter::Money(magnitude) + L" CNY";
}

} // namespace

int BodyHeight(const aimeter::AimeterData& data, bool expanded) {
    return skin::RowsHeight(Rows(data)) +
           (expanded ? skin::kDetailsGap + kDetailsHeight : 0);
}

void DrawBody(const aimeter::AimeterData& data, wg::Canvas& canvas, const wg::Rect& body,
              const wg::Theme& theme, bool expanded) {
    skin::Rows(canvas, body, data.rows, theme);
    if (!expanded) return;

    const int y = body.y + skin::RowsHeight(Rows(data)) + skin::kDetailsGap;
    const int left = body.x + skin::kCardPadX;
    const int right = body.right() - skin::kCardPadX;

    skin::Divider(canvas, left, right, y, theme);

    canvas.Text(L"TOKENS TODAY", (float)left, (float)(y + 9), theme.faint, 1.0f);
    canvas.TextRight(L"LOCAL", right, (float)(y + 9), theme.label, 1.0f);

    const unsigned long long total = data.tokens.total();
    if (data.tokensOk) {
        const std::wstring tokenText = aimeter::FormatTokens(total);
        canvas.Text(tokenText, (float)left, (float)(y + 19), theme.text, 3.0f);
        canvas.Text(L"TOK", (float)(left + canvas.TextWidth(tokenText, 3.0f) + 6),
                    (float)(y + 28), theme.faint, 1.0f);
        canvas.TextRight(L"DSH LOG", right, (float)(y + 28), theme.faint, 1.0f);

        const int barY = y + 45;
        const int barW = right - left;
        if (total == 0) {
            canvas.Fill(left, barY, barW, 6, theme.track);
        } else {
            const int w1 = (int)((double)barW * (double)data.tokens.uncachedInput / (double)total);
            const int w2 = (int)((double)barW * (double)data.tokens.cacheRead / (double)total);
            const int w3 = (int)((double)barW * (double)data.tokens.cacheWrite / (double)total);
            const int w4 = barW - w1 - w2 - w3;
            canvas.Fill(left, barY, w1, 6, kInput);
            canvas.Fill(left + w1, barY, w2, 6, theme.ok);
            canvas.Fill(left + w1 + w2, barY, w3, 6, kWrite);
            canvas.Fill(left + w1 + w2 + w3, barY, w4, 6, theme.warn);
        }

        const struct Item {
            const wchar_t* name;
            unsigned long long value;
            wg::Color color;
        } items[4] = {
            {L"INPUT", data.tokens.uncachedInput, kInput},
            {L"CACHE", data.tokens.cacheRead, theme.ok},
            {L"WRITE", data.tokens.cacheWrite, kWrite},
            {L"OUTPUT", data.tokens.output, theme.warn},
        };

        int x = left;
        for (const Item& item : items) {
            canvas.Fill(x, y + 56, 3, 3, item.color);
            canvas.Text(item.name, (float)(x + 6), (float)(y + 55), theme.faint, 1.0f);
            const int nameW = (int)canvas.TextWidth(item.name, 1.0f) + 6;
            const std::wstring value = aimeter::FormatTokens(item.value);
            canvas.Text(value, (float)(x + 6 + nameW + 2), (float)(y + 55), theme.label, 1.0f);
            x += 6 + nameW + 2 + (int)canvas.TextWidth(value, 1.0f) + 14;
        }
    } else {
        canvas.Text(L"N/A", (float)left, (float)(y + 19), theme.text, 3.0f);
        canvas.Text(L"NO DSH SESSION LOG", (float)left, (float)(y + 45), theme.faint, 1.0f);
    }

    skin::Divider(canvas, left, right, y + 71, theme);
    canvas.Text(L"SPEND TODAY", (float)left, (float)(y + 81), theme.faint, 1.0f);
    canvas.TextRight(data.balanceOk ? SpendText(data.spendCents) : L"--", right,
                     (float)(y + 79), theme.text, 1.5f);

    canvas.Text(L"CACHE HIT", (float)left, (float)(y + 97), theme.faint, 1.0f);
    if (data.tokensOk) {
        const unsigned long long prompt =
            data.tokens.uncachedInput + data.tokens.cacheRead + data.tokens.cacheWrite;
        if (prompt > 0) {
            wchar_t pctText[16];
            const double pct = 100.0 * (double)data.tokens.cacheRead / (double)prompt;
            swprintf(pctText, 16, L"%.1f%%", pct);
            canvas.TextRight(pctText, right, (float)(y + 95), theme.label, 1.5f);
        } else {
            canvas.TextRight(L"--", right, (float)(y + 95), theme.label, 1.5f);
        }
    } else {
        canvas.TextRight(L"--", right, (float)(y + 95), theme.label, 1.5f);
    }

    canvas.Text(data.tokensOk ? L"TOKENS  DSH LOG - THIS MACHINE" : L"TOKENS  NO LOCAL SOURCE",
                (float)left, (float)(y + 111), theme.faint, 1.0f);
    canvas.TextRight(L"SPEND  ACCOUNT", right, (float)(y + 111), theme.faint, 1.0f);
}

wg::StatusChip Status(const aimeter::AimeterData& data, const wg::Theme& theme) {
    if (data.balanceText.empty() || data.balanceText == L"...") {
        return wg::StatusChip{L"LOADING", theme.faint};
    }
    if (data.balanceOk) return wg::StatusChip{L"OK", theme.ok};
    if (data.balanceText == L"NO KEY") return wg::StatusChip{L"NO KEY", theme.warn};
    return wg::StatusChip{L"OFFLINE", theme.warn};
}

} // namespace account_view
