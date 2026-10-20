// The widget contract.
//
// A widget is one header's worth of code: a data struct, a blocking Refresh
// that fills it, and a Paint that draws it. The host owns the window, tray,
// hotkeys, timers, persistence and threading.
//
// Data flow (this is the whole concurrency model):
//
//   UI thread                                  worker thread
//   ---------                                  -------------
//   Load()   -> LoadData(staging) -> shown
//   (idle)                                     Refresh() -> RefreshData(staging)
//   Commit() <- swap(shown, staging) <-------------- post WM_APP when done
//   Paint(shown)
//
// RefreshData() only touches staging, PaintData() only touches shown, and the
// host never runs them at the same time - so a slow HTTP call never blocks the
// message loop and widget code needs no locks.
#pragma once

#include "widget/canvas.h"
#include "widget/store.h"

#include <memory>
#include <string>
#include <utility>

namespace wg {

class Host;

struct Desc {
    std::wstring id;          // stable: window class, mutex, <id>.ini, credential prefix
    std::wstring title;       // tray tooltip + autostart registry value
    int width = 280;          // pixels; height comes from Widget::Height()
};

class Widget {
public:
    virtual ~Widget() = default;

    virtual Desc Describe() = 0;

    // Optional: paint a 16x16 tray icon. Return false to keep the default glyph.
    virtual bool TrayIcon(Canvas&) { return false; }

    // Optional: show "Settings..." in the tray/context menu.
    virtual bool HasSettings() const { return false; }
    virtual void OnSettings() {}

    // Optional: a read-only details panel, reachable from the tray menu and
    // Options::detailsHotkey (default Ctrl+Alt+D).
    virtual bool HasDetails() const { return false; }
    virtual void ShowDetails() {}

    bool editing() const { return editing_; }
    Store& store() { return *store_; }

    // Host plumbing; WidgetT implements all of it.
    virtual void Load() = 0;
    virtual void Refresh() = 0;
    virtual void Commit() = 0;
    virtual int  Height() const = 0;
    virtual void Paint(Canvas&) = 0;

private:
    friend class Host;
    Store* store_ = nullptr;
    bool editing_ = false;
};

template <class Data>
class WidgetT : public Widget {
public:
    void Load() final {
        LoadData(staging_);
        shown_ = staging_;
    }
    void Refresh() final { RefreshData(staging_); }
    void Commit() final { std::swap(shown_, staging_); OnCommit(); }
    int  Height() const final { return HeightOf(shown_); }
    void Paint(Canvas& canvas) final { PaintData(shown_, canvas); }

protected:
    virtual void LoadData(Data&) {}              // UI thread, once
    virtual void RefreshData(Data&) = 0;         // worker thread, may block
    virtual int  HeightOf(const Data&) const = 0;  // UI thread
    virtual void PaintData(const Data&, Canvas&) = 0;  // UI thread
    virtual void OnCommit() {}                   // UI thread, after the swap
    const Data& shown() const { return shown_; }

private:
    Data shown_{};
    Data staging_{};
};

} // namespace wg
