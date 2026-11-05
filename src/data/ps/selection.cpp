#include "data/ps/selection.h"

#include <algorithm>

namespace ps {
namespace {

bool Before(const Cell& a, const Cell& b) {
    return a.seq != b.seq ? a.seq < b.seq : a.col < b.col;
}

int ClampCol(int col, std::size_t length) {
    if (col < 0) return 0;
    if ((std::size_t)col > length) return (int)length;
    return col;
}

} // namespace

bool Normalize(const Selection& sel, Cell* from, Cell* to) {
    const Cell& a = sel.anchor;
    const Cell& b = sel.focus;
    const Cell& first = Before(a, b) ? a : b;
    const Cell& last = Before(a, b) ? b : a;
    if (first.seq == last.seq && first.col == last.col) return false;
    if (from) *from = first;
    if (to) *to = last;
    return true;
}

std::wstring ExtractText(const std::vector<Line>& lines, const Selection& sel) {
    Cell from;
    Cell to;
    if (!Normalize(sel, &from, &to)) return std::wstring();

    std::wstring out;
    for (const Line& line : lines) {
        if (line.seq < from.seq || line.seq > to.seq) continue;

        const int begin = line.seq == from.seq ? ClampCol(from.col, line.text.size()) : 0;
        const int end =
            line.seq == to.seq ? ClampCol(to.col, line.text.size()) : (int)line.text.size();
        if (end <= begin) {
            // A fully clamped line still separates paragraphs; keep the break.
            if (!out.empty()) out += L"\r\n";
            continue;
        }
        if (!out.empty()) out += L"\r\n";
        out.append(line.text, (std::size_t)begin, (std::size_t)(end - begin));
    }
    return out;
}

} // namespace ps
