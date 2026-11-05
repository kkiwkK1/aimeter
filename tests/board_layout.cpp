// Headless checks for the pure board geometry and clipping.
#include "widget/board_layout.h"
#include "widget/canvas.h"
#include "widget/skin.h"

#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const wchar_t* what) {
    if (!ok) {
        wprintf(L"[board-test] FAIL %ls\n", what);
        ++g_failures;
    }
}

bool Same(const wg::Rect& a, const wg::Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

} // namespace

int wmain() {
    using namespace wg;

    Check(board::Snap(37, 8) == 40, L"snap up");
    Check(board::Snap(3, 8) == 0, L"snap down");
    Check(board::Snap(-9, 8) == -8, L"snap negative");

    const Rect card = board::CardRect(20, 32, 340, 96);
    Check(Same(card, Rect{20, 32, 340, 144}), L"card rect includes chrome");
    Check(skin::CardHeight(96) == 144, L"card height");
    Check(Same(skin::CardBody(card), Rect{20, 70, 340, 96}), L"card body rect");

    const Rect interior{14, 32, 732, 738};
    const Rect clamped = board::ClampCard(700, 700, 340, 144, interior);
    Check(clamped.x == 406 && clamped.y == 626, L"clamp card inside board");
    const Rect pinned = board::ClampCard(-50, -50, 340, 144, interior);
    Check(pinned.x == 14 && pinned.y == 32, L"clamp card top-left");

    const RECT work{0, 0, 1568, 849};
    const Rect boardRect =
        board::ClampBoard(Rect{784, 24, 760, 800}, work, 400, 420);
    Check(Same(boardRect, Rect{784, 24, 760, 800}), L"clamp board in work area");
    const Rect big = board::ClampBoard(Rect{-100, -50, 900, 900}, work, 400, 420);
    Check(Same(big, Rect{0, 0, 900, 849}), L"clamp oversized board");
    const Rect tiny = board::ClampBoard(Rect{10, 10, 20, 20}, work, 400, 420);
    Check(tiny.w == 400 && tiny.h == 420, L"board min size");

    const std::vector<Rect> occupied = {Rect{20, 32, 340, 144}};
    const POINT drop = board::ResolveDrop(20, 32, 340, 144, occupied, interior, 12, 8);
    const Rect dropped{drop.x, drop.y, 340, 144};
    Check(!dropped.intersects(occupied[0], 12), L"drop avoids occupied card");
    Check(dropped.x >= interior.x && dropped.right() <= interior.right(), L"drop inside x");
    Check(drop.x % 8 == 0 && drop.y % 8 == 0, L"drop snapped");

    const Rect full{0, 0, 340, 144};
    const POINT noRoom =
        board::ResolveDrop(0, 0, 340, 144, std::vector<Rect>{full}, full, 12, 8);
    Check(noRoom.x == 0 && noRoom.y == 0, L"no room keeps the drop");

    const std::vector<Rect> cards = {Rect{20, 32, 340, 144}};
    const std::vector<Rect> slots = {Rect{20, 200, 340, 118}};
    Check(board::HitTest(30, 40, 760, 800, cards, slots).kind ==
              board::HitKind::CardTitle,
          L"hit card title");
    Check(board::HitTest(30, 100, 760, 800, cards, slots).kind == board::HitKind::Card,
          L"hit card body");
    Check(board::HitTest(30, 10, 760, 800, cards, slots).kind ==
              board::HitKind::BoardTitle,
          L"hit board title");
    Check(board::HitTest(750, 790, 760, 800, cards, slots).kind ==
              board::HitKind::Resize,
          L"hit resize");
    Check(board::HitTest(30, 210, 760, 800, cards, slots).kind ==
              board::HitKind::AddSlot,
          L"hit add slot");
    Check(board::HitTest(700, 700, 760, 800, cards, slots).kind ==
              board::HitKind::Empty,
          L"hit empty");

    Canvas canvas;
    Check(canvas.Resize(20, 20), L"canvas resize");
    canvas.Clear(0);
    canvas.SetClip(5, 5, 5, 5);
    canvas.Fill(0, 0, 20, 20, Rgba(0xFF0000));
    canvas.ResetClip();
    Check(canvas.data()[4 * 20 + 4] == 0, L"clip blocks outside");
    Check(canvas.data()[5 * 20 + 5] == Rgba(0xFF0000), L"clip allows inside");
    Check(canvas.data()[9 * 20 + 9] == Rgba(0xFF0000), L"clip allows corner");
    Check(canvas.data()[10 * 20 + 10] == 0, L"clip ends");

    Canvas textCanvas;
    textCanvas.Resize(340, 100);
    const std::wstring fitted =
        skin::FitText(textCanvas, L"COMMANDCODE-WITH-A-VERY-LONG-NAME", 100, 1.0f);
    Check(textCanvas.TextWidth(fitted, 1.0f) <= 100.0f, L"fit text width");
    Check(fitted.size() >= 3 && fitted.substr(fitted.size() - 3) == L"...",
          L"fit text ellipsis");

    if (g_failures) {
        wprintf(L"[board-test] %d failure(s)\n", g_failures);
        return 1;
    }
    wprintf(L"[board-test] ok\n");
    return 0;
}
