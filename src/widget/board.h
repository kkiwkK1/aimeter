// wg::RunBoard - a desktop pegboard that hosts movable module cards.
//
// One board window, one DIB, one timer, one tray icon. Modules are rectangles
// inside the board; the board owns card chrome, dragging, snapping, menus,
// persistence and the refresh workers.
#pragma once

#include "widget/module.h"

#include <memory>
#include <vector>

namespace wg {

struct BoardOptions {
    std::wstring id = L"desk";
    std::wstring title = L"Desktop Desk";
    int width = 760;          // first run; later read from desk.ini
    int height = 800;
    int minWidth = 400;
    int minHeight = 420;
    int grid = 8;             // card snap
    int dotGrid = 16;         // visual only
    bool startWithWindows = true;
    UINT refreshHotkeyVk = 'R';
    UINT menuHotkeyVk = 'W';
    UINT detailsHotkeyVk = 'D';
    UINT quitHotkeyVk = 'Q';
};

int RunBoard(std::vector<std::unique_ptr<Module>> modules, BoardOptions options = {});

} // namespace wg
