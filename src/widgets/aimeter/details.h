#pragma once

#include "aimeter_data.h"

#include "widget/canvas.h"
#include "widget/store.h"

#include <windows.h>

// Read-only 400x184 popup: Ctrl+Alt+D or tray -> Details...
class DetailsPanel {
public:
    ~DetailsPanel();

    void Toggle(const aimeter::AimeterData& data, const wg::Theme& theme, wg::Store& store);
    void Refresh(const aimeter::AimeterData& data, const wg::Theme& theme);
    void Hide();
    bool visible() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void Show(const aimeter::AimeterData& data, const wg::Theme& theme, wg::Store& store);
    void Redraw(const aimeter::AimeterData& data, const wg::Theme& theme);
    void Place(wg::Store& store);

    HWND hwnd_ = nullptr;
    wg::Canvas canvas_;
};
