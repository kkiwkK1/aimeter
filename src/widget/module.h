// The card contract for the desktop board.
//
// A module is one header's worth of code: a data struct, a blocking Refresh
// that fills it, a BodyHeight and a PaintBody. The board owns the window, the
// board surface, card chrome, dragging, menus, tray, timer and worker threads.
//
// Data flow is the same double buffer as the standalone WidgetT:
//
//   UI thread                                  worker thread
//   ---------                                  -------------
//   Load()   -> LoadData(staging) -> shown
//   (idle)                                     Refresh() -> RefreshData(staging)
//   Commit() <- swap(shown, staging) <-------------- post WM_APP when done
//   PaintBody(shown)
//
// RefreshData() only touches staging, PaintBody() only touches shown, and the
// board never runs them at the same time - so module code needs no locks.
#pragma once

#include "widget/canvas.h"
#include "widget/geom.h"
#include "widget/hotkey.h"
#include "widget/store.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace wg {

// Right-aligned status in a card title bar. color == 0 means theme.faint.
struct StatusChip {
    std::wstring text;
    Color color = 0;
};

// A module-owned action appended to the card menu.
struct MenuItem {
    std::wstring label;
    int id = 0;
    bool checked = false;
};

struct ModuleSpec {
    std::wstring id;          // "account": config prefix, menu key, catalog id
    std::wstring title;       // "ACCOUNT": card title
    std::wstring configFile;  // "aimeter.ini"; empty -> "<id>.ini"
    int width = 340;          // card width; height comes from BodyHeight()
    int refreshSeconds = 0;   // 0 = manual refresh only
    bool hasDetails = false;  // +/- in the title bar, inline details
    bool expandedByDefault = false;
    std::vector<Hotkey> hotkeys;  // local ids 1..99; the board allocates
};

class Module {
public:
    virtual ~Module() = default;

    virtual ModuleSpec Describe() const = 0;  // must not touch store()

    virtual void Load() = 0;      // UI thread, once
    virtual void Refresh() = 0;   // worker thread, may block
    virtual void Commit() = 0;    // UI thread, after the swap

    virtual int BodyHeight() const = 0;  // includes inline details when expanded
    virtual void PaintBody(Canvas& canvas, const Rect& body) = 0;

    virtual StatusChip Status() const { return {}; }
    virtual std::vector<MenuItem> Menu() const { return {}; }
    virtual void OnMenu(int id) { (void)id; }
    virtual bool OnHotkey(int localId) { (void)localId; return false; }
    virtual void OnClick(int x, int y) { (void)x; (void)y; }
    virtual void OnHover(int x, int y) { (void)x; (void)y; }

    // Host plumbing. Bound by the board before Load().
    void Bind(Store* store, std::function<void()> repaint) {
        store_ = store;
        repaint_ = std::move(repaint);
    }
    Store& store() { return *store_; }
    bool expanded() const { return expanded_; }
    void SetExpanded(bool on) { expanded_ = on; }
    void RequestRepaint() const {
        if (repaint_) repaint_();
    }

private:
    Store* store_ = nullptr;
    bool expanded_ = false;
    std::function<void()> repaint_;
};

template <class Data>
class ModuleT : public Module {
public:
    void Load() final {
        LoadData(staging_);
        shown_ = staging_;
    }
    void Refresh() final { RefreshData(staging_); }
    void Commit() final {
        std::swap(shown_, staging_);
        OnCommit();
    }
    int BodyHeight() const final { return BodyHeightOf(shown_); }
    void PaintBody(Canvas& canvas, const Rect& body) final {
        PaintBodyOf(shown_, canvas, body);
    }

protected:
    virtual void LoadData(Data&) {}
    virtual void RefreshData(Data&) = 0;
    virtual int BodyHeightOf(const Data&) const = 0;
    virtual void PaintBodyOf(const Data&, Canvas&, const Rect&) = 0;
    virtual void OnCommit() {}
    const Data& shown() const { return shown_; }

private:
    Data shown_{};
    Data staging_{};
};

} // namespace wg
