#include "ps_view.h"

#include "widget/cjk.h"
#include "widget/skin.h"

#include <algorithm>

namespace ps_view {

using namespace wg;

namespace {

constexpr int kTextPadX = 2;
constexpr wchar_t kPrompt[] = L"> ";
constexpr int kPromptWidth = 12;   // two ASCII cells at scale 1.0

// Selection wash: dark enough that the 5x7 glyphs stay readable on top.
const Color kSelectionFill = Rgba(0x1D4ED8, 150);

int Rows(bool expanded) { return expanded ? ps::kExpandedLines : ps::kCollapsedLines; }

Color LineColor(const ps::Line& line, const Theme& theme) {
    if (line.color) return line.color;
    return line.kind == ps::Kind::Err ? theme.bad : theme.text;
}

// Pixel y of the input row inside the body.
int InputRowY(const Rect& body) {
    const Rect inner = skin::ConsoleInner(body);
    return inner.bottom() - skin::kConsolePitch;
}

int InputTextX(const Rect& body) {
    return skin::ConsoleInner(body).x + kTextPadX + kPromptWidth;
}

float AdvanceOf(wchar_t c) { return cjk::Advance(c, skin::kConsoleScale); }

// Char index under a pixel offset inside a line, clamped to [0, size()].
int ColumnAtX(std::wstring_view text, int x) {
    if (x <= 0) return 0;
    int width = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int advance = (int)AdvanceOf(text[i]);
        if (width + advance / 2 >= x) return (int)i;
        width += advance;
    }
    return (int)text.size();
}

int XOfColumn(std::wstring_view text, int col) {
    int width = 0;
    const int upto = std::max(0, std::min(col, (int)text.size()));
    for (int i = 0; i < upto; ++i) width += (int)AdvanceOf(text[i]);
    return width;
}

} // namespace

int BodyHeight(bool expanded) {
    return Rows(expanded) * skin::kConsolePitch + skin::kConsolePad * 2;
}

void DrawBody(const std::vector<ps::Line>& lines, std::wstring_view input, int caretPx,
              bool focused, bool caretOn, const ps::Selection& selection,
              wg::Canvas& canvas, const wg::Rect& body, const wg::Theme& theme,
              bool expanded) {
    skin::ConsoleFrame(canvas, body, theme);
    const Rect inner = skin::ConsoleInner(body);
    if (inner.empty()) return;

    canvas.SetClip(inner.x, inner.y, inner.w, inner.h);

    const int inputY = InputRowY(body);
    const int outputRows = Rows(expanded) - 1;
    const int textX = inner.x + kTextPadX;

    // Selection range, resolved once: every drawn line compares its seq.
    ps::Cell from{};
    ps::Cell to{};
    const bool hasSelection = ps::Normalize(selection, &from, &to);

    if (lines.empty()) {
        canvas.Text(L"STARTING POWERSHELL...", (float)textX, (float)(inner.y + 2),
                    theme.faint, skin::kConsoleScale);
    } else {
        // Bottom-up inside the output area: the newest line sits just above the
        // input row, so the tail stays in view without any scroll state.
        int slot = 0;
        for (std::size_t i = lines.size(); i-- > 0 && slot < outputRows; ++slot) {
            const ps::Line& line = lines[i];
            const int y = inputY - (slot + 1) * skin::kConsolePitch;
            if (y < inner.y) break;
            const int textY = y + (skin::kConsolePitch - 9) / 2;

            if (hasSelection && line.seq >= from.seq && line.seq <= to.seq) {
                const int begin = line.seq == from.seq
                                      ? std::max(0, std::min(from.col, (int)line.text.size()))
                                      : 0;
                const int end = line.seq == to.seq
                                    ? std::max(0, std::min(to.col, (int)line.text.size()))
                                    : (int)line.text.size();
                if (end > begin) {
                    const int x0 = textX + XOfColumn(line.text, begin);
                    const int x1 = textX + XOfColumn(line.text, end);
                    // Half a cell of slack on both sides reads as a continuous
                    // highlight across line breaks.
                    canvas.Fill(x0 - 1, y, x1 - x0 + 2, skin::kConsolePitch, kSelectionFill);
                }
            }

            cjk::Text(canvas, line.text, (float)textX, (float)textY, LineColor(line, theme),
                      skin::kConsoleScale);
        }
    }

    // Input row: dim prompt, then the text the user is typing.
    cjk::Text(canvas, kPrompt, (float)textX, (float)(inputY + (skin::kConsolePitch - 9) / 2),
              focused ? theme.ok : theme.faint, skin::kConsoleScale);

    const int inputX = InputTextX(body);
    if (input.empty()) {
        if (!focused) {
            canvas.Text(L"CLICK TO TYPE", (float)inputX,
                        (float)(inputY + (skin::kConsolePitch - 9) / 2), theme.faint,
                        skin::kConsoleScale);
        }
    } else {
        cjk::Text(canvas, input, (float)inputX,
                  (float)(inputY + (skin::kConsolePitch - 9) / 2), theme.text,
                  skin::kConsoleScale);
    }

    if (focused && caretOn) {
        // Two columns wide: a one-pixel bar disappears against the 5x7 glyphs.
        canvas.Fill(inputX + caretPx, inputY + (skin::kConsolePitch - 9) / 2 - 1, 2, 11,
                    theme.borderEdit);
    }

    canvas.ResetClip();
}

Hit HitTest(const std::vector<ps::Line>& lines, int x, int y, const wg::Rect& body,
            bool expanded) {
    Hit hit;
    const Rect inner = skin::ConsoleInner(body);
    if (inner.empty()) return hit;

    const int inputY = InputRowY(body);
    if (y >= inputY) {
        hit.ok = true;
        hit.inputRow = true;
        return hit;
    }

    const int slot = (inputY - y) / skin::kConsolePitch;
    if (slot < 0 || slot >= Rows(expanded) - 1) return hit;
    if ((std::size_t)slot >= lines.size()) return hit;

    const ps::Line& line = lines[lines.size() - 1 - (std::size_t)slot];
    hit.ok = true;
    hit.cell.seq = line.seq;
    hit.cell.col = ColumnAtX(line.text, x - (inner.x + kTextPadX));
    return hit;
}

bool CaretPoint(int caretPx, const wg::Rect& body, int* x, int* y) {
    if (x) *x = InputTextX(body) + caretPx;
    if (y) *y = InputRowY(body) + (skin::kConsolePitch - 9) / 2;
    return true;
}

int TextStartX(const wg::Rect& body) { return InputTextX(body); }

wg::StatusChip Status(bool running, bool started, const std::wstring& label,
                      const wg::Theme& theme) {
    if (running) {
        return wg::StatusChip{label.empty() ? std::wstring(L"READY") : label, theme.ok};
    }
    if (started) return wg::StatusChip{L"EXITED", theme.bad};
    return wg::StatusChip{L"NO SHELL", theme.bad};
}

} // namespace ps_view
