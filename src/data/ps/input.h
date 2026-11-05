// The console's line editor: text, caret, and the up/down history.
//
// Kept separate from the card so it is a plain value type: the module feeds it
// keystrokes, the view asks for the caret pixel, and the tests drive it without
// a window (or a shell).
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ps {

class LineEditor {
public:
    const std::wstring& text() const { return text_; }
    std::size_t caret() const { return caret_; }
    bool empty() const { return text_.empty(); }

    void Clear();
    void Insert(wchar_t c);
    void InsertText(std::wstring_view text);   // paste; newlines become spaces
    bool Backspace();                          // false when nothing was deleted
    bool Delete();                             // delete at the caret
    void Left();
    void Right();
    void Home();
    void End();

    // Caret from a click offset inside the text run, in pixels.
    void SetCaretByX(int x);

    // Hands the text over and clears the editor.
    std::wstring Take();

    // Shell-style history: PushHistory keeps the newest at the end; Up walks
    // back through it and Down walks forward to the (preserved) draft.
    void PushHistory(std::wstring_view line);
    bool HistoryPrev();
    bool HistoryNext();
    const std::vector<std::wstring>& history() const { return history_; }

    std::size_t caretPixelWidth() const;   // caret offset from the text start

private:
    void Recall(std::size_t index);

    std::wstring text_;
    std::size_t caret_ = 0;

    std::vector<std::wstring> history_;
    std::size_t historyPos_ = 0;   // history_.size() == "not browsing"
    std::wstring draft_;
};

} // namespace ps
