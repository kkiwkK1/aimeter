#include "widget/board_layout.h"

#include "widget/skin.h"

#include <algorithm>

namespace wg {
namespace board {
namespace {

int Clamp(int v, int lo, int hi) {
    if (hi < lo) return lo;
    return std::max(lo, std::min(v, hi));
}

} // namespace

int Snap(int value, int grid) {
    if (grid <= 1) return value;
    const int half = grid / 2;
    return value >= 0 ? ((value + half) / grid) * grid
                      : -(((-value + half) / grid) * grid);
}

Rect CardRect(int x, int y, int width, int bodyHeight) {
    Rect r;
    r.x = x;
    r.y = y;
    r.w = std::max(1, width);
    r.h = skin::CardHeight(std::max(0, bodyHeight));
    return r;
}

Rect ClampCard(int x, int y, int w, int h, const Rect& interior) {
    Rect r;
    r.w = std::max(1, w);
    r.h = std::max(1, h);
    r.x = r.w >= interior.w ? interior.x : Clamp(x, interior.x, interior.right() - r.w);
    r.y = r.h >= interior.h ? interior.y : Clamp(y, interior.y, interior.bottom() - r.h);
    return r;
}

POINT ResolveDrop(int x, int y, int w, int h, const std::vector<Rect>& occupied,
                  const Rect& interior, int gap, int grid) {
    const Rect want = ClampCard(Snap(x, grid), Snap(y, grid), w, h, interior);

    bool blocked = false;
    for (const Rect& other : occupied) {
        if (want.intersects(other, gap)) {
            blocked = true;
            break;
        }
    }
    if (!blocked) return POINT{want.x, want.y};

    RECT work{interior.x, interior.y, interior.right(), interior.bottom()};
    const POINT spot = FindFreeSpot(want.x, want.y, w, h, occupied, work, gap);
    const Rect fits = ClampCard(Snap(spot.x, grid), Snap(spot.y, grid), w, h, interior);

    for (const Rect& other : occupied) {
        if (fits.intersects(other, gap)) {
            return POINT{want.x, want.y};  // no slot: keep the drop, allow overlap
        }
    }
    return POINT{fits.x, fits.y};
}

Rect ClampBoard(const Rect& want, const RECT& work, int minWidth, int minHeight) {
    const int workW = std::max(1, (int)(work.right - work.left));
    const int workH = std::max(1, (int)(work.bottom - work.top));

    Rect r;
    r.w = std::min(std::max(want.w, minWidth), workW);
    r.h = std::min(std::max(want.h, minHeight), workH);
    r.x = Clamp(want.x, work.left, work.right - r.w);
    r.y = Clamp(want.y, work.top, work.bottom - r.h);
    return r;
}

Hit HitTest(int x, int y, int boardWidth, int boardHeight,
            const std::vector<Rect>& cards, const std::vector<Rect>& addSlots) {
    if (x >= boardWidth - skin::kBoardResize && y >= boardHeight - skin::kBoardResize) {
        return Hit{HitKind::Resize, -1};
    }
    if (y < skin::kBoardTitleH) {
        return Hit{HitKind::BoardTitle, -1};
    }
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (!cards[i].contains(x, y)) continue;
        return Hit{y < cards[i].y + skin::kCardHeaderH ? HitKind::CardTitle : HitKind::Card,
                   (int)i};
    }
    for (const Rect& slot : addSlots) {
        if (slot.contains(x, y)) return Hit{HitKind::AddSlot, -1};
    }
    return Hit{};
}

} // namespace board
} // namespace wg
