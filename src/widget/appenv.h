// Per-user application plumbing shared by the standalone hosts and the board.
#pragma once

#include <string>

namespace wg {
namespace appenv {

// Directory of the running exe (no trailing slash).
std::wstring ExeDir();

// Directory that holds desk.ini / <module>.ini. Uses the exe directory when it
// is writable, otherwise %APPDATA%\<appTitle> (created on demand).
std::wstring ConfigDir(const std::wstring& appTitle);
std::wstring ConfigPath(const std::wstring& fileName, const std::wstring& appTitle);

// HKCU\...\Run value helpers.
bool AutostartEnabled(const std::wstring& name);
std::wstring AutostartCommand(const std::wstring& name);
void SetAutostart(const std::wstring& name, bool on);
void RemoveAutostart(const std::wstring& name);

} // namespace appenv
} // namespace wg
