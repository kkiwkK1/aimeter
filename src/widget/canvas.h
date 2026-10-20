// wg::Canvas - a tiny pixel canvas for layered desktop widgets.
//
// One DIB section, premultiplied BGRA, a hand-written 5x7 font, no GDI text,
// no Direct2D, no font files. Draw straight into the pixels and hand the whole
// bitmap to UpdateLayeredWindow in a single call.
#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string_view>

namespace wg {

// 0xAARRGGBB, premultiplied (what UpdateLayeredWindow expects).
using Color = uint32_t;

constexpr Color Rgba(uint32_t rgb, int alpha = 255) {
    return ((Color)alpha << 24) |
           ((Color)(((rgb >> 16) & 0xFF) * alpha / 255) << 16) |
           ((Color)(((rgb >> 8) & 0xFF) * alpha / 255) << 8) |
           (Color)((rgb & 0xFF) * alpha / 255);
}

// The default "dark glass" palette used by the bundled widgets.
struct Theme {
    Color panel      = Rgba(0x0E0E12, 200);
    Color panelSolid = Rgba(0x0E0E12, 244);
    Color border     = Rgba(0x2A2A32);
    Color borderEdit = Rgba(0x60A5FA);
    Color text       = Rgba(0xF2F2F6);
    Color label      = Rgba(0xB9B9C4);
    Color faint      = Rgba(0x8A8A96);
    Color track      = Rgba(0x2A2A34);
    Color ok         = Rgba(0x4ADE80);
    Color warn       = Rgba(0xFBBF24);
    Color bad        = Rgba(0xF87171);
};

constexpr float kTextScale = 1.5f;

class Canvas {
public:
    Canvas() = default;
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    // (Re)allocates the bitmap; contents are cleared. Keeps the current size if
    // it already matches, so a widget that repaints often does not churn GDI.
    bool Resize(int w, int h);

    bool ready() const { return px_ != nullptr; }
    int width() const { return w_; }
    int height() const { return h_; }

    void Clear(Color c = 0);

    void Fill(int x, int y, int w, int h, Color c);
    void HLine(int x0, int x1, int y, Color c);
    void Rect(int x, int y, int w, int h, Color c);

    float TextWidth(std::wstring_view t, float scale = kTextScale) const;
    void  Text(std::wstring_view t, float x, float y, Color c, float scale = kTextScale);
    void  TextRight(std::wstring_view t, int right, float y, Color c, float scale = kTextScale);
    void  TextCentered(std::wstring_view t, int centerX, float y, Color c, float scale = kTextScale);

    // Track bar; frac < 0 draws the track only, frac > 1 is clamped.
    void Bar(int x, int y, int w, int h, float frac, Color fill, Color track);

    // The standard 2px-cut rounded panel covering the whole canvas.
    void Panel(const Theme& theme, bool active, bool solid = false);

    // Hands the bitmap to UpdateLayeredWindow at screen position (x, y).
    void Present(HWND hwnd, int x, int y) const;

    // Turns the canvas into an icon (used for the tray).
    HICON ToIcon() const;

    // Raw premultiplied BGRA, top-down. Exposed for tests and screenshots.
    const Color* data() const { return px_; }

private:
    void Release();

    HDC dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HBITMAP prev_ = nullptr;
    Color* px_ = nullptr;
    int w_ = 0, h_ = 0;
};

} // namespace wg
