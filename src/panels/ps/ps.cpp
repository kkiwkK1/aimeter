// POWERSHELL - a resident shell in a card you type into directly.
//
// Two things make this module different from the others:
//   1. It is push-driven: the session's reader thread calls RequestRepaint()
//      when output lands, so refreshSeconds stays 0 and an idle card costs
//      nothing. The log is owned by the Session (its own mutex) instead of the
//      ModuleT shown/staging pair, because a shell has no "one snapshot per
//      refresh" boundary - it streams.
//   2. It owns the keyboard while focused: click the console body and the board
//      takes focus (only there), then WM_KEYDOWN/WM_CHAR arrive here. See
//      Module::WantsKeyboard.
#include "ps.h"

#include "ps_view.h"

#include "data/ps/input.h"
#include "data/ps/selection.h"
#include "data/ps/shell.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kWidth = 340;
constexpr int kMenuTest = 2;
constexpr int kMenuClear = 3;
constexpr int kMenuRestart = 4;
constexpr int kMenuFocus = 5;

// The "is the whole stack alive" one-liner. Deliberately CIM-free: WMI calls are
// denied in locked-down contexts, while the tick counter and Get-PSDrive work
// everywhere. Sent as a single line, so no continuation prompt is involved.
const wchar_t* kSystemTest =
    L"\"PS   $($PSVersionTable.PSVersion.ToString())\"; "
    L"\"HOST $env:COMPUTERNAME\"; "
    L"\"UP   $([int]([Environment]::TickCount64/3600000))h\"; "
    L"\"C:   $([math]::Round((Get-PSDrive C).Free/1GB,1))GB FREE\"; "
    L"\"DIR  $($PWD.Path)\"";

class PsModule : public wg::Module {
public:
    ~PsModule() override {
        // Readers post repaints; drop the callback before the board can die.
        session_.SetOnOutput(nullptr);
        session_.Stop();
    }

    wg::ModuleSpec Describe() const override {
        wg::ModuleSpec spec;
        spec.id = L"ps";
        spec.title = L"POWERSHELL";
        spec.configFile = L"ps.ini";
        spec.width = kWidth;
        spec.refreshSeconds = 0;   // push-driven, see the file comment
        spec.hasDetails = true;
        return spec;
    }

    void Load() override {
        session_.SetOnOutput([this] { RequestRepaint(); });
        // autostart=0 keeps the ~100 MB child out of memory until the first
        // command; Submit() starts the session on demand in that case.
        if (store().GetInt(L"autostart", 1) != 0) StartShell();
    }

    void Refresh() override {}   // a manual refresh must not kill the session
    void Commit() override {}

    int BodyHeight() const override { return ps_view::BodyHeight(expanded()); }

    void PaintBody(wg::Canvas& canvas, const wg::Rect& body) override {
        const std::size_t rows = (std::size_t)(expanded() ? ps::kExpandedLines
                                                         : ps::kCollapsedLines);
        const std::vector<ps::Line> lines = session_.Tail(rows);
        ps_view::DrawBody(lines, editor_.text(), (int)editor_.caretPixelWidth(), focused(),
                          caretOn(), selection_, canvas, body, theme_, expanded());
    }

    wg::StatusChip Status() const override {
        return ps_view::Status(session_.running(), started_, session_.label(), theme_);
    }

    std::vector<wg::MenuItem> Menu() const override {
        return {
            wg::MenuItem{L"Type here", kMenuFocus},
            wg::MenuItem{L"Run system test", kMenuTest},
            wg::MenuItem{L"Clear log", kMenuClear},
            wg::MenuItem{L"Restart shell", kMenuRestart},
        };
    }

    void OnMenu(int id) override {
        switch (id) {
        case kMenuFocus:
            RequestFocus();
            break;
        case kMenuTest:
            Run(kSystemTest);
            break;
        case kMenuClear:
            session_.Clear();
            break;
        case kMenuRestart:
            StartShell();
            break;
        default:
            break;
        }
        RequestRepaint();
    }

    // ---- keyboard ---------------------------------------------------------

    bool WantsKeyboard(int, int) const override { return true; }

    void OnFocusChanged(bool on) override {
        focused_ = on;
        // Give the shell's own echo a chance to show the caret position: the
        // input row is redrawn anyway, but the hint text changes with focus.
        RequestRepaint();
    }

    void OnChar(wchar_t c) override {
        editor_.Insert(c);
        RequestRepaint();
    }

    void OnPaste(std::wstring_view text) override {
        editor_.InsertText(text);
        RequestRepaint();
    }

    bool OnKey(const wg::KeyEvent& key) override {
        switch (key.vk) {
        case VK_RETURN:
            Submit();
            return true;
        case VK_ESCAPE:
            editor_.Clear();
            RequestRepaint();
            return true;
        case VK_BACK:
            editor_.Backspace();
            RequestRepaint();
            return true;
        case VK_DELETE:
            editor_.Delete();
            RequestRepaint();
            return true;
        case VK_LEFT:
            editor_.Left();
            RequestRepaint();
            return true;
        case VK_RIGHT:
            editor_.Right();
            RequestRepaint();
            return true;
        case VK_HOME:
            editor_.Home();
            RequestRepaint();
            return true;
        case VK_END:
            editor_.End();
            RequestRepaint();
            return true;
        case VK_UP:
            if (editor_.HistoryPrev()) RequestRepaint();
            return true;
        case VK_DOWN:
            if (editor_.HistoryNext()) RequestRepaint();
            return true;
        case 'L':
            if (key.ctrl) {
                session_.Clear();
                selection_.Clear();
                RequestRepaint();
                return true;
            }
            return false;
        case 'C':
            if (key.ctrl) {
                // Ctrl+C copies the selection (the way a terminal does) and
                // falls back to killing the current line when nothing is
                // selected.
                if (!CopySelection()) {
                    editor_.Clear();
                    RequestRepaint();
                }
                return true;
            }
            return false;
        case 'A':
            if (key.ctrl) {
                SelectAll();
                return true;
            }
            return false;
        case VK_TAB:
            return true;      // no focus traversal inside a card
        default:
            return false;
        }
    }

    // ---- mouse: caret and text selection ---------------------------------

    void OnPress(int x, int y) override {
        const wg::Rect body = BodySize();
        const std::vector<ps::Line> lines = VisibleLines();
        const ps_view::Hit hit = ps_view::HitTest(lines, x, y, body, expanded());

        if (hit.inputRow || !hit.ok) {
            // Clicking the input row (or empty space) is caret placement.
            selection_.Clear();
            dragging_ = false;
            const int textX = x - ps_view::TextStartX(body);
            editor_.SetCaretByX(textX < 0 ? 0 : textX);
        } else {
            selection_.active = true;
            selection_.anchor = hit.cell;
            selection_.focus = hit.cell;
            dragging_ = true;
        }
        RequestRepaint();
    }

    void OnDrag(int x, int y) override {
        if (!dragging_) return;
        const ps_view::Hit hit = ps_view::HitTest(VisibleLines(), x, y, BodySize(), expanded());
        if (!hit.ok || hit.inputRow) return;
        selection_.focus = hit.cell;
        RequestRepaint();
    }

    void OnClick(int x, int y) override {
        // A press that never turned into a drag: keep the caret placement that
        // OnPress already did, and drop any leftover selection.
        if (dragging_) {
            dragging_ = false;
            RequestRepaint();
            return;
        }
        const wg::Rect body = BodySize();
        selection_.Clear();
        const int textX = x - ps_view::TextStartX(body);
        editor_.SetCaretByX(textX < 0 ? 0 : textX);
        RequestRepaint();
    }

    bool CaretPos(int* x, int* y) const override {
        return ps_view::CaretPoint((int)editor_.caretPixelWidth(), BodySize(), x, y);
    }

private:
    // The board hands out the body rect only while painting; the click and IME
    // paths need the same geometry, so rebuild it at the origin. ps_view only
    // reads offsets from the body's top-left corner.
    wg::Rect BodySize() const { return wg::Rect{0, 0, kWidth, BodyHeight()}; }

    std::vector<ps::Line> VisibleLines() const {
        const std::size_t rows =
            (std::size_t)(expanded() ? ps::kExpandedLines : ps::kCollapsedLines);
        return session_.Tail(rows);
    }

    bool CopySelection() {
        if (!selection_.active) return false;
        const std::wstring text = ps::ExtractText(session_.Snapshot(), selection_);
        if (text.empty()) return false;
        CopyToClipboard(text);
        RequestRepaint();
        return true;
    }

    void SelectAll() {
        const std::vector<ps::Line> lines = VisibleLines();
        if (lines.empty()) return;
        selection_.active = true;
        selection_.anchor = ps::Cell{lines.front().seq, 0};
        selection_.focus = ps::Cell{lines.back().seq, (int)lines.back().text.size()};
        RequestRepaint();
    }

    void Submit() {
        const std::wstring command = editor_.text();
        if (command.empty()) return;
        // History lives in memory (ps::LineEditor); ps.ini stays the two
        // settings a user actually edits, so nothing is written per command.
        editor_.PushHistory(command);
        editor_.Clear();
        Run(command);
        RequestRepaint();
    }

    void Run(const std::wstring& command) {
        if (!session_.running() && !StartShell()) return;
        session_.WriteLine(command);
    }

    bool StartShell() {
        const bool ok = session_.Start(store(), theme_);
        if (ok) started_ = true;
        RequestRepaint();
        return ok;
    }

    ps::Session session_;
    ps::LineEditor editor_;
    ps::Selection selection_;
    wg::Theme theme_;
    bool started_ = false;   // at least one successful spawn, so EXITED != NO SHELL
    bool focused_ = false;
    bool dragging_ = false;  // mouse is down inside the log, extending a selection
};

} // namespace

std::unique_ptr<wg::Module> CreatePsModule() {
    return std::make_unique<PsModule>();
}
