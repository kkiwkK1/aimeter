#include "data/ps/input.h"

#include "widget/cjk.h"

#include <algorithm>

namespace ps {
namespace {

constexpr std::size_t kMaxLineChars = 512;

int AdvanceOf(std::wstring_view text) {
    int width = 0;
    for (const wchar_t c : text) width += (int)wg::cjk::Advance(c, 1.0f);
    return width;
}

} // namespace

void LineEditor::Clear() {
    text_.clear();
    caret_ = 0;
    historyPos_ = history_.size();
    draft_.clear();
}

void LineEditor::Insert(wchar_t c) {
    if (c < 32 || c == 127) return;
    if (text_.size() >= kMaxLineChars) return;
    text_.insert(text_.begin() + (std::ptrdiff_t)caret_, c);
    ++caret_;
}

void LineEditor::InsertText(std::wstring_view text) {
    for (const wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L'\t') Insert(L' ');
        else Insert(c);
    }
}

bool LineEditor::Backspace() {
    if (caret_ == 0) return false;
    // Keep surrogate pairs (emoji, rare CJK) whole.
    std::size_t width = 1;
    if (caret_ >= 2 && text_[caret_ - 1] >= 0xDC00 && text_[caret_ - 1] <= 0xDFFF) {
        const wchar_t high = text_[caret_ - 2];
        if (high >= 0xD800 && high <= 0xDBFF) width = 2;
    }
    text_.erase(caret_ - width, width);
    caret_ -= width;
    return true;
}

bool LineEditor::Delete() {
    if (caret_ >= text_.size()) return false;
    std::size_t width = 1;
    if (text_[caret_] >= 0xD800 && text_[caret_] <= 0xDBFF && caret_ + 1 < text_.size()) {
        const wchar_t low = text_[caret_ + 1];
        if (low >= 0xDC00 && low <= 0xDFFF) width = 2;
    }
    text_.erase(caret_, width);
    return true;
}

void LineEditor::Left() {
    if (caret_ > 0) --caret_;
}

void LineEditor::Right() {
    if (caret_ < text_.size()) ++caret_;
}

void LineEditor::Home() { caret_ = 0; }
void LineEditor::End() { caret_ = text_.size(); }

void LineEditor::SetCaretByX(int x) {
    if (x <= 0) {
        caret_ = 0;
        return;
    }
    int width = 0;
    std::size_t pos = 0;
    while (pos < text_.size()) {
        const int advance = (int)wg::cjk::Advance(text_[pos], 1.0f);
        if (width + advance / 2 >= x) break;
        width += advance;
        ++pos;
    }
    caret_ = pos;
}

std::wstring LineEditor::Take() {
    std::wstring out = std::move(text_);
    text_.clear();
    caret_ = 0;
    historyPos_ = history_.size();
    draft_.clear();
    return out;
}

void LineEditor::PushHistory(std::wstring_view line) {
    if (line.empty()) return;
    if (history_.empty() || history_.back() != line) history_.push_back(std::wstring(line));
    constexpr std::size_t kMaxHistory = 100;
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin(), history_.begin() + (history_.size() - kMaxHistory));
    }
    historyPos_ = history_.size();
    draft_.clear();
}

void LineEditor::Recall(std::size_t index) {
    if (index < history_.size()) {
        text_ = history_[index];
    } else {
        text_ = draft_;
    }
    caret_ = text_.size();
}

bool LineEditor::HistoryPrev() {
    if (history_.empty()) return false;
    if (historyPos_ == history_.size()) draft_ = text_;
    if (historyPos_ == 0) return false;
    --historyPos_;
    Recall(historyPos_);
    return true;
}

bool LineEditor::HistoryNext() {
    if (history_.empty()) return false;
    if (historyPos_ >= history_.size()) return false;
    ++historyPos_;
    Recall(historyPos_);
    return true;
}

std::size_t LineEditor::caretPixelWidth() const {
    if (caret_ == 0) return 0;
    return (std::size_t)AdvanceOf(std::wstring_view(text_).substr(0, caret_));
}

} // namespace ps
