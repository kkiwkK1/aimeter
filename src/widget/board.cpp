#include "widget/board.h"

#include "widget/appenv.h"
#include "widget/board_layout.h"
#include "widget/skin.h"

#include <shellapi.h>
#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace wg {
namespace {

constexpr UINT kMsgModuleDone = WM_APP + 1;
constexpr UINT kMsgTray = WM_APP + 2;
constexpr UINT kMsgDirty = WM_APP + 3;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT_PTR kCaretTimerId = 2;
constexpr UINT kCaretBlinkMs = 530;

constexpr int kHotkeyRefresh = 1;
constexpr int kHotkeyMenu = 2;
constexpr int kHotkeyDetails = 3;
constexpr int kHotkeyQuit = 4;
constexpr int kModuleHotkeyBase = 1000;

constexpr UINT kMenuTidy = 10;
constexpr UINT kMenuRefreshAll = 11;
constexpr UINT kMenuGrid = 12;
constexpr UINT kMenuSolid = 13;
constexpr UINT kMenuAutostart = 14;
constexpr UINT kMenuExit = 15;

constexpr UINT kMenuCardDetails = 20;
constexpr UINT kMenuCardRefresh = 21;
constexpr UINT kMenuCardRemove = 22;

constexpr UINT kMenuModuleBase = 100;
constexpr UINT kMenuAddBase = 500;

constexpr int kClickSlop = 4;
constexpr int kRepaintMs = 16;

const wchar_t* kBoardTitle = L"DESKTOP BOARD";
const wchar_t* kBoardHint = L"RIGHT CLICK FOR MODULES";

} // namespace

class Board {
public:
    int Exec(std::vector<std::unique_ptr<Module>> modules, BoardOptions options);

private:
    struct CardState {
        int x = skin::kBoardPad + 6;
        int y = skin::kBoardTitleH + 6;
        bool visible = true;
        bool expanded = false;
        bool placed = false;
    };

    struct ModuleRuntime {
        std::unique_ptr<Module> module;
        ModuleSpec spec;
        Store store;
        CardState state;
        StatusChip status;
        bool failed = false;
        bool lastOk = true;
        std::thread worker;
        bool busy = false;
        bool again = false;
        ULONGLONG dueAt = 0;
    };

    struct VisibleCard {
        int module = -1;
        Rect rect;
    };

    void Render();
    void RenderThrottled();
    void LayoutCards();
    void UpdateAddSlots();
    Rect Interior() const;

    void ArmTimer();
    void StartRefresh(int index);
    void FinishRefresh(int index);
    void RefreshAll();
    void ToggleDetailsAll();
    void ToggleExpanded(int index);

    void SaveState();
    void StartSaveThread();
    void StopSaveThread();
    void SaveLoop();
    void LoadModuleStates();
    void LoadBoardRect(bool hadState);
    void PlaceMissingCards();
    void MigrateAutostart(bool hadState);
    RECT WorkArea() const;
    RECT MonitorWork() const;
    void ClampToWorkArea(bool persist);

    void PinToDesktop();
    void AddTray();
    void RemoveTray();
    HICON BuildIcon();
    void Shutdown();

    void ShowMenu(bool boardMenu, int cardIndex);
    void AddModule(int index);
    void RemoveModule(int index);
    void Tidy();
    LONG_PTR BeginMenu();
    void EndMenu(LONG_PTR oldExStyle);

    board::Hit HitAt(int x, int y);
    void UpdateCursor(const board::Hit& hit, int x, int y);
    void ResetGesture();
    void CancelGesture();

    // Keyboard focus. The board only ever takes focus for a card that claimed
    // the click with Module::WantsKeyboard(); the window itself stops being
    // WS_EX_NOACTIVATE, but WM_MOUSEACTIVATE still says "don't activate" for
    // every other click, so cards stay click-through as far as focus goes.
    void FocusCard(int index);
    void BlurCard();
    void PasteIntoFocusedCard();
    void SetClipboardText(std::wstring_view text);
    bool FocusedBodyRect(Rect* body) const;
    void ForwardKey(WPARAM vk);
    void ForwardChar(wchar_t c);

    void NotifyDirty() {
        if (hwnd_) PostMessageW(hwnd_, kMsgDirty, 0, 0);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK OnForeground(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG a, LONG b, DWORD c, DWORD d);

    std::vector<std::unique_ptr<ModuleRuntime>> modules_;
    BoardOptions options_;
    Theme theme_;
    std::wstring configDir_;
    std::wstring className_;

    Store boardStore_;
    Canvas canvas_;
    Rect boardRect_;
    bool grid_ = true;
    bool solid_ = false;
    bool quitting_ = false;

    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    bool tray_ = false;
    HICON icon_ = nullptr;
    UINT taskbarCreated_ = 0;
    HWINEVENTHOOK fgHook_ = nullptr;
    HANDLE mutex_ = nullptr;

    std::vector<VisibleCard> visible_;
    std::vector<Rect> addSlots_;

    int hoverCard_ = -1;
    int focusCard_ = -1;
    bool caretOn_ = true;
    std::chrono::steady_clock::time_point lastPaint_{};

    board::HitKind pressKind_ = board::HitKind::Empty;
    int pressCard_ = -1;
    POINT pressScreen_{};
    POINT pressBoard_{};
    int pressCardX_ = 0;
    int pressCardY_ = 0;
    int pressW_ = 0;
    int pressH_ = 0;
    bool pressMoved_ = false;
    bool pressToggle_ = false;
    int dragCard_ = -1;
    bool draggingBoard_ = false;
    bool resizing_ = false;

    HWND previousForeground_ = nullptr;

    std::thread saveThread_;
    std::mutex saveMu_;
    std::condition_variable saveCv_;
    std::map<std::wstring, std::wstring> savePending_;
    bool savePendingSet_ = false;
    bool saveStop_ = false;
};

namespace {
Board* g_board = nullptr;

}

int Board::Exec(std::vector<std::unique_ptr<Module>> modules, BoardOptions options) {
    options_ = std::move(options);

    const std::wstring mutexName = options_.id + L"SingleInstance";
    mutex_ = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (mutex_ && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    configDir_ = appenv::ConfigDir(options_.title);
    const bool hadState = boardStore_.Open(configDir_ + L"\\desk.ini");
    MigrateAutostart(hadState);

    modules_.reserve(modules.size());
    for (auto& module : modules) {
        if (!module) continue;
        auto rt = std::make_unique<ModuleRuntime>();
        rt->module = std::move(module);
        rt->spec = rt->module->Describe();
        if (rt->spec.width < 80) rt->spec.width = 80;

        const std::wstring file =
            rt->spec.configFile.empty() ? rt->spec.id + L".ini" : rt->spec.configFile;
        rt->store.Open(configDir_ + L"\\" + file);
        rt->module->Bind(&rt->store, [this] { NotifyDirty(); });
        const int index = (int)modules_.size();
        rt->module->BindFocus([this, index] { FocusCard(index); });
        rt->module->BindClipboard([this](std::wstring_view text) { SetClipboardText(text); });
        modules_.push_back(std::move(rt));
    }

    if (modules_.empty()) return 0;

    // State first (expanded/visible/position), then Load so the initial card
    // heights are available for the default flow.
    LoadModuleStates();
    for (auto& rt : modules_) rt->module->Load();
    LoadBoardRect(hadState);
    PlaceMissingCards();

    className_ = L"wg." + options_.id;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Board::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = className_.c_str();
    RegisterClassExW(&wc);

    // WS_EX_NOACTIVATE is deliberately absent: a card that owns the keyboard
    // (the console) needs real focus. Every other click answers MA_NOACTIVATE
    // in WM_MOUSEACTIVATE, so the board still never steals focus by itself.
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW;
    hwnd_ = CreateWindowExW(exStyle, className_.c_str(), options_.title.c_str(), WS_POPUP,
                            boardRect_.x, boardRect_.y, boardRect_.w, boardRect_.h, nullptr,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) return 1;

    g_board = this;
    StartSaveThread();

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray();
    PinToDesktop();
    fgHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                              &Board::OnForeground, 0, 0, WINEVENT_OUTOFCONTEXT);

    RegisterHotKey(hwnd_, kHotkeyRefresh, MOD_CONTROL | MOD_ALT, options_.refreshHotkeyVk);
    RegisterHotKey(hwnd_, kHotkeyMenu, MOD_CONTROL | MOD_ALT, options_.menuHotkeyVk);
    RegisterHotKey(hwnd_, kHotkeyDetails, MOD_CONTROL | MOD_ALT, options_.detailsHotkeyVk);
    RegisterHotKey(hwnd_, kHotkeyQuit, MOD_CONTROL | MOD_ALT, options_.quitHotkeyVk);
    for (int i = 0; i < (int)modules_.size(); ++i) {
        for (const Hotkey& hk : modules_[i]->spec.hotkeys) {
            if (hk.vk == 0 || hk.id <= 0 || hk.id >= 100) continue;
            RegisterHotKey(hwnd_, kModuleHotkeyBase + i * 100 + hk.id, hk.mods, hk.vk);
        }
        modules_[i]->dueAt = 0;
    }

    LayoutCards();
    Render();
    ArmTimer();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shutdown();
    return 0;
}

void Board::LoadModuleStates() {
    for (auto& rt : modules_) {
        const std::wstring prefix = L"card." + rt->spec.id + L".";
        const std::wstring kExpanded = prefix + L"expanded";
        const std::wstring kVisible = prefix + L"visible";
        const std::wstring kX = prefix + L"x";
        const std::wstring kY = prefix + L"y";

        if (boardStore_.Has(kExpanded.c_str())) {
            rt->state.expanded = boardStore_.GetInt(kExpanded.c_str(), 0) != 0;
        } else {
            rt->state.expanded = rt->spec.expandedByDefault;
        }
        if (boardStore_.Has(kVisible.c_str())) {
            rt->state.visible = boardStore_.GetInt(kVisible.c_str(), 1) != 0;
        }
        if (boardStore_.Has(kX.c_str()) && boardStore_.Has(kY.c_str())) {
            rt->state.x = (int)boardStore_.GetInt(kX.c_str(), rt->state.x);
            rt->state.y = (int)boardStore_.GetInt(kY.c_str(), rt->state.y);
            rt->state.placed = true;
        }
        rt->module->SetExpanded(rt->state.expanded);
    }
}

void Board::LoadBoardRect(bool hadState) {
    const RECT work = WorkArea();
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;

    const int defW = std::min(options_.width,
                              std::max(options_.minWidth, (int)(workW * 0.55)));
    const int defH = std::min(options_.height,
                              std::max(options_.minHeight, workH - 48));

    Rect want;
    want.x = work.right - defW - 24;
    want.y = work.top + 24;
    want.w = defW;
    want.h = defH;

    if (hadState) {
        want.x = (int)boardStore_.GetInt(L"board.x", want.x);
        want.y = (int)boardStore_.GetInt(L"board.y", want.y);
        want.w = (int)boardStore_.GetInt(L"board.w", want.w);
        want.h = (int)boardStore_.GetInt(L"board.h", want.h);
    }
    boardRect_ = board::ClampBoard(want, work, options_.minWidth, options_.minHeight);
    grid_ = boardStore_.GetInt(L"board.grid", 1) != 0;
    solid_ = boardStore_.GetInt(L"board.solid", 0) != 0;
}

void Board::PlaceMissingCards() {
    // Cards added by a newer build have no saved position yet. Place them in
    // the first free slot instead of the old top-left flow, which used to drop
    // a new card on top of whatever was already there.
    const Rect interior = Interior();
    std::vector<Rect> occupied;
    for (const auto& rt : modules_) {
        if (!rt->state.visible || !rt->state.placed) continue;
        occupied.push_back(board::CardRect(rt->state.x, rt->state.y, rt->spec.width,
                                           rt->module->BodyHeight()));
    }

    for (auto& rt : modules_) {
        if (!rt->state.visible || rt->state.placed) continue;
        const int bodyH = rt->module->BodyHeight();
        const POINT p = board::ResolveDrop(interior.x + 6, interior.y + 6, rt->spec.width,
                                           skin::CardHeight(bodyH), occupied, interior, 12,
                                           options_.grid);
        rt->state.x = p.x;
        rt->state.y = p.y;
        rt->state.placed = true;
        occupied.push_back(board::CardRect(p.x, p.y, rt->spec.width, bodyH));
    }
}

void Board::MigrateAutostart(bool hadState) {
    if (hadState) return;

    bool old = false;
    for (const wchar_t* name : {L"AiMeter", L"Server Status", L"SSH Pad"}) {
        if (!appenv::AutostartCommand(name).empty()) {
            appenv::RemoveAutostart(name);
            old = true;
        }
    }
    if (old || options_.startWithWindows) {
        appenv::SetAutostart(options_.title, true);
    }
}

RECT Board::WorkArea() const {
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    return work;
}

RECT Board::MonitorWork() const {
    RECT work{};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (hwnd_ && GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) {
        work = mi.rcWork;
    } else {
        work = WorkArea();
    }
    return work;
}

Rect Board::Interior() const {
    Rect r;
    r.x = skin::kBoardPad;
    r.y = skin::kBoardTitleH + 6;
    r.w = std::max(1, boardRect_.w - 2 * skin::kBoardPad);
    r.h = std::max(1, boardRect_.h - skin::kBoardTitleH - 6 - skin::kBoardPad -
                          skin::kBoardResize);
    return r;
}

void Board::LayoutCards() {
    visible_.clear();
    const Rect interior = Interior();
    for (int i = 0; i < (int)modules_.size(); ++i) {
        auto& rt = *modules_[i];
        if (!rt.state.visible) continue;
        rt.module->SetExpanded(rt.state.expanded);
        const int bodyH = rt.module->BodyHeight();
        const int h = skin::CardHeight(bodyH);
        const Rect r = board::ClampCard(rt.state.x, rt.state.y, rt.spec.width, h, interior);
        rt.state.x = r.x;
        rt.state.y = r.y;
        visible_.push_back(VisibleCard{i, r});
    }
    UpdateAddSlots();
}

void Board::UpdateAddSlots() {
    addSlots_.clear();
    const Rect interior = Interior();
    if (interior.w < skin::kAddW || interior.h < skin::kAddH) return;

    std::vector<Rect> occupied;
    occupied.reserve(visible_.size() + 2);
    for (const VisibleCard& card : visible_) occupied.push_back(card.rect);

    RECT work{interior.x, interior.y, interior.right(), interior.bottom()};
    for (int i = 0; i < 2; ++i) {
        const POINT p = FindFreeSpot(interior.x + 6, interior.y + 6, skin::kAddW,
                                     skin::kAddH, occupied, work, 12);
        Rect slot{p.x, p.y, skin::kAddW, skin::kAddH};
        if (slot.x < interior.x || slot.y < interior.y || slot.right() > interior.right() ||
            slot.bottom() > interior.bottom()) {
            break;
        }
        bool blocked = false;
        for (const Rect& other : occupied) {
            if (slot.intersects(other, 12)) {
                blocked = true;
                break;
            }
        }
        if (blocked) break;
        addSlots_.push_back(slot);
        occupied.push_back(slot);
    }
}

void Board::Render() {
    if (!hwnd_ || quitting_) return;
    if (canvas_.width() != boardRect_.w || canvas_.height() != boardRect_.h) {
        if (!canvas_.Resize(boardRect_.w, boardRect_.h)) return;
    }

    LayoutCards();
    canvas_.Clear();
    skin::BoardChrome(canvas_, Rect{0, 0, boardRect_.w, boardRect_.h}, kBoardTitle, kBoardHint,
                      theme_, grid_);

    for (const Rect& slot : addSlots_) skin::AddSlot(canvas_, slot, theme_);

    for (const VisibleCard& vc : visible_) {
        auto& rt = *modules_[vc.module];
        StatusChip chip = rt.module->Status();
        if (rt.failed) {
            chip.text = L"ERROR";
            chip.color = theme_.bad;
        }
        skin::CardChrome(canvas_, vc.rect, rt.spec.title, chip.text, chip.color, theme_,
                         rt.spec.hasDetails, rt.state.expanded, hoverCard_ == vc.module,
                         dragCard_ == vc.module);

        const Rect body = skin::CardBody(vc.rect);
        canvas_.SetClip(body.x, body.y, body.w, body.h);
        rt.module->PaintBody(canvas_, body);
        canvas_.ResetClip();
    }

    canvas_.Present(hwnd_, boardRect_.x, boardRect_.y);
}

void Board::RenderThrottled() {
    const auto now = std::chrono::steady_clock::now();
    if (lastPaint_ != std::chrono::steady_clock::time_point{} &&
        now - lastPaint_ < std::chrono::milliseconds(kRepaintMs)) {
        return;
    }
    lastPaint_ = now;
    Render();
}

void Board::ArmTimer() {
    if (!hwnd_ || quitting_) return;
    KillTimer(hwnd_, kTimerId);

    const ULONGLONG now = GetTickCount64();
    ULONGLONG next = 0;
    for (const auto& rt : modules_) {
        if (!rt->state.visible || rt->spec.refreshSeconds <= 0) continue;
        ULONGLONG due = rt->dueAt ? rt->dueAt : now + 50;
        if (due < now) due = now + 50;
        if (!next || due < next) next = due;
    }
    if (!next) return;

    const ULONGLONG delta = next > now ? next - now : 50;
    SetTimer(hwnd_, kTimerId, (UINT)std::min<ULONGLONG>(std::max<ULONGLONG>(delta, 50), 600000),
             nullptr);
}

void Board::StartRefresh(int index) {
    if (index < 0 || index >= (int)modules_.size()) return;
    auto& rt = *modules_[index];

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG interval =
        (ULONGLONG)std::max(0, rt.spec.refreshSeconds) * 1000ULL;
    rt.dueAt = interval ? now + interval : 0;

    if (rt.busy) {
        rt.again = true;
        return;
    }

    rt.busy = true;
    rt.lastOk = true;
    try {
        rt.worker = std::thread([this, index] {
            bool ok = true;
            try {
                modules_[index]->module->Refresh();
            } catch (...) {
                ok = false;
            }
            modules_[index]->lastOk = ok;
            if (hwnd_) PostMessageW(hwnd_, kMsgModuleDone, (WPARAM)index, 0);
        });
    } catch (...) {
        rt.busy = false;
        rt.lastOk = false;
    }
}

void Board::FinishRefresh(int index) {
    if (index < 0 || index >= (int)modules_.size()) return;
    auto& rt = *modules_[index];
    if (!rt.busy) return;

    if (rt.worker.joinable()) rt.worker.join();
    rt.busy = false;
    if (quitting_) return;

    try {
        rt.module->Commit();
    } catch (...) {
        rt.failed = true;
    }
    rt.failed = !rt.lastOk;
    rt.status = rt.module->Status();

    LayoutCards();
    Render();
    ArmTimer();

    if (rt.again) {
        rt.again = false;
        StartRefresh(index);
    }
}

void Board::RefreshAll() {
    for (int i = 0; i < (int)modules_.size(); ++i) {
        if (modules_[i]->state.visible) StartRefresh(i);
    }
    ArmTimer();
}

void Board::ToggleDetailsAll() {
    bool any = false;
    for (auto& rt : modules_) {
        if (!rt->spec.hasDetails) continue;
        rt->state.expanded = !rt->state.expanded;
        rt->module->SetExpanded(rt->state.expanded);
        any = true;
    }
    if (!any) return;
    SaveState();
    LayoutCards();
    Render();
}

void Board::ToggleExpanded(int index) {
    if (index < 0 || index >= (int)modules_.size()) return;
    auto& rt = *modules_[index];
    if (!rt.spec.hasDetails) return;
    rt.state.expanded = !rt.state.expanded;
    rt.module->SetExpanded(rt.state.expanded);
    SaveState();
    LayoutCards();
    Render();
}

void Board::AddModule(int index) {
    if (index < 0 || index >= (int)modules_.size()) return;
    auto& rt = *modules_[index];
    rt.state.visible = true;
    rt.module->SetExpanded(rt.state.expanded);

    const Rect interior = Interior();
    std::vector<Rect> occupied;
    for (const VisibleCard& card : visible_) {
        if (card.module != index) occupied.push_back(card.rect);
    }
    const int h = skin::CardHeight(rt.module->BodyHeight());
    const POINT p = board::ResolveDrop(interior.x + 6, interior.y + 6, rt.spec.width, h,
                                       occupied, interior, 12, options_.grid);
    rt.state.x = p.x;
    rt.state.y = p.y;
    rt.state.placed = true;
    rt.dueAt = 0;

    SaveState();
    LayoutCards();
    Render();
    ArmTimer();
    if (!rt.busy) StartRefresh(index);
}

void Board::RemoveModule(int index) {
    if (index < 0 || index >= (int)modules_.size()) return;
    modules_[index]->state.visible = false;
    SaveState();
    LayoutCards();
    Render();
    ArmTimer();
}

void Board::Tidy() {
    const Rect interior = Interior();
    int maxW = 1;
    for (const auto& rt : modules_) maxW = std::max(maxW, rt->spec.width);

    const int gap = skin::kCardGap;
    const int cols = std::max(1, (interior.w + gap) / (maxW + gap));
    std::vector<int> colX(cols);
    std::vector<int> colY(cols, interior.y + 6);
    for (int c = 0; c < cols; ++c) {
        colX[c] = interior.x + 6 + c * (maxW + gap);
    }

    for (auto& rt : modules_) {
        if (!rt->state.visible) continue;
        const int h = skin::CardHeight(rt->module->BodyHeight());
        int best = -1;
        for (int c = 0; c < cols; ++c) {
            if (colY[c] + h > interior.bottom()) continue;
            if (best < 0 || colY[c] < colY[best]) best = c;
        }
        if (best < 0) {
            best = 0;
            for (int c = 1; c < cols; ++c) {
                if (colY[c] < colY[best]) best = c;
            }
        }
        rt->state.x = colX[best];
        rt->state.y = colY[best];
        colY[best] += h + gap;
    }

    SaveState();
    LayoutCards();
    Render();
}

void Board::SaveState() {
    boardStore_.SetInt(L"board.x", boardRect_.x);
    boardStore_.SetInt(L"board.y", boardRect_.y);
    boardStore_.SetInt(L"board.w", boardRect_.w);
    boardStore_.SetInt(L"board.h", boardRect_.h);
    boardStore_.SetInt(L"board.grid", grid_ ? 1 : 0);
    boardStore_.SetInt(L"board.solid", solid_ ? 1 : 0);

    for (const auto& rt : modules_) {
        const std::wstring prefix = L"card." + rt->spec.id + L".";
        boardStore_.SetInt((prefix + L"x").c_str(), rt->state.x);
        boardStore_.SetInt((prefix + L"y").c_str(), rt->state.y);
        boardStore_.SetInt((prefix + L"visible").c_str(), rt->state.visible ? 1 : 0);
        boardStore_.SetInt((prefix + L"expanded").c_str(), rt->state.expanded ? 1 : 0);
    }

    if (!saveThread_.joinable()) {
        boardStore_.Save();
        return;
    }

    auto snapshot = boardStore_.Snapshot();
    {
        std::lock_guard<std::mutex> lock(saveMu_);
        savePending_ = std::move(snapshot);
        savePendingSet_ = true;
    }
    saveCv_.notify_one();
}

void Board::StartSaveThread() {
    if (saveThread_.joinable()) return;
    try {
        saveThread_ = std::thread([this] { SaveLoop(); });
    } catch (...) {
        saveThread_ = std::thread();
    }
}

void Board::SaveLoop() {
    for (;;) {
        std::map<std::wstring, std::wstring> snapshot;
        {
            std::unique_lock<std::mutex> lock(saveMu_);
            saveCv_.wait(lock, [this] { return saveStop_ || savePendingSet_; });
            if (!savePendingSet_ && saveStop_) break;
            snapshot = std::move(savePending_);
            savePending_.clear();
            savePendingSet_ = false;
        }

        Store store;
        store.Open(boardStore_.path());
        for (const auto& kv : snapshot) store.Set(kv.first.c_str(), kv.second);
        store.Save();
        // Loop again: a newer snapshot may have been queued while writing
        // (including the final snapshot queued by Shutdown).
    }
}

void Board::StopSaveThread() {
    if (!saveThread_.joinable()) {
        SaveState();
        return;
    }

    SaveState();  // queue the final snapshot
    {
        std::lock_guard<std::mutex> lock(saveMu_);
        saveStop_ = true;
    }
    saveCv_.notify_one();
    saveThread_.join();
}

void Board::PinToDesktop() {
    if (!hwnd_) return;
    SetWindowPos(hwnd_, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Board::OnForeground(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG, LONG, DWORD, DWORD) {
    if (g_board && event == EVENT_SYSTEM_FOREGROUND && g_board->hwnd_ && hwnd != g_board->hwnd_) {
        g_board->PinToDesktop();
    }
}

HICON Board::BuildIcon() {
    Canvas icon;
    if (!icon.Resize(16, 16)) return nullptr;
    icon.Clear(0);
    icon.Rect(2, 2, 12, 12, Rgba(0x8A8A96));
    for (int y = 5; y < 12; y += 3) {
        for (int x = 5; x < 13; x += 3) icon.Fill(x, y, 1, 1, Rgba(0x4A4A5C));
    }
    icon.Fill(4, 5, 4, 4, theme_.ok);
    icon.Fill(9, 6, 3, 3, theme_.text);
    return icon.ToIcon();
}

void Board::AddTray() {
    if (!icon_) icon_ = BuildIcon();

    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = kMsgTray;
    nid_.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(nid_.szTip, options_.title.c_str(), 128);
    tray_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

void Board::RemoveTray() {
    if (tray_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        tray_ = false;
    }
}

void Board::Shutdown() {
    quitting_ = true;

    if (hwnd_) {
        KillTimer(hwnd_, kTimerId);
        UnregisterHotKey(hwnd_, kHotkeyRefresh);
        UnregisterHotKey(hwnd_, kHotkeyMenu);
        UnregisterHotKey(hwnd_, kHotkeyDetails);
        UnregisterHotKey(hwnd_, kHotkeyQuit);
        for (int i = 0; i < (int)modules_.size(); ++i) {
            for (const Hotkey& hk : modules_[i]->spec.hotkeys) {
                if (hk.id > 0 && hk.id < 100) {
                    UnregisterHotKey(hwnd_, kModuleHotkeyBase + i * 100 + hk.id);
                }
            }
        }
    }
    RemoveTray();

    if (fgHook_) {
        UnhookWinEvent(fgHook_);
        fgHook_ = nullptr;
    }
    for (auto& rt : modules_) {
        if (rt->worker.joinable()) rt->worker.join();
    }

    StopSaveThread();

    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
    if (mutex_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    g_board = nullptr;
}

void Board::FocusCard(int index) {
    if (index < 0 || index >= (int)modules_.size()) return;
    if (focusCard_ == index) return;
    BlurCard();

    focusCard_ = index;
    caretOn_ = true;
    modules_[index]->module->SetFocusState(true, true);
    modules_[index]->module->OnFocusChanged(true);
    if (hwnd_) SetTimer(hwnd_, kCaretTimerId, kCaretBlinkMs, nullptr);

    // Drop to the desktop layer *first*, then take the keyboard: moving the
    // window afterwards would undo the activation we just asked for.
    if (hwnd_) {
        PinToDesktop();
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    }
    Render();
}

void Board::BlurCard() {
    if (focusCard_ < 0) return;
    const int index = focusCard_;
    focusCard_ = -1;
    if (hwnd_) KillTimer(hwnd_, kCaretTimerId);
    modules_[index]->module->SetFocusState(false, false);
    modules_[index]->module->OnFocusChanged(false);
    Render();
}

bool Board::FocusedBodyRect(Rect* body) const {
    if (focusCard_ < 0) return false;
    for (const VisibleCard& vc : visible_) {
        if (vc.module != focusCard_) continue;
        if (body) *body = skin::CardBody(vc.rect);
        return true;
    }
    return false;
}

void Board::PasteIntoFocusedCard() {
    if (focusCard_ < 0) return;
    if (!OpenClipboard(hwnd_)) return;

    std::wstring text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) {
            text = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    if (text.empty()) return;

    modules_[focusCard_]->module->OnPaste(text);
    Render();
}

void Board::SetClipboardText(std::wstring_view text) {
    if (text.empty()) return;
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();

    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(block)) {
            memcpy(dst, text.data(), text.size() * sizeof(wchar_t));
            static_cast<wchar_t*>(dst)[text.size()] = L'\0';
            GlobalUnlock(block);
            if (!SetClipboardData(CF_UNICODETEXT, block)) GlobalFree(block);
        } else {
            GlobalFree(block);
        }
    }
    CloseClipboard();
}

void Board::ForwardKey(WPARAM vk) {
    if (focusCard_ < 0) return;

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Paste lives here: a module should not have to open the clipboard itself.
    if (ctrl && !alt && vk == 'V') {
        PasteIntoFocusedCard();
        return;
    }

    const KeyEvent key{(unsigned)vk, ctrl, alt, shift};
    if (modules_[focusCard_]->module->OnKey(key)) Render();
}

void Board::ForwardChar(wchar_t c) {
    if (focusCard_ < 0) return;
    if (c < 32 || c == 127) return;
    modules_[focusCard_]->module->OnChar(c);
    Render();
}

board::Hit Board::HitAt(int x, int y) {
    std::vector<Rect> cards;
    cards.reserve(visible_.size());
    for (const VisibleCard& card : visible_) cards.push_back(card.rect);
    return board::HitTest(x, y, boardRect_.w, boardRect_.h, cards, addSlots_);
}

void Board::UpdateCursor(const board::Hit& hit, int x, int y) {
    (void)y;
    LPCWSTR cursor = IDC_ARROW;
    switch (hit.kind) {
    case board::HitKind::Resize:
        cursor = IDC_SIZENWSE;
        break;
    case board::HitKind::BoardTitle:
        cursor = IDC_SIZEALL;
        break;
    case board::HitKind::AddSlot:
        cursor = IDC_HAND;
        break;
    case board::HitKind::Card:
        cursor = IDC_ARROW;
        break;
    case board::HitKind::CardTitle: {
        const auto& card = visible_[hit.card].rect;
        const auto& rt = *modules_[visible_[hit.card].module];
        if (rt.spec.hasDetails && x >= card.right() - 28) {
            cursor = IDC_HAND;
        } else {
            cursor = IDC_SIZEALL;
        }
        break;
    }
    case board::HitKind::Empty:
    default:
        cursor = IDC_ARROW;
        break;
    }
    SetCursor(LoadCursorW(nullptr, cursor));
}

void Board::ResetGesture() {
    pressKind_ = board::HitKind::Empty;
    pressCard_ = -1;
    pressMoved_ = false;
    pressToggle_ = false;
    dragCard_ = -1;
    draggingBoard_ = false;
    resizing_ = false;
}

void Board::CancelGesture() {
    const bool hadDrag = dragCard_ >= 0 || draggingBoard_ || resizing_;
    ResetGesture();
    if (hadDrag) {
        SaveState();
        LayoutCards();
        Render();
    }
}

LONG_PTR Board::BeginMenu() {
    HWND previous = GetForegroundWindow();
    previousForeground_ = previous == hwnd_ ? nullptr : previous;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex & ~WS_EX_NOACTIVATE);
    SetForegroundWindow(hwnd_);
    return ex;
}

void Board::EndMenu(LONG_PTR oldExStyle) {
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, oldExStyle);
    if (previousForeground_ && IsWindow(previousForeground_) && previousForeground_ != hwnd_) {
        SetForegroundWindow(previousForeground_);
    }
    previousForeground_ = nullptr;
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    PinToDesktop();
}

void Board::ShowMenu(bool boardMenu, int cardIndex) {
    HMENU menu = CreatePopupMenu();

    if (boardMenu) {
        HMENU add = CreatePopupMenu();
        int count = 0;
        for (int i = 0; i < (int)modules_.size(); ++i) {
            if (modules_[i]->state.visible) continue;
            AppendMenuW(add, MF_STRING, kMenuAddBase + (UINT)i,
                        modules_[i]->spec.title.c_str());
            ++count;
        }
        if (count == 0) AppendMenuW(add, MF_STRING | MF_GRAYED, 0, L"ALL MODULES ADDED");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)add, L"Add module");

        AppendMenuW(menu, MF_STRING, kMenuTidy, L"Tidy board");
        AppendMenuW(menu, MF_STRING, kMenuRefreshAll, L"Refresh all");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (grid_ ? MF_CHECKED : 0), kMenuGrid, L"Show grid");
        AppendMenuW(menu, MF_STRING | (!solid_ ? MF_CHECKED : 0), kMenuSolid,
                    L"Empty area passes clicks");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu,
                    MF_STRING |
                        (appenv::AutostartEnabled(options_.title) ? MF_CHECKED : 0),
                    kMenuAutostart, L"Start with Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    } else if (cardIndex >= 0 && cardIndex < (int)modules_.size()) {
        auto& rt = *modules_[cardIndex];
        const std::vector<MenuItem> items = rt.module->Menu();
        for (const MenuItem& item : items) {
            AppendMenuW(menu, MF_STRING | (item.checked ? MF_CHECKED : 0),
                        kMenuModuleBase + (UINT)item.id, item.label.c_str());
        }
        if (!items.empty()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (rt.spec.hasDetails) {
            AppendMenuW(menu, MF_STRING | (rt.state.expanded ? MF_CHECKED : 0),
                        kMenuCardDetails, L"Details");
        }
        AppendMenuW(menu, MF_STRING, kMenuCardRefresh, L"Refresh");
        AppendMenuW(menu, MF_STRING, kMenuCardRemove, L"Remove card");
    } else {
        DestroyMenu(menu);
        return;
    }

    POINT pt{};
    GetCursorPos(&pt);
    const LONG_PTR oldEx = BeginMenu();
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_,
                                   nullptr);
    DestroyMenu(menu);
    EndMenu(oldEx);

    if (cmd == 0) return;

    if (boardMenu) {
        if (cmd >= (int)kMenuAddBase &&
            cmd < (int)kMenuAddBase + (int)modules_.size()) {
            AddModule(cmd - (int)kMenuAddBase);
            return;
        }
        switch (cmd) {
        case kMenuTidy:
            Tidy();
            break;
        case kMenuRefreshAll:
            RefreshAll();
            break;
        case kMenuGrid:
            grid_ = !grid_;
            SaveState();
            Render();
            break;
        case kMenuSolid:
            solid_ = !solid_;
            SaveState();
            Render();
            break;
        case kMenuAutostart:
            appenv::SetAutostart(options_.title,
                                 !appenv::AutostartEnabled(options_.title));
            break;
        case kMenuExit:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
        return;
    }

    if (cmd >= (int)kMenuModuleBase) {
        modules_[cardIndex]->module->OnMenu(cmd - (int)kMenuModuleBase);
        PinToDesktop();
        StartRefresh(cardIndex);
        Render();
        return;
    }

    switch (cmd) {
    case kMenuCardDetails:
        ToggleExpanded(cardIndex);
        break;
    case kMenuCardRefresh:
        StartRefresh(cardIndex);
        ArmTimer();
        break;
    case kMenuCardRemove:
        RemoveModule(cardIndex);
        break;
    default:
        break;
    }
}

void Board::ClampToWorkArea(bool persist) {
    if (!hwnd_) return;
    const RECT work = MonitorWork();
    const Rect r = board::ClampBoard(boardRect_, work, options_.minWidth, options_.minHeight);
    if (r.x != boardRect_.x || r.y != boardRect_.y || r.w != boardRect_.w ||
        r.h != boardRect_.h) {
        boardRect_ = r;
        SetWindowPos(hwnd_, nullptr, r.x, r.y, r.w, r.h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutCards();
        Render();
    }
    if (persist) SaveState();
}

LRESULT CALLBACK Board::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Board* host = g_board;
    if (!host) return DefWindowProcW(hwnd, msg, wp, lp);

    if (host->taskbarCreated_ && msg == host->taskbarCreated_) {
        if (host->tray_) Shell_NotifyIconW(NIM_DELETE, &host->nid_);
        host->AddTray();
        return 0;
    }

    switch (msg) {
    case kMsgTray:
        if (LOWORD(lp) == WM_RBUTTONUP) host->ShowMenu(true, -1);
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) host->ToggleDetailsAll();
        return 0;

    case kMsgModuleDone:
        host->FinishRefresh((int)wp);
        return 0;

    case kMsgDirty:
        if (!host->quitting_) host->Render();
        return 0;

    case WM_TIMER:
        if (wp == kTimerId) {
            KillTimer(hwnd, kTimerId);
            const ULONGLONG now = GetTickCount64();
            for (int i = 0; i < (int)host->modules_.size(); ++i) {
                auto& rt = *host->modules_[i];
                if (!rt.state.visible || rt.spec.refreshSeconds <= 0) continue;
                if (rt.dueAt == 0 || rt.dueAt <= now) host->StartRefresh(i);
            }
            host->ArmTimer();
        } else if (wp == kCaretTimerId) {
            host->caretOn_ = !host->caretOn_;
            if (host->focusCard_ >= 0) {
                host->modules_[host->focusCard_]->module->SetFocusState(true, host->caretOn_);
            }
            host->Render();
        }
        return 0;

    case WM_HOTKEY:
        if (wp == (WPARAM)kHotkeyRefresh) {
            host->RefreshAll();
        } else if (wp == (WPARAM)kHotkeyMenu) {
            host->ShowMenu(true, -1);
        } else if (wp == (WPARAM)kHotkeyDetails) {
            host->ToggleDetailsAll();
        } else if (wp == (WPARAM)kHotkeyQuit) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else {
            const int slot = (int)wp - kModuleHotkeyBase;
            if (slot >= 0) {
                const int index = slot / 100;
                const int local = slot % 100;
                if (index >= 0 && index < (int)host->modules_.size()) {
                    host->modules_[index]->module->OnHotkey(local);
                    host->Render();
                }
            }
        }
        return 0;

    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        const board::Hit hit = host->HitAt(pt.x, pt.y);
        if (hit.kind == board::HitKind::Empty && !host->solid_) return HTTRANSPARENT;
        return HTCLIENT;
    }

    case WM_MOUSEACTIVATE: {
        // Only a click the module claims for the keyboard may activate the
        // board; everything else keeps the "clicking a card steals nothing"
        // behaviour it had while the window was WS_EX_NOACTIVATE.
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        const board::Hit hit = host->HitAt(pt.x, pt.y);
        if (hit.kind == board::HitKind::Card && hit.card >= 0) {
            const auto& vc = host->visible_[hit.card];
            const Rect body = skin::CardBody(vc.rect);

            if (host->modules_[vc.module]->module->WantsKeyboard(pt.x - body.x,
                                                                 pt.y - body.y)) {
                return MA_ACTIVATE;
            }
        }
        return MA_NOACTIVATE;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        const board::Hit hit = host->HitAt(x, y);

        if (hit.kind == board::HitKind::Empty) return 0;

        // Focus follows the click: a card that wants the keyboard takes it,
        // anything else drops it.
        if (hit.kind == board::HitKind::Card || hit.kind == board::HitKind::CardTitle) {
            const auto& vc = host->visible_[hit.card];
            bool wants = false;
            if (hit.kind == board::HitKind::Card) {
                const Rect body = skin::CardBody(vc.rect);
                wants = host->modules_[vc.module]->module->WantsKeyboard(x - body.x,
                                                                         y - body.y);
            }
            if (wants) {
                if (host->focusCard_ != vc.module) host->FocusCard(vc.module);
            } else {
                host->BlurCard();
            }
        } else {
            host->BlurCard();
        }

        host->pressKind_ = hit.kind;
        host->pressCard_ = hit.kind == board::HitKind::CardTitle ||
                                   hit.kind == board::HitKind::Card
                               ? hit.card
                               : -1;
        host->pressScreen_ = POINT{x + host->boardRect_.x, y + host->boardRect_.y};
        host->pressMoved_ = false;
        host->pressToggle_ = false;
        host->dragCard_ = -1;
        host->draggingBoard_ = false;
        host->resizing_ = false;

        if (hit.kind == board::HitKind::CardTitle || hit.kind == board::HitKind::Card) {
            const auto& vc = host->visible_[hit.card];
            auto& rt = *host->modules_[vc.module];
            if (hit.kind == board::HitKind::CardTitle) {
                const bool toggleZone =
                    rt.spec.hasDetails && x >= vc.rect.right() - 28;
                if (!toggleZone) {
                    host->dragCard_ = vc.module;
                    host->pressCardX_ = rt.state.x;
                    host->pressCardY_ = rt.state.y;
                } else {
                    host->pressToggle_ = true;
                }
            } else {
                // Card body: let the module start whatever gesture it owns
                // (the console anchors a text selection here).
                const Rect body = skin::CardBody(vc.rect);
                rt.module->OnPress(x - body.x, y - body.y);
            }
        } else if (hit.kind == board::HitKind::BoardTitle) {
            host->draggingBoard_ = true;
            host->pressBoard_ = POINT{host->boardRect_.x, host->boardRect_.y};
        } else if (hit.kind == board::HitKind::Resize) {
            host->resizing_ = true;
            host->pressW_ = host->boardRect_.w;
            host->pressH_ = host->boardRect_.h;
        }

        SetCapture(hwnd);
        return 0;
    }

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);

        if (host->pressKind_ != board::HitKind::Empty && GetCapture() == hwnd) {
            // Use the screen-space delta. For the board itself the window moves
            // under the cursor, so client deltas feed back into the drag and
            // make the board oscillate.
            const int sx = x + host->boardRect_.x;
            const int sy = y + host->boardRect_.y;
            const int dx = sx - host->pressScreen_.x;
            const int dy = sy - host->pressScreen_.y;
            if (!host->pressMoved_ &&
                (dx > kClickSlop || dx < -kClickSlop || dy > kClickSlop ||
                 dy < -kClickSlop)) {
                host->pressMoved_ = true;
            }

            if (host->pressMoved_) {
                if (host->pressKind_ == board::HitKind::Card && host->pressCard_ >= 0) {
                    // A drag inside a card body is the card's business: the
                    // console uses it to extend a text selection.
                    const auto& vc = host->visible_[host->pressCard_];
                    const Rect body = skin::CardBody(vc.rect);
                    host->modules_[vc.module]->module->OnDrag(x - body.x, y - body.y);
                } else if (host->dragCard_ >= 0) {
                    auto& rt = *host->modules_[host->dragCard_];
                    const int nx = board::Snap(host->pressCardX_ + dx, host->options_.grid);
                    const int ny = board::Snap(host->pressCardY_ + dy, host->options_.grid);
                    const Rect r = board::ClampCard(
                        nx, ny, rt.spec.width,
                        skin::CardHeight(rt.module->BodyHeight()), host->Interior());
                    if (r.x != rt.state.x || r.y != rt.state.y) {
                        rt.state.x = r.x;
                        rt.state.y = r.y;
                        host->LayoutCards();
                        host->RenderThrottled();
                    }
                } else if (host->draggingBoard_) {
                    const RECT work = host->MonitorWork();
                    Rect want{host->pressBoard_.x + dx, host->pressBoard_.y + dy,
                              host->boardRect_.w, host->boardRect_.h};
                    const Rect r = board::ClampBoard(want, work, host->options_.minWidth,
                                                     host->options_.minHeight);
                    if (r.x != host->boardRect_.x || r.y != host->boardRect_.y) {
                        host->boardRect_.x = r.x;
                        host->boardRect_.y = r.y;
                        SetWindowPos(hwnd, nullptr, r.x, r.y, 0, 0,
                                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                } else if (host->resizing_) {
                    const RECT work = host->MonitorWork();
                    int nw = std::max(host->options_.minWidth, host->pressW_ + dx);
                    int nh = std::max(host->options_.minHeight, host->pressH_ + dy);
                    if (host->boardRect_.x + nw > work.right)
                        nw = work.right - host->boardRect_.x;
                    if (host->boardRect_.y + nh > work.bottom)
                        nh = work.bottom - host->boardRect_.y;
                    if (nw != host->boardRect_.w || nh != host->boardRect_.h) {
                        host->boardRect_.w = nw;
                        host->boardRect_.h = nh;
                        SetWindowPos(hwnd, nullptr, 0, 0, nw, nh,
                                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                        host->LayoutCards();
                        host->Render();
                    }
                }
            }
            return 0;
        }

        const board::Hit hit = host->HitAt(x, y);
        const int hovered =
            (hit.kind == board::HitKind::Card || hit.kind == board::HitKind::CardTitle) &&
                    hit.card >= 0
                ? host->visible_[hit.card].module
                : -1;
        if (hovered != host->hoverCard_) {
            host->hoverCard_ = hovered;
            host->Render();
        }
        host->UpdateCursor(hit, x, y);
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (host->pressKind_ == board::HitKind::Empty) return 0;

        const board::HitKind kind = host->pressKind_;
        const int card = host->pressCard_;
        const bool moved = host->pressMoved_;
        const bool toggle = host->pressToggle_;
        const int drag = host->dragCard_;
        const bool boardDrag = host->draggingBoard_;
        const bool resize = host->resizing_;
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);

        host->ResetGesture();
        ReleaseCapture();

        if (!moved) {
            if (kind == board::HitKind::CardTitle && toggle && card >= 0) {
                host->ToggleExpanded(host->visible_[card].module);
            } else if (kind == board::HitKind::Card && card >= 0) {
                const auto& vc = host->visible_[card];
                const Rect body = skin::CardBody(vc.rect);
                host->modules_[vc.module]->module->OnClick(x - body.x, y - body.y);
            } else if (kind == board::HitKind::AddSlot) {
                host->ShowMenu(true, -1);
            }
            return 0;
        }

        if (drag >= 0) {
            auto& rt = *host->modules_[drag];
            std::vector<Rect> occupied;
            for (const auto& vc : host->visible_) {
                if (vc.module != drag) occupied.push_back(vc.rect);
            }
            const int h = skin::CardHeight(rt.module->BodyHeight());
            const POINT p = board::ResolveDrop(rt.state.x, rt.state.y, rt.spec.width, h,
                                               occupied, host->Interior(), 12,
                                               host->options_.grid);
            rt.state.x = p.x;
            rt.state.y = p.y;
            host->SaveState();
            host->LayoutCards();
            host->Render();
        } else if (boardDrag || resize) {
            host->ClampToWorkArea(true);
            host->SaveState();
            host->LayoutCards();
            host->Render();
            host->PinToDesktop();
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (host->pressKind_ != board::HitKind::Empty && (HWND)lp != hwnd) {
            host->CancelGesture();
        }
        return 0;

    case WM_RBUTTONUP: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        const board::Hit hit = host->HitAt(x, y);
        if ((hit.kind == board::HitKind::Card || hit.kind == board::HitKind::CardTitle) &&
            hit.card >= 0) {
            host->ShowMenu(false, host->visible_[hit.card].module);
        } else {
            host->ShowMenu(true, -1);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (host->hoverCard_ != -1) {
            host->hoverCard_ = -1;
            host->Render();
        }
        return 0;

    case WM_KILLFOCUS:

        // Focus left the board (another app, a menu): stop owning the keyboard.
        host->BlurCard();
        host->PinToDesktop();
        return 0;

    case WM_ACTIVATE:

        // Activation must not lift the board out of the desktop layer.
        if (LOWORD(wp) != WA_INACTIVE) host->PinToDesktop();
        return 0;


    case WM_KEYDOWN:
        if (host->focusCard_ >= 0) {
            host->ForwardKey(wp);
            return 0;
        }
        break;

    case WM_CHAR:
        if (host->focusCard_ >= 0) {
            host->ForwardChar((wchar_t)wp);
            return 0;
        }
        break;

    case WM_IME_CHAR:
        if (host->focusCard_ >= 0) {
            host->ForwardChar((wchar_t)wp);
            return 0;
        }
        break;

    case WM_IME_STARTCOMPOSITION: {
        if (host->focusCard_ < 0) return 0;
        int cx = 0;
        int cy = 0;
        Rect body;
        if (host->modules_[host->focusCard_]->module->CaretPos(&cx, &cy) &&
            host->FocusedBodyRect(&body)) {
            if (HIMC himc = ImmGetContext(hwnd)) {
                COMPOSITIONFORM cf{};
                cf.dwStyle = CFS_POINT;
                cf.ptCurrentPos.x = body.x + cx;
                cf.ptCurrentPos.y = body.y + cy;
                ImmSetCompositionWindow(himc, &cf);
                ImmReleaseContext(hwnd, himc);
            }
        }
        return 0;
    }

    case WM_DISPLAYCHANGE:
        host->ClampToWorkArea(true);
        return 0;

    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA) host->ClampToWorkArea(true);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        host->quitting_ = true;
        host->RemoveTray();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int RunBoard(std::vector<std::unique_ptr<Module>> modules, BoardOptions options) {
    Board board;
    return board.Exec(std::move(modules), std::move(options));
}

} // namespace wg
