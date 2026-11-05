#include "data/ps/text.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace ps {
namespace {

std::wstring Decode(std::string_view bytes, UINT codePage, DWORD flags) {
    if (bytes.empty()) return std::wstring();
    const int want = MultiByteToWideChar(codePage, flags, bytes.data(), (int)bytes.size(),
                                         nullptr, 0);
    if (want <= 0) return std::wstring();
    std::wstring out((std::size_t)want, L'\0');
    const int got = MultiByteToWideChar(codePage, flags, bytes.data(), (int)bytes.size(),
                                        out.data(), want);
    if (got <= 0) return std::wstring();
    out.resize((std::size_t)got);
    return out;
}

// 30..37 / 90..97 -> theme colour. Anything else leaves the colour alone.
wg::Color SgrColor(int code, const wg::Theme& theme) {
    switch (code) {
    case 31:
    case 91:
        return theme.bad;
    case 33:
    case 93:
        return theme.warn;
    case 32:
    case 92:
        return theme.ok;
    case 34:
    case 94:
    case 36:
    case 96:
        return theme.label;
    case 35:
    case 95:
        return theme.borderEdit;
    default:
        return 0;
    }
}

void ApplySgr(const std::wstring& params, const wg::Theme& theme, wg::Color& color) {
    int value = -1;
    bool any = false;
    for (std::size_t i = 0; i <= params.size(); ++i) {
        const wchar_t c = i < params.size() ? params[i] : L';';
        if (c >= L'0' && c <= L'9') {
            value = (value < 0 ? 0 : value) * 10 + (c - L'0');
            any = true;
            continue;
        }
        if (c != L';') continue;
        // An empty parameter means "reset", per ECMA-48.
        const int code = any ? value : 0;
        if (code == 0 || code == 39) color = 0;
        else if (const wg::Color mapped = SgrColor(code, theme)) color = mapped;
        value = -1;
        any = false;
    }
}

} // namespace

std::wstring DecodeLine(std::string_view bytes) {
    // MB_ERR_INVALID_CHARS is what separates real UTF-8 from the GBK bytes the
    // shells emit before (and, for 5.1 stderr, after) the encoding switch.
    std::wstring text = Decode(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!text.empty() || bytes.empty()) return text;
    return Decode(bytes, CP_ACP, 0);
}

std::string EncodeUtf8(std::wstring_view text) {
    if (text.empty()) return std::string();
    const int want = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0,
                                         nullptr, nullptr);
    if (want <= 0) return std::string();
    std::string out((std::size_t)want, '\0');
    const int got = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(),
                                        want, nullptr, nullptr);
    if (got <= 0) return std::string();
    out.resize((std::size_t)got);
    return out;
}

bool StripAnsi(std::wstring& text, const wg::Theme& theme, wg::Color& color) {
    std::wstring out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];

        if (c == 0x1B) {
            if (i + 1 >= text.size()) break;
            const wchar_t next = text[i + 1];
            if (next == L'[') {
                // CSI: parameters up to a final byte in 0x40..0x7E.
                std::size_t j = i + 2;
                std::wstring params;
                while (j < text.size()) {
                    const wchar_t p = text[j];
                    if (p >= 0x40 && p <= 0x7E) break;
                    params.push_back(p);
                    ++j;
                }
                if (j < text.size() && text[j] == L'm') ApplySgr(params, theme, color);
                i = j;
                continue;
            }
            if (next == L']') {
                // OSC: terminate with BEL or ESC \.
                std::size_t j = i + 2;
                while (j < text.size()) {
                    if (text[j] == 0x07) break;
                    if (text[j] == 0x1B && j + 1 < text.size() && text[j + 1] == L'\\') {
                        ++j;
                        break;
                    }
                    ++j;
                }
                i = j;
                continue;
            }
            // Two-byte escapes (ESC ( B, ESC = ...) and lone ESC.
            i += 1;
            continue;
        }

        if (c == L'\t') {
            out.push_back(L' ');
            continue;
        }
        if (c < 0x20) continue;   // stray BEL / backspace / DEL-ish noise
        out.push_back(c);
    }

    text = std::move(out);
    return color != 0;
}

void LineSplitter::Feed(std::string_view bytes, std::vector<std::string>& out) {
    std::size_t i = 0;

    if (skipLf_ && !bytes.empty()) {
        if (bytes[0] == '\n') i = 1;
        skipLf_ = false;
    }

    for (; i < bytes.size(); ++i) {
        const char c = bytes[i];

        if (c == '\r' || c == '\n') {
            if (c == '\r') {
                if (i + 1 < bytes.size()) {
                    if (bytes[i + 1] == '\n') ++i;   // CRLF inside this chunk
                } else {
                    skipLf_ = true;                  // CRLF split across chunks
                }
            }
            out.push_back(pending_);
            pending_.clear();
            dropped_ = false;
            continue;
        }

        if (dropped_ || c == '\0') continue;
        if (pending_.size() >= kMaxLineChars) {
            dropped_ = true;
            continue;
        }
        pending_.push_back(c);
    }
}

} // namespace ps
