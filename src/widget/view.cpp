#include "widget/view.h"

namespace wg {

int ListHeight(const ListStyle& style, size_t rows) {
    if (rows == 0) rows = 1;
    const int content = style.TextHeight() + style.barGap + style.barHeight;
    return style.pad * 2 + (int)(rows - 1) * style.rowPitch + content;
}

void DrawList(Canvas& canvas, const std::vector<Row>& rows, const ListStyle& style,
              const Theme& theme, bool editing) {
    const int width = canvas.width();
    canvas.Panel(theme, editing);

    int y = style.pad;
    for (const Row& row : rows) {
        canvas.Text(row.label, (float)style.pad, (float)y, theme.label, style.scale);
        canvas.TextRight(row.value, width - style.pad, (float)y, theme.text, style.scale);

        const int barY = y + style.TextHeight() + style.barGap;
        if (!row.note.empty() || !row.noteLeft.empty()) {
            if (!row.noteLeft.empty()) {
                canvas.Text(row.noteLeft, (float)style.pad, (float)barY, theme.faint, 1.0f);
            }
            if (!row.note.empty()) {
                canvas.TextRight(row.note, width - style.pad, (float)barY,
                                 row.noteColor ? row.noteColor : theme.faint, 1.0f);
            }
        } else {
            canvas.Bar(style.pad, barY, width - style.pad * 2, style.barHeight, row.frac,
                       row.accent ? row.accent : theme.ok, theme.track);
        }
        y += style.rowPitch;
    }
}

} // namespace wg
