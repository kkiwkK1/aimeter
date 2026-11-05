#include "servers_view.h"

#include "widget/skin.h"

#include <algorithm>
#include <string>

namespace servers_view {

using namespace wg;

namespace {

constexpr int kDetailsHeight = 40;

int Rows(const server::ServerData& data) {
    return (int)std::max<size_t>(1, data.rows.size());
}

std::wstring DisplayPath(std::wstring path) {
    for (wchar_t& c : path) {
        if (c == L'\\') c = L'/';
    }
    // The card is 340px wide: keep the last three components instead of
    // truncating the tail of an absolute path.
    size_t end = path.size();
    for (int i = 0; i < 3 && end > 0; ++i) {
        const size_t slash = path.find_last_of(L'/', end - 1);
        if (slash == std::wstring::npos) break;
        end = slash;
        if (i == 2) return path.substr(end + 1);
    }
    return path;
}

std::wstring CoresLine(const server::ServerData& data) {
    std::wstring text = L"CORES";
    if (data.hosts.empty()) return text + L"  --";
    for (const server::HostStatus& host : data.hosts) {
        text += L"  ";
        text += host.name;
        text += L" ";
        text += host.cores > 0 ? std::to_wstring(host.cores) : std::wstring(L"-");
    }
    return text;
}

} // namespace

int BodyHeight(const server::ServerData& data, bool expanded) {
    return skin::RowsHeight(Rows(data)) +
           (expanded ? skin::kDetailsGap + kDetailsHeight : 0);
}

void DrawBody(const server::ServerData& data, wg::Canvas& canvas, const wg::Rect& body,
              const wg::Theme& theme, bool expanded) {
    skin::Rows(canvas, body, data.rows, theme);
    if (!expanded) return;

    const int y = body.y + skin::RowsHeight(Rows(data)) + skin::kDetailsGap;
    const int left = body.x + skin::kCardPadX;
    const int right = body.right() - skin::kCardPadX;
    skin::Divider(canvas, left, right, y, theme);

    canvas.Text(skin::FitText(canvas, CoresLine(data), right - left, 1.0f), (float)left,
                (float)(y + 9), theme.faint, 1.0f);

    const std::wstring updated = data.updated.empty() ? std::wstring() : L"UPDATED " + data.updated;
    const int reserve = updated.empty() ? 0 : (int)canvas.TextWidth(updated, 1.0f) + 12;
    const int maxLeft = std::max(0, right - left - reserve);

    std::wstring ssh = data.sshFound ? L"SSH  " + DisplayPath(data.sshPath)
                                     : std::wstring(L"SSH CLIENT NOT FOUND");
    ssh = skin::FitText(canvas, ssh, maxLeft, 1.0f);
    canvas.Text(ssh, (float)left, (float)(y + 25), data.sshFound ? theme.faint : theme.bad,
                1.0f);
    if (!updated.empty()) {
        canvas.TextRight(updated, right, (float)(y + 25), theme.faint, 1.0f);
    }
}

wg::StatusChip Status(const server::ServerData& data, const wg::Theme& theme) {
    if (data.hosts.empty()) return wg::StatusChip{L"NO HOSTS", theme.warn};
    if (!data.sshFound) return wg::StatusChip{L"NO SSH", theme.bad};

    int up = 0;
    for (const server::HostStatus& host : data.hosts) {
        if (host.ok) ++up;
    }
    wchar_t text[32];
    swprintf(text, 32, L"%d/%d UP", up, (int)data.hosts.size());
    const wg::Color color = up == (int)data.hosts.size() ? theme.ok
                            : up > 0 ? theme.warn
                                     : theme.bad;
    return wg::StatusChip{text, color};
}

} // namespace servers_view
