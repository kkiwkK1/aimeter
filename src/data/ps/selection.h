// Text selection over the console log.
//
// A selection is anchored to (line seq, column) rather than to a row index, so
// it survives new output arriving and the ring buffer dropping old lines. Pure
// value type: the view hit-tests into it, the card copies out of it.
#pragma once

#include "data/ps/shell.h"

#include <string>
#include <vector>

namespace ps {

struct Cell {
    unsigned long long seq = 0;
    int col = 0;
};

struct Selection {
    bool active = false;
    Cell anchor;
    Cell focus;

    void Clear() {
        active = false;
        anchor = Cell{};
        focus = Cell{};
    }
};

// Orders the two ends. False when nothing is selected (both ends on one cell).
bool Normalize(const Selection& sel, Cell* from, Cell* to);

// Selected text, oldest line first, lines separated by CRLF. Columns are
// clamped to each line; lines that already fell out of the ring are skipped.
std::wstring ExtractText(const std::vector<Line>& lines, const Selection& sel);

} // namespace ps
