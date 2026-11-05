// desk.exe - the desktop pegboard.
//
// To add a module: write src/panels/<name>/<name>.cpp with a wg::Module
// subclass and append one factory call below. The board creates the card,
// adds it to the Add Module menu and persists its position automatically.
#include "panels/account/account.h"
#include "panels/ps/ps.h"
#include "panels/servers/servers.h"

#include "widget/board.h"

#include <memory>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::vector<std::unique_ptr<wg::Module>> modules;
    modules.push_back(CreateAccountModule());
    modules.push_back(CreateServersModule());
    modules.push_back(CreatePsModule());

    wg::BoardOptions options;
    return wg::RunBoard(std::move(modules), options);
}
