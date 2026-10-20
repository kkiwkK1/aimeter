// Headless check: renders the standard list layout without a window and writes
// a BMP, so layout/font regressions are visible in CI or on a build machine.
#include "widget/view.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

bool WriteBmp(const wchar_t* path, const wg::Canvas& canvas, wg::Color background) {
    const int w = canvas.width();
    const int h = canvas.height();
    const wg::Color* src = canvas.data();
    if (!src) return false;

    const DWORD pixelBytes = (DWORD)w * h * 4;
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + pixelBytes;

    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;            // bottom-up
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = pixelBytes;

    std::vector<wg::Color> out((size_t)w * h);

    const int bgA = (int)(background >> 24);
    const int bgR = (int)((background >> 16) & 0xFF);
    const int bgG = (int)((background >> 8) & 0xFF);
    const int bgB = (int)(background & 0xFF);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const wg::Color s = src[(size_t)y * w + x];
            const int a = (int)(s >> 24);
            const int r = (int)((s >> 16) & 0xFF) + bgR * (255 - a) / 255;
            const int g = (int)((s >> 8) & 0xFF) + bgG * (255 - a) / 255;
            const int b = (int)(s & 0xFF) + bgB * (255 - a) / 255;
            (void)bgA;

            const int dy = h - 1 - y;
            out[(size_t)dy * w + x] = ((wg::Color)r << 16) | ((wg::Color)g << 8) |
                                      (wg::Color)b | 0xFF000000u;
        }
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = WriteFile(file, &fh, sizeof(fh), &written, nullptr) &&
              WriteFile(file, &ih, sizeof(ih), &written, nullptr) &&
              WriteFile(file, out.data(), pixelBytes, &written, nullptr);
    CloseHandle(file);
    return ok;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* path = argc > 1 ? argv[1] : L"build\\preview.bmp";

    wg::Theme theme;
    wg::ListStyle style;
    const std::vector<wg::Row> rows = {
        wg::Row{L"DEEPSEEK", L"CNY 6.75", L"1.24M TOK  LOCAL", -1.0f, 0,
                L"TODAY -0.45", theme.label},
        wg::Row{L"CODEX", L"6% / 1%", L"", 0.06f, 0, L"", 0},
    };

    const int height = wg::ListHeight(style, rows.size());
    if (height != 71) {
        wprintf(L"[test] unexpected height: %d\n", height);
        return 1;
    }

    wg::Canvas canvas;
    if (!canvas.Resize(280, height)) return 1;
    canvas.Clear();
    wg::DrawList(canvas, rows, style, theme, false);

    if (!WriteBmp(path, canvas, wg::Rgba(0x20242C))) {
        wprintf(L"[test] cannot write %ls\n", path);
        return 1;
    }
    wprintf(L"[test] wrote %ls (%dx%d)\n", path, canvas.width(), canvas.height());
    return 0;
}
