// Renders the whole board without a window so the visual language can be
// reviewed in a BMP and a regression is visible on a build machine.
#include "bmp.h"

#include "panels/account/account_view.h"
#include "panels/servers/servers_view.h"

#include "widget/skin.h"

#include "data/aimeter/aimeter_data.h"
#include "data/aimeter/token_usage.h"
#include "data/server/providers.h"
#include "data/server/status_view.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

aimeter::AimeterData MakeAccount() {
    aimeter::AimeterData data;
    data.rows = {
        wg::Row{L"DEEPSEEK", L"CNY 6.75", L"", -1.0f, 0, L"TODAY -0.45", 0},
        wg::Row{L"CODEX", L"6% / 1%", L"", 0.06f, 0, L"", 0},
        wg::Row{L"COMMANDCODE", L"5H 9%  W 100%", L"", 1.0f, 0, L"", 0},
    };
    data.balanceOk = true;
    data.balanceText = L"CNY 6.75";
    data.spendCents = 45;
    data.tokensOk = true;
    data.tokens.uncachedInput = 412000;
    data.tokens.cacheRead = 690000;
    data.tokens.cacheWrite = 88000;
    data.tokens.output = 52000;
    return data;
}

server::ServerData MakeServers() {
    server::ServerData data;
    data.sshFound = true;
    data.sshPath = L"C:\\Windows\\System32\\OpenSSH\\ssh.exe";
    data.updated = L"14:32";

    auto add = [&](const wchar_t* name, bool ok, int cpu, int mem, int disk, double load,
                   int cores, long long up, int rtt, const wchar_t* state) {
        server::HostStatus host;
        host.name = name;
        host.target = name;
        host.ok = ok;
        host.cpuPercent = cpu;
        host.memPercent = mem;
        host.diskPercent = disk;
        host.load1 = load;
        host.cores = cores;
        host.uptimeSec = up;
        host.rttMs = rtt;
        host.state = state;
        data.hosts.push_back(std::move(host));
    };
    add(L"WEB-01", true, 12, 43, 61, 0.42, 8, 1814400, 118, L"UP");
    add(L"DB-01", true, 3, 27, 24, 0.16, 4, 18000, 96, L"UP");
    add(L"CACHE-01", true, 8, 51, 33, 0.31, 4, 1036800, 41, L"UP");
    add(L"10.0.0.9", false, -1, -1, -1, -1.0, 0, -1, 8000, L"TIMEOUT");

    wg::Theme theme;
    data.rows = server::BuildRows(data, theme);
    return data;
}

void DrawCard(wg::Canvas& canvas, const wg::Rect& card, const std::wstring& title,
              const std::wstring& status, wg::Color color, bool expanded,
              const wg::Theme& theme, const std::function<void(const wg::Rect&)>& body) {
    wg::skin::CardChrome(canvas, card, title, status, color, theme, true, expanded, false,
                         false);
    const wg::Rect r = wg::skin::CardBody(card);
    canvas.SetClip(r.x, r.y, r.w, r.h);
    body(r);
    canvas.ResetClip();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* path = argc > 1 ? argv[1] : L"build\\desk-board-preview.bmp";

    wg::Theme theme;
    const aimeter::AimeterData account = MakeAccount();
    const server::ServerData servers = MakeServers();

    const int accountExpanded = wg::skin::CardHeight(
        account_view::BodyHeight(account, true));
    const int serversExpanded = wg::skin::CardHeight(
        servers_view::BodyHeight(servers, true));
    if (accountExpanded != 269) {
        wprintf(L"[desk-test] unexpected account height %d\n", accountExpanded);
        return 1;
    }
    if (serversExpanded != 222) {
        wprintf(L"[desk-test] unexpected servers height %d\n", serversExpanded);
        return 1;
    }

    wg::Canvas canvas;
    if (!canvas.Resize(760, 800)) return 1;
    canvas.Clear(0);
    wg::skin::BoardChrome(canvas, wg::Rect{0, 0, 760, 800}, L"DESKTOP BOARD",
                          L"RIGHT CLICK FOR MODULES", theme, true);

    wg::skin::AddSlot(canvas, wg::Rect{20, 370, 340, 118}, theme);
    wg::skin::AddSlot(canvas, wg::Rect{380, 330, 340, 118}, theme);

    DrawCard(canvas, wg::Rect{20, 32, 340, accountExpanded}, L"ACCOUNT",
             account_view::Status(account, theme).text,
             account_view::Status(account, theme).color, true, theme,
             [&](const wg::Rect& body) {
                 account_view::DrawBody(account, canvas, body, theme, true);
             });

    DrawCard(canvas, wg::Rect{380, 32, 340, serversExpanded}, L"SERVERS",
             servers_view::Status(servers, theme).text,
             servers_view::Status(servers, theme).color, true, theme,
             [&](const wg::Rect& body) {
                 servers_view::DrawBody(servers, canvas, body, theme, true);
             });

    if (!test::WriteBmp(path, canvas, wg::Rgba(0x101820))) {
        wprintf(L"[desk-test] cannot write %ls\n", path);
        return 1;
    }

    wprintf(L"[desk-test] account=%d servers=%d\n", accountExpanded, serversExpanded);
    wprintf(L"[desk-test] wrote %ls (%dx%d)\n", path, canvas.width(), canvas.height());
    return 0;
}
