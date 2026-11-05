// A resident shell behind pipes, plus the small line buffer the card paints.
//
// Why a session and not RunCapture: the card keeps one shell alive so `cd`,
// variables and modules persist between commands, the way a terminal does.
// Two reader threads (stdout and stderr are separate pipes because the shells
// use different code pages for them) append decoded lines under a mutex; the
// UI thread only ever takes a snapshot of the tail.
#pragma once

#include "widget/canvas.h"
#include "widget/proc.h"
#include "widget/store.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ps {

enum class Kind { Out, Err, Echo };

struct Line {
    std::wstring text;
    Kind kind = Kind::Out;
    wg::Color color = 0;   // 0 = the card's default text colour
    // Monotonic id: a selection keeps pointing at the right line even after the
    // ring drops the oldest ones or new output arrives.
    unsigned long long seq = 0;
};

// Lines kept in memory. Long enough to scroll back through an inspection,
// short enough that a runaway command cannot grow the process.
constexpr std::size_t kMaxLines = 500;

// How many lines the card shows collapsed / expanded.
constexpr int kCollapsedLines = 6;
constexpr int kExpandedLines = 14;

// Finds the shell to run: explicit override, then pwsh on PATH, then the usual
// install locations, then Windows PowerShell 5.1. Empty when nothing exists.
std::wstring ResolveShell(const std::wstring& overridePath);

// "PWSH 7.6" / "PS 5.1" from the executable's version resource.
std::wstring ShellLabel(const std::wstring& path);

class Session {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Called from a reader thread after new output landed. The module points it
    // at RequestRepaint(); it is rate limited to one call per 80 ms.
    void SetOnOutput(std::function<void()> fn);

    // 'store' supplies the "shell" override; 'theme' colours SGR output.
    bool Start(const wg::Store& store, const wg::Theme& theme);
    void Stop();
    bool running() const;

    bool WriteLine(const std::wstring& command);
    void Clear();

    std::vector<Line> Tail(std::size_t count) const;
    // Every retained line, oldest first. Copy-to-clipboard walks this.
    std::vector<Line> Snapshot() const;
    std::wstring label() const;
    std::wstring path() const;
    std::wstring error() const;   // "SHELL NOT FOUND" / "START FAILED (5)"

private:
    void ReadLoop(bool isErr);
    void Append(std::wstring text, Kind kind, wg::Color color);
    void Notify();
    void StartShell(std::wstring path);
    void JoinReaders();

    wg::Child child_;
    wg::Theme theme_;
    mutable std::mutex mu_;
    std::deque<Line> lines_;
    unsigned long long seq_ = 0;   // last assigned Line::seq
    std::wstring shellError_;

    std::wstring path_;
    std::wstring label_;
    std::atomic<bool> running_{false};

    std::thread out_;
    std::thread err_;
    std::mutex cbMu_;
    std::function<void()> onOutput_;
    std::atomic<ULONGLONG> lastNotify_{0};
};

} // namespace ps
