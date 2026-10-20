// One synchronous HTTPS GET on WinHTTP. Called from a widget's worker thread,
// so blocking here never freezes the desktop.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace wg {

struct HttpResponse {
    bool transport = false;   // request completed (any status)
    DWORD status = 0;         // HTTP status when transport == true
    std::string body;

    bool ok() const { return transport && status == 200; }
};

// Direct connection on purpose: WPAD auto-discovery can stall ~30s on networks
// without a proxy, and the APIs these widgets call are reachable directly.
HttpResponse HttpGet(const wchar_t* host, const wchar_t* path,
                     const std::wstring& bearer,
                     const wchar_t* userAgent = L"WinWidget/1.0");

} // namespace wg
