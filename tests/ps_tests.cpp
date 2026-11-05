// Headless checks for the POWERSHELL card's plumbing: byte decoding, ANSI
// stripping, line splitting, the line ring - and one real shell round trip
// (spawn, command, decoded output, kill tree) so the card is not the first
// thing to find out that the pipes do not work.
#include "data/ps/input.h"
#include "data/ps/selection.h"
#include "data/ps/shell.h"
#include "data/ps/text.h"

#include "widget/canvas.h"
#include "widget/store.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const wchar_t* what) {
    if (!ok) {
        wprintf(L"[ps-test] FAIL %ls\n", what);
        ++g_failures;
    }
}

bool WaitFor(const std::function<bool()>& done, int timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeoutMs;
    while (GetTickCount64() < deadline) {
        if (done()) return true;
        Sleep(50);
    }
    return done();
}

bool HasLine(const ps::Session& session, const std::wstring& needle, bool errorsOnly = false) {
    for (const ps::Line& line : session.Tail(ps::kMaxLines)) {
        if (errorsOnly && line.kind != ps::Kind::Err) continue;
        if (line.text.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

void TestDecoding() {
    Check(ps::DecodeLine("ascii") == L"ascii", L"decode ascii");
    Check(ps::DecodeLine("").empty(), L"decode empty");

    // The same two characters as UTF-8 and as CP936.
    const std::string utf8 = "\xE4\xB8\xAD\xE6\x96\x87";
    const std::wstring expected = L"\u4E2D\u6587";
    Check(ps::DecodeLine(utf8) == expected, L"decode utf-8");

    if (GetACP() == 936) {
        const std::string gbk = "\xD6\xD0\xCE\xC4";
        Check(ps::DecodeLine(gbk) == expected, L"decode gbk fallback");
    } else {
        wprintf(L"[ps-test] skip gbk fallback (acp %u)\n", GetACP());
    }

    Check(ps::EncodeUtf8(expected) == utf8, L"encode utf-8");
}

void TestAnsi() {
    wg::Theme theme;

    std::wstring red = L"\x1b[31mERR";
    wg::Color color = 0;
    Check(ps::StripAnsi(red, theme, color) && color == theme.bad, L"ansi red maps to bad");
    Check(red == L"ERR", L"ansi red stripped");

    std::wstring reset = L"\x1b[32mOK\x1b[0m done";
    color = 0;
    ps::StripAnsi(reset, theme, color);
    Check(color == 0, L"ansi reset clears colour");
    Check(reset == L"OK done", L"ansi reset stripped");

    std::wstring osc = L"\x1b]0;window title\x07visible";
    color = 0;
    ps::StripAnsi(osc, theme, color);
    Check(osc == L"visible", L"osc stripped");

    std::wstring clear = L"a\x1b[2Jb\x1b[?25hc";
    ps::StripAnsi(clear, theme, color);
    Check(clear == L"abc", L"csi non-sgr stripped");

    std::wstring tab = L"col1\tcol2";
    ps::StripAnsi(tab, theme, color);
    Check(tab == L"col1 col2", L"tab becomes a space");
}

void TestSplitter() {
    ps::LineSplitter split;
    std::vector<std::string> lines;

    split.Feed("ab", lines);
    Check(lines.empty(), L"splitter waits for a newline");

    split.Feed("c\r\n", lines);
    Check(lines.size() == 1 && lines[0] == "abc", L"splitter crlf");

    lines.clear();
    split.Feed("one\rtwo\n\nthree", lines);
    Check(lines.size() == 3, L"splitter mixed endings");
    Check(lines[0] == "one" && lines[1] == "two" && lines[2] == "", L"splitter line content");
    Check(split.pending() == "three", L"splitter keeps the tail");

    // A \r at the very end of a chunk must not produce a phantom empty line
    // when the \n arrives in the next chunk.
    lines.clear();
    split.Feed("\r", lines);
    split.Feed("\n", lines);
    Check(lines.size() == 1 && lines[0] == "three", L"splitter does not double-split crlf");

    // Runaway lines are cut, not buffered forever.
    lines.clear();
    const std::string longLine(ps::kMaxLineChars + 50, 'x');
    split.Feed(longLine + "\n", lines);
    Check(lines.size() == 1 && lines[0].size() == ps::kMaxLineChars, L"splitter cuts long lines");
}

void TestLineEditor() {
    ps::LineEditor ed;
    Check(ed.empty() && ed.caret() == 0, L"editor starts empty");

    ed.Insert(L'a');
    ed.Insert(L'b');
    ed.Insert(L'c');
    Check(ed.text() == L"abc" && ed.caret() == 3, L"editor inserts at the caret");

    ed.Home();
    ed.Insert(L'X');
    Check(ed.text() == L"Xabc" && ed.caret() == 1, L"editor inserts at home");
    ed.Delete();
    Check(ed.text() == L"Xbc", L"editor delete at the caret");

    ed.End();
    ed.Backspace();
    Check(ed.text() == L"Xb" && ed.caret() == 2, L"editor backspace");

    ed.Left();
    ed.Left();
    ed.Left();
    Check(ed.caret() == 0, L"editor left stops at the start");
    ed.Right();
    ed.Right();
    ed.Right();
    Check(ed.caret() == 2, L"editor right stops at the end");
    Check(!ed.Delete(), L"editor delete at the end is a no-op");

    // A click maps to a character boundary: ASCII cells are 6 px wide.
    ed.SetCaretByX(0);
    Check(ed.caret() == 0, L"editor caret from x=0");
    ed.SetCaretByX(13);
    Check(ed.caret() == 2, L"editor caret from x=13");
    ed.SetCaretByX(1000);
    Check(ed.caret() == ed.text().size(), L"editor caret clamps to the end");

    // Paste is single-line: newlines and tabs become spaces.
    ed.Clear();
    ed.InsertText(L"a\r\nb\tc");
    Check(ed.text() == L"a  b c", L"editor flattens pasted newlines");

    // Caret pixel offset is the sum of advances (6 px per ASCII cell).
    ed.Clear();
    ed.InsertText(L"ab");
    ed.End();
    Check(ed.caretPixelWidth() == 12, L"editor caret pixel width");
    ed.Clear();
    ed.InsertText(L"\u4E2D\u6587");
    ed.End();
    Check(ed.caretPixelWidth() == 28, L"editor caret pixel width with wide glyphs");

    // History: newest first when walking back, draft restored when walking
    // past the newest entry again.
    ed.Clear();
    ed.PushHistory(L"first");
    ed.PushHistory(L"second");
    Check(ed.HistoryPrev() && ed.text() == L"second", L"editor history newest");
    Check(ed.HistoryPrev() && ed.text() == L"first", L"editor history older");
    Check(!ed.HistoryPrev(), L"editor history stops at the oldest");
    Check(ed.HistoryNext() && ed.text() == L"second", L"editor history forward");
    Check(ed.HistoryNext() && ed.text().empty(), L"editor history returns to the draft");

    ed.Clear();
    ed.InsertText(L"draft");
    ed.HistoryPrev();
    ed.HistoryNext();
    Check(ed.text() == L"draft", L"editor keeps the unfinished line");

    ed.Clear();
    ed.InsertText(L"ls");
    Check(ed.Take() == L"ls" && ed.empty() && ed.caret() == 0, L"editor take clears");
}

void TestSelection() {
    auto line = [](unsigned long long seq, const wchar_t* text) {
        ps::Line l;
        l.seq = seq;
        l.text = text;
        return l;
    };
    const std::vector<ps::Line> lines = {
        line(1, L"alpha"),
        line(2, L"bravo"),
        line(3, L"\u4E2D\u6587"),
        line(4, L"delta"),
    };

    ps::Selection sel;
    Check(!ps::Normalize(sel, nullptr, nullptr), L"selection inactive");

    // Top-left to middle of the second line.
    sel.active = true;
    sel.anchor = ps::Cell{1, 2};
    sel.focus = ps::Cell{2, 3};
    ps::Cell from;
    ps::Cell to;
    Check(ps::Normalize(sel, &from, &to), L"selection normalizes");
    Check(from.seq == 1 && from.col == 2 && to.seq == 2 && to.col == 3,
          L"selection keeps its order");
    Check(ps::ExtractText(lines, sel) == L"pha\r\nbra", L"selection text across lines");

    // Reversed drag: the same result.
    sel.anchor = ps::Cell{2, 3};
    sel.focus = ps::Cell{1, 2};
    Check(ps::ExtractText(lines, sel) == L"pha\r\nbra", L"selection text is direction-free");

    // Whole lines and a clamped column past the end.
    sel.anchor = ps::Cell{2, 0};
    sel.focus = ps::Cell{3, 99};
    Check(ps::ExtractText(lines, sel) == L"bravo\r\n\u4E2D\u6587", L"selection clamps columns");

    // A selection that starts on a line which already dropped out of the ring
    // still yields every surviving line it covers.
    sel.anchor = ps::Cell{0, 0};
    sel.focus = ps::Cell{4, 99};
    Check(ps::ExtractText(lines, sel) == L"alpha\r\nbravo\r\n\u4E2D\u6587\r\ndelta",
          L"selection skips missing lines");

    // Degenerate selection.
    sel.anchor = ps::Cell{2, 1};
    sel.focus = ps::Cell{2, 1};
    Check(ps::ExtractText(lines, sel).empty(), L"selection empty when both ends match");
}

void TestSession() {
    wg::Store store;
    wg::Theme theme;

    ps::Session session;
    std::atomic<int> repaints{0};
    session.SetOnOutput([&repaints] { ++repaints; });

    if (!session.Start(store, theme)) {
        const std::wstring why = session.error();
        wprintf(L"[ps-test] SKIP no shell available (%ls)\n",
                why.empty() ? L"unknown" : why.c_str());
        return;
    }

    Check(session.running(), L"session running");
    Check(!session.label().empty(), L"session label");
    wprintf(L"[ps-test] shell: %ls  %ls\n", session.label().c_str(), session.path().c_str());

    Check(session.WriteLine(L"Write-Output \"PS-OK-TEST\""), L"write accepted");
    Check(WaitFor([&] { return HasLine(session, L"PS-OK-TEST"); }, 8000),
          L"stdout round trip");

    // The echo of the command itself proves the input side is displayed too.
    Check(HasLine(session, L"> Write-Output"), L"command echo in the log");

    const std::wstring cjk = L"\u4E2D\u6587-OK";
    session.WriteLine(L"Write-Output \"" + cjk + L"\"");
    Check(WaitFor([&] { return HasLine(session, cjk); }, 8000), L"cjk output decodes");

    session.WriteLine(L"Get-Item C:\\no-such-file-for-ps-test");
    Check(WaitFor([&] { return HasLine(session, L"no-such-file-for-ps-test", true); }, 8000),
          L"stderr reaches the log");

    // The ring keeps the newest kMaxLines lines and the newest line last.
    session.WriteLine(L"1..600 | ForEach-Object { \"L$_\" }");
    Check(WaitFor(
              [&] {
                  const std::vector<ps::Line> tail = session.Tail(ps::kMaxLines + 100);
                  return tail.size() == ps::kMaxLines && !tail.empty() &&
                         tail.back().text == L"L600";
              },
              15000),
          L"ring buffer caps and keeps the tail");

    Check(repaints.load() > 0, L"output requested a repaint");

    session.Stop();
    Check(!session.running(), L"session stopped");
    Check(!session.WriteLine(L"Write-Output \"after-stop\""), L"write refused after stop");
    wprintf(L"[ps-test] repaints=%d\n", repaints.load());
}

} // namespace

int wmain() {
    TestDecoding();
    TestAnsi();
    TestSplitter();
    TestLineEditor();
    TestSelection();
    TestSession();

    if (g_failures != 0) {
        wprintf(L"[ps-test] %d failure(s)\n", g_failures);
        return 1;
    }
    wprintf(L"[ps-test] ok\n");
    return 0;
}
