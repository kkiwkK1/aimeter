#include "widget/http.h"

#include <winhttp.h>

namespace wg {

HttpResponse HttpGet(const wchar_t* host, const wchar_t* path,
                     const std::wstring& bearer, const wchar_t* userAgent) {
    HttpResponse out;

    HINTERNET session = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return out;
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 8000);

    HINTERNET conn = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) { WinHttpCloseHandle(session); return out; }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return out;
    }

    std::wstring headers = L"Accept: application/json";
    if (!bearer.empty()) headers = L"Authorization: Bearer " + bearer + L"\r\n" + headers;

    BOOL sent = WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr);
    if (sent) {
        out.transport = true;
        DWORD code = 0, size = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        out.status = code;

        DWORD available = 0;
        do {
            available = 0;
            if (!WinHttpQueryDataAvailable(req, &available)) break;
            if (!available) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, chunk.data(), available, &read)) break;
            chunk.resize(read);
            out.body += chunk;
        } while (available > 0);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return out;
}

} // namespace wg
