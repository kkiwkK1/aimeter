// Console text plumbing: encoding, ANSI escapes and line splitting.
//
// The shell writes bytes, not lines: Windows PowerShell and pwsh both fall back
// to the ANSI code page for piped output (GBK on zh-CN) until the session sets
// [Console]::OutputEncoding, and 5.1 keeps writing its stderr in that code page
// afterwards. So every line is decoded as UTF-8 first and as ACP when that
// fails - one rule that covers both shells and native child processes.
#pragma once

#include "widget/canvas.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ps {

// Longest line kept; a runaway command (a minified JSON dump, say) is cut
// instead of pinning the whole board.
constexpr std::size_t kMaxLineChars = 2000;

// UTF-8 when the bytes are valid UTF-8, ANSI code page otherwise.
std::wstring DecodeLine(std::string_view bytes);

// UTF-8 encode, for the command we send back to the shell.
std::string EncodeUtf8(std::wstring_view text);

// Removes CSI / OSC / single-character escapes from 'text' and maps the basic
// SGR colours onto the theme. Returns true when a colour was applied.
bool StripAnsi(std::wstring& text, const wg::Theme& theme, wg::Color& color);

// Incremental CR / LF splitter: feed it whatever ReadFile returned and get back
// the lines that completed. A trailing partial line waits for the next chunk.
class LineSplitter {
public:
    void Feed(std::string_view bytes, std::vector<std::string>& out);

    // Leftover bytes that have not been terminated yet (used by tests).
    const std::string& pending() const { return pending_; }

private:
    std::string pending_;
    bool dropped_ = false;   // this line already hit kMaxLineChars
    bool skipLf_ = false;    // last chunk ended with \r; drop a leading \n
};

} // namespace ps
