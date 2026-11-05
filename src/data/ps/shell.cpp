#include "data/ps/shell.h"

#include "data/ps/text.h"

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>

namespace ps {
namespace {

constexpr UINT kNotifyIntervalMs = 80;

bool FileExists(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring OnPath(const wchar_t* exe) {
    wchar_t found[MAX_PATH * 2];
    const DWORD n = SearchPathW(nullptr, exe, L".exe", MAX_PATH * 2, found, nullptr);
    if (n == 0 || n >= MAX_PATH * 2) return std::wstring();
    return found;
}

std::wstring Env(const wchar_t* name) {
    wchar_t value[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, value, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return std::wstring();
    return value;
}

std::wstring Join(const std::wstring& dir, const wchar_t* rest) {
    if (dir.empty()) return std::wstring();
    std::wstring out = dir;
    if (out.back() != L'\\') out.push_back(L'\\');
    out += rest;
    return out;
}

// PowerShell 5.1's fixed install path (System32, not Program Files).
std::wstring WindowsPowerShell() {
    const std::wstring root = Env(L"SystemRoot");
    if (root.empty()) return std::wstring();
    return Join(root, L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
}

std::wstring VersionText(const std::wstring& path, const wchar_t* prefix) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return prefix;

    std::wstring buffer(size / sizeof(wchar_t) + 2, L'\0');
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return prefix;

    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", (void**)&info, &length) || !info) return prefix;
    if (length < sizeof(VS_FIXEDFILEINFO)) return prefix;

    wchar_t text[64];
    // %ls: MinGW's wide swprintf treats %s as narrow, which would print only
    // the first byte of the prefix ("P 7.6").
    swprintf(text, 64, L"%ls %u.%u", prefix, (unsigned)HIWORD(info->dwFileVersionMS),
             (unsigned)LOWORD(info->dwFileVersionMS));
    return text;
}

} // namespace

std::wstring ResolveShell(const std::wstring& overridePath) {
    if (FileExists(overridePath)) return overridePath;

    const std::wstring fromEnv = Env(L"PWSH_EXE");
    if (FileExists(fromEnv)) return fromEnv;

    if (const std::wstring onPath = OnPath(L"pwsh"); !onPath.empty()) return onPath;

    if (const std::wstring programFiles = Env(L"ProgramFiles"); !programFiles.empty()) {
        const std::wstring stable = Join(programFiles, L"PowerShell\\7\\pwsh.exe");
        if (FileExists(stable)) return stable;
    }

    // This machine keeps pwsh as a portable install on another drive; other
    // machines should set shell= in ps.ini instead of relying on this probe.
    if (FileExists(L"L:\\powershell\\pwsh.exe")) return L"L:\\powershell\\pwsh.exe";

    if (const std::wstring onPath = OnPath(L"powershell"); !onPath.empty()) return onPath;

    const std::wstring legacy = WindowsPowerShell();
    if (FileExists(legacy)) return legacy;
    return std::wstring();
}

std::wstring ShellLabel(const std::wstring& path) {
    const wchar_t* base = wcsrchr(path.c_str(), L'\\');
    base = base ? base + 1 : path.c_str();

    if (_wcsicmp(base, L"pwsh.exe") == 0) return VersionText(path, L"PWSH");
    if (_wcsicmp(base, L"powershell.exe") == 0) return VersionText(path, L"PS");
    return L"SHELL";
}

Session::~Session() { Stop(); }

void Session::SetOnOutput(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(cbMu_);
    onOutput_ = std::move(fn);
}

bool Session::Start(const wg::Store& store, const wg::Theme& theme) {
    Stop();
    theme_ = theme;

    const std::wstring path = ResolveShell(store.Get(L"shell"));
    if (path.empty()) {
        std::lock_guard<std::mutex> lock(mu_);
        shellError_ = L"SHELL NOT FOUND";
        lines_.push_back(Line{L"SHELL NOT FOUND - SET shell= IN ps.ini", Kind::Err, theme.bad, ++seq_});
        return false;
    }

    path_ = path;
    label_ = ShellLabel(path);

    // -NonInteractive is the safety net: Read-Host and -Confirm fail on stderr
    // instead of parking a command that nobody can answer from a card.
    // Start in the user's profile: a shell card is a terminal, and inheriting
    // the widget's own directory made every per-directory tool (pi, npm, git)
    // key its state off build\bin instead of the project the user meant.
    const std::wstring args = L"-NoLogo -NoProfile -NonInteractive -NoExit -Command -";
    const std::wstring home = Env(L"USERPROFILE");
    if (!child_.Start(path, args, home)) {
        std::wstring message = L"START FAILED (" + std::to_wstring(child_.error()) + L")";
        {
            std::lock_guard<std::mutex> lock(mu_);
            shellError_ = L"START FAILED";
            lines_.push_back(Line{message + L"  " + path, Kind::Err, theme_.bad, ++seq_});
        }
        return false;
    }

    // Both shells write piped output in the ANSI code page until told
    // otherwise. pwsh 7 then carries stderr along too; 5.1 keeps GBK stderr,
    // which DecodeLine falls back to.
    child_.Write("[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\r\n");
    child_.Write("$OutputEncoding=[Text.UTF8Encoding]::new($false)\r\n");

    running_ = true;
    try {
        out_ = std::thread([this] { ReadLoop(false); });
        err_ = std::thread([this] { ReadLoop(true); });
    } catch (...) {
        running_ = false;
        child_.Shutdown();
        child_.Close();
        std::lock_guard<std::mutex> lock(mu_);
        shellError_ = L"THREAD FAILED";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        shellError_.clear();
        lines_.push_back(Line{L"-- " + label_ + L"  " + path, Kind::Out, theme_.faint, ++seq_});
    }
    return true;
}

void Session::Stop() {
    // The callback survives Stop() so a Restart keeps repainting; the module
    // clears it in its own destructor, before the board goes away.
    running_ = false;
    child_.Shutdown();
    if (out_.joinable()) out_.join();
    if (err_.joinable()) err_.join();
    child_.Close();
}

bool Session::running() const {
    return running_.load() && child_.running();
}

bool Session::WriteLine(const std::wstring& command) {
    if (!running()) return false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        lines_.push_back(Line{L"> " + command, Kind::Echo, theme_.label, ++seq_});
    }
    return child_.Write(EncodeUtf8(command) + "\r\n");
}

void Session::Clear() {
    std::lock_guard<std::mutex> lock(mu_);
    lines_.clear();
}

std::vector<Line> Session::Tail(std::size_t count) const {
    std::lock_guard<std::mutex> lock(mu_);
    const std::size_t n = std::min(count, lines_.size());
    return std::vector<Line>(lines_.end() - (std::ptrdiff_t)n, lines_.end());
}

std::wstring Session::label() const { return label_; }
std::wstring Session::path() const { return path_; }

std::wstring Session::error() const {
    std::lock_guard<std::mutex> lock(mu_);
    return shellError_;
}

void Session::Append(std::wstring text, Kind kind, wg::Color color) {
    std::lock_guard<std::mutex> lock(mu_);
    lines_.push_back(Line{std::move(text), kind, color, ++seq_});
    while (lines_.size() > kMaxLines) lines_.pop_front();
}

std::vector<Line> Session::Snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<Line>(lines_.begin(), lines_.end());
}

void Session::ReadLoop(bool isErr) {
    char buffer[4096];
    LineSplitter splitter;
    std::vector<std::string> complete;

    for (;;) {
        const int n = isErr ? child_.ReadErr(buffer, (int)sizeof(buffer))
                            : child_.ReadOut(buffer, (int)sizeof(buffer));
        if (n <= 0) break;

        complete.clear();
        splitter.Feed(std::string_view(buffer, (std::size_t)n), complete);
        if (complete.empty()) continue;

        for (const std::string& raw : complete) {
            std::wstring text = DecodeLine(raw);
            wg::Color color = 0;
            StripAnsi(text, theme_, color);
            Append(std::move(text), isErr ? Kind::Err : Kind::Out, color);
        }
        Notify();
    }

    // A last line without a newline (a shell dying mid-print) still shows.
    const std::string& tail = splitter.pending();
    if (!tail.empty()) {
        std::wstring text = DecodeLine(tail);
        wg::Color color = 0;
        StripAnsi(text, theme_, color);
        Append(std::move(text), isErr ? Kind::Err : Kind::Out, color);
        Notify();
    }

    if (!child_.running()) running_ = false;
}

void Session::Notify() {
    // Coalesce repaints without ever dropping the last one: the reader sleeps
    // out the remainder of the window, which also back-pressures the shell.
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG last = lastNotify_.load();
    if (last != 0 && now - last < kNotifyIntervalMs) {
        Sleep((DWORD)(kNotifyIntervalMs - (now - last)));
    }
    lastNotify_ = GetTickCount64();

    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> lock(cbMu_);
        fn = onOutput_;
    }
    if (fn) fn();
}

} // namespace ps
