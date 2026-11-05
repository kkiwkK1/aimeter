#include "widget/appenv.h"

#include <windows.h>

#include <string>
#include <vector>

namespace wg {
namespace appenv {
namespace {

constexpr const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring Env(const wchar_t* name) {
    wchar_t buf[1024]{};
    const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return n ? std::wstring(buf, n) : std::wstring();
}

bool WritableDir(const std::wstring& dir) {
    if (dir.empty()) return false;
    const std::wstring probe = dir + L"\\.wg-write-test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

} // namespace

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

std::wstring ConfigDir(const std::wstring& appTitle) {
    const std::wstring local = ExeDir();
    if (WritableDir(local)) return local;

    const std::wstring appData = Env(L"APPDATA");
    if (appData.empty()) return local;

    const std::wstring dir = appData + L"\\" + appTitle;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring ConfigPath(const std::wstring& fileName, const std::wstring& appTitle) {
    return ConfigDir(appTitle) + L"\\" + fileName;
}

bool AutostartEnabled(const std::wstring& name) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    const bool on = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, nullptr, nullptr) ==
                    ERROR_SUCCESS;
    RegCloseKey(key);
    return on;
}

std::wstring AutostartCommand(const std::wstring& name) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return std::wstring();

    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        type != REG_SZ || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::wstring();
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name.c_str(), nullptr, &type, (LPBYTE)buffer.data(), &bytes) !=
        ERROR_SUCCESS) {
        RegCloseKey(key);
        return std::wstring();
    }
    RegCloseKey(key);
    return std::wstring(buffer.data());
}

void SetAutostart(const std::wstring& name, bool on) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;

    if (on) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const std::wstring value = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(key, name.c_str(), 0, REG_SZ, (const BYTE*)value.c_str(),
                       (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, name.c_str());
    }
    RegCloseKey(key);
}

void RemoveAutostart(const std::wstring& name) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(key, name.c_str());
    RegCloseKey(key);
}

} // namespace appenv
} // namespace wg
