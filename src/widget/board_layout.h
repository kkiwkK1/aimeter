// Pure board geometry: grid snap, card rects, overlap resolution and hit
// testing. No HWND, so the headless test can drive every case.
#pragma once

#include "widget/geom.h"
#include "widget/layout.h"

#include <vector>

namespace wg {
namespace board {

int Snap(int value, int grid);

Rect CardRect(int x, int y, int width, int bodyHeight);

// Keeps a card inside the board interior; if the card is larger than the
// interior it is pinned to the top-left of the interior.
Rect ClampCard(int x, int y, int w, int h, const Rect& interior);

// Grid-snaps a drop, and if it overlaps another card (with 'gap') asks
// FindFreeSpot for the nearest free slot. Returns the requested point when no
// slot fits (overlap allowed as a last resort).
POINT ResolveDrop(int x, int y, int w, int h, const std::vector<Rect>& occupied,
                  const Rect& interior, int gap, int grid);

// Board rect after min-size and work-area clamping.
Rect ClampBoard(const Rect& want, const RECT& work, int minWidth, int minHeight);

enum class HitKind { Empty, BoardTitle, Resize, Card, CardTitle, AddSlot };

struct Hit {
    HitKind kind = HitKind::Empty;
    int card = -1;
};

Hit HitTest(int x, int y, int boardWidth, int boardHeight,
            const std::vector<Rect>& cards, const std::vector<Rect>& addSlots);

} // namespace board
} // namespace wg
