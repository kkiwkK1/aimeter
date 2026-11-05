// POWERSHELL card: turns the session's tail, the live input line and the text
// selection into pixels. Font-free of state, so the live card and the offline
// render test use the same code.
#pragma once

#include "data/ps/selection.h"
#include "data/ps/shell.h"

#include "widget/canvas.h"
#include "widget/module.h"

#include <string>
#include <string_view>
#include <vector>

namespace ps_view {

// Where a click landed inside the body.
struct Hit {
    bool ok = false;
    bool inputRow = false;   // the editable line at the bottom
    ps::Cell cell;           // text position when ok && !inputRow
};

// Card body height: whole rows of console text plus the frame padding.
int BodyHeight(bool expanded);

// Output lines above, one editable input row at the bottom. 'caretPx' is the
// caret's pixel offset inside the text run (see ps::LineEditor). The selection
// is highlighted behind the text it covers.
void DrawBody(const std::vector<ps::Line>& lines, std::wstring_view input, int caretPx,
              bool focused, bool caretOn, const ps::Selection& selection,
              wg::Canvas& canvas, const wg::Rect& body, const wg::Theme& theme,
              bool expanded);

// Click -> cell. 'x' and 'y' are body-local pixels.
Hit HitTest(const std::vector<ps::Line>& lines, int x, int y, const wg::Rect& body,
            bool expanded);

// Caret position in body-local pixels, for the IME composition window.
bool CaretPoint(int caretPx, const wg::Rect& body, int* x, int* y);

// X of the first character of the input run, relative to the body origin.
int TextStartX(const wg::Rect& body);

// 'running' wins over 'started': a restarted shell is READY again.
wg::StatusChip Status(bool running, bool started, const std::wstring& label,
                      const wg::Theme& theme);

} // namespace ps_view
