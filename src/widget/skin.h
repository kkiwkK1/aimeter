// Visual kit for the desktop board and its cards.
//
// The board owns the outer surface and the card chrome; modules only paint
// their body through the Rows/Details helpers here. Changing a token here
// changes every card, so a new module cannot drift away from the design.
#pragma once

#include "widget/canvas.h"
#include "widget/geom.h"
#include "widget/view.h"

#include <string>
#include <vector>

namespace wg {
namespace skin {

constexpr int kBoardPad = 14;
constexpr int kBoardTitleH = 26;
constexpr int kBoardDot = 16;
constexpr int kBoardResize = 16;

constexpr int kCardHeaderH = 30;
constexpr int kCardPadX = 12;
constexpr int kCardPadTop = 8;
constexpr int kCardPadBottom = 10;
constexpr int kCardGap = 20;
constexpr int kRowPitch = 32;
constexpr int kDetailsGap = 6;

constexpr int kAddW = 340;
constexpr int kAddH = 118;

constexpr float kTitleScale = 1.5f;
constexpr float kValueScale = 1.5f;
constexpr float kMetaScale = 1.0f;
constexpr float kBigScale = 3.0f;

// Card outer height for a module body of bodyH pixels.
int CardHeight(int bodyH);

// The body rectangle a module paints into (already excludes the title bar and
// the card padding).
Rect CardBody(const Rect& card);

// Pixel height of a standard row block.
int RowsHeight(int rows);

// Board surface: tint, 16px dot grid, border, title strip, footer hint and the
// bottom-right resize handle.
void BoardChrome(Canvas& canvas, const Rect& board, const std::wstring& title,
                 const std::wstring& hint, const Theme& theme, bool grid);

// Card frame + title bar. The module paints the body separately.
void CardChrome(Canvas& canvas, const Rect& card, const std::wstring& title,
                const std::wstring& status, Color statusColor, const Theme& theme,
                bool hasDetails, bool expanded, bool hovered, bool dragging);

// Standard label / value / meta-or-bar rows, aligned to the card padding.
void Rows(Canvas& canvas, const Rect& body, const std::vector<Row>& rows,
          const Theme& theme);

void Divider(Canvas& canvas, int x0, int x1, int y, const Theme& theme);

// Dashed "ADD MODULE" tile.
void AddSlot(Canvas& canvas, const Rect& slot, const Theme& theme);

// Pixel-width truncation with a trailing "...".
std::wstring FitText(const Canvas& canvas, std::wstring text, int maxWidth, float scale);

} // namespace skin
} // namespace wg
