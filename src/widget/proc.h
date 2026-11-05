// wg::proc - small Win32 process helpers shared by widgets and tools.
//
// Two shapes:
//   RunCapture   - hide the window, feed stdin, capture stdout+stderr, time out
//   RunInherited - hand the console (and redirected stdio) to the child, wait
//
// Everything here is deliberately dependency-free: CreateProcessW plus a job
// object so a timed-out ssh cannot leave children behind.
#pragma once

#include <windows.h>

#include <cstddef>
#include <mutex>
#include <string>

namespace wg {

// Windows command-line quoting (the rules CreateProcess expects for argv).
std::wstring QuoteArg(const std::wstring& arg);

struct RunResult {
    bool started = false;
    bool timedOut = false;
    DWORD exitCode = 0;
    std::string output;   // stdout and stderr, merged, capped at maxOutput
};

// Blocks for at most timeoutMs (0 means "use 1 ms"). Never throws.
RunResult RunCapture(const std::wstring& app, const std::wstring& args,
                     const std::string& input, int timeoutMs,
                     std::size_t maxOutput = 64 * 1024);

// Runs the child attached to this console with inherited stdio so ssh can
// prompt for a password and stream output. Returns the exit code, 124 on
// timeout, or -1 when the process could not be created.
int RunInherited(const std::wstring& app, const std::wstring& args, int timeoutMs,
                 bool* timedOut = nullptr);

// A long-lived child with piped stdio - the console card's shell.
//
// RunCapture is "start, feed, collect, die"; this is the other shape: the child
// stays alive across calls, callers write commands into stdin and block on the
// two output pipes from their own reader threads.
//
// Lifetime rule: Shutdown() ends the process (and everything it spawned) but
// keeps the pipes open, so a blocked ReadOut/ReadErr still returns the last
// buffered bytes and then 0. Close() frees the handles and must run only after
// the reader threads have returned.
class Child {
public:
    Child() = default;
    ~Child();
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    // 'workDir' empty = inherit ours. A shell card hands over %USERPROFILE% so
    // the prompt starts where a terminal would, not in the build output dir.
    bool Start(const std::wstring& app, const std::wstring& args,
               const std::wstring& workDir = std::wstring());
    bool running() const;          // false once the process has exited
    DWORD exitCode() const;
    DWORD error() const { return error_; }  // GetLastError from a failed Start

    bool Write(const std::string& bytes);   // stdin; false when the pipe is gone

    // Blocking reads. Return the byte count, or 0 at end of stream.
    int ReadOut(char* buf, int len);
    int ReadErr(char* buf, int len);

    void Shutdown();   // close stdin, then terminate the process tree
    void Close();      // release handles; only after the readers are joined

private:
    int Read(HANDLE pipe, char* buf, int len);

    mutable std::mutex inMu_;   // guards the stdin handle against Shutdown()
    HANDLE in_ = nullptr;
    HANDLE out_ = nullptr;
    HANDLE err_ = nullptr;
    HANDLE proc_ = nullptr;
    HANDLE job_ = nullptr;
    DWORD error_ = 0;
};

} // namespace wg
