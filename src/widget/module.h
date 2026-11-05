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
#include <string_view>
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

// One keyboard event, already stripped of the Windows plumbing.
struct KeyEvent {
    unsigned vk = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
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

    // Mouse inside the card body: press, drag with the button held, release.
    // OnClick keeps its "pressed and released without moving" meaning; a drag
    // is reported through OnPress/OnDrag instead (text selection).
    virtual void OnPress(int x, int y) { (void)x; (void)y; }
    virtual void OnDrag(int x, int y) { (void)x; (void)y; }

    // Keyboard. The board focuses a card only after a click whose point the
    // module claimed with WantsKeyboard(), so every other card (and every other
    // pixel of the board) keeps the "a click never steals focus" rule. While a
    // card is focused the board forwards keys, characters, pasted text and the
    // IME composition position to it.
    virtual bool WantsKeyboard(int x, int y) const {
        (void)x;
        (void)y;
        return false;
    }
    virtual void OnFocusChanged(bool focused) { (void)focused; }
    virtual bool OnKey(const KeyEvent& key) {
        (void)key;
        return false;
    }
    virtual void OnChar(wchar_t c) { (void)c; }
    virtual void OnPaste(std::wstring_view text) { (void)text; }
    // Caret in body-local pixels; the board anchors the IME window there.
    virtual bool CaretPos(int* x, int* y) const {
        (void)x;
        (void)y;
        return false;
    }

    // Host plumbing. Bound by the board before Load().
    void Bind(Store* store, std::function<void()> repaint) {
        store_ = store;
        repaint_ = std::move(repaint);
    }
    // Separate setter: the board binds this after the module is in its list, so
    // the callback can address the card by index.
    void BindFocus(std::function<void()> focus) { focus_ = std::move(focus); }
    void RequestFocus() const {
        if (focus_) focus_();
    }
    // Ctrl+V is read by the board; Ctrl+C in a card goes through here, so a
    // module can put a selection on the clipboard without opening it itself.
    void BindClipboard(std::function<void(std::wstring_view)> copy) {
        copy_ = std::move(copy);
    }
    void CopyToClipboard(std::wstring_view text) const {
        if (copy_ && !text.empty()) copy_(text);
    }
    Store& store() { return *store_; }
    bool expanded() const { return expanded_; }
    void SetExpanded(bool on) { expanded_ = on; }
    bool focused() const { return focused_; }
    bool caretOn() const { return caretOn_; }
    void SetFocusState(bool focused, bool caretOn) {
        focused_ = focused;
        caretOn_ = caretOn;
    }
    void RequestRepaint() const {
        if (repaint_) repaint_();
    }

private:
    Store* store_ = nullptr;
    bool expanded_ = false;
    bool focused_ = false;
    bool caretOn_ = false;
    std::function<void()> repaint_;
    std::function<void()> focus_;
    std::function<void(std::wstring_view)> copy_;
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
