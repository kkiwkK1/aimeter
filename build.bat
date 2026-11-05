@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
cd /d "%~dp0"

rem MinGW-w64 GCC. Point TOOLROOT at your own toolchain if it lives elsewhere.
set "TOOLROOT=C:\Tools\mingw64"
if exist "%TOOLROOT%\bin\g++.exe" set "PATH=%TOOLROOT%\bin;%PATH%"
where g++ >nul 2>nul || (echo [build] g++ not found & exit /b 1)

rem Keep every path relative: this repo may live under a non-ASCII directory
rem (e.g. a Chinese user name), and MinGW's assembler cannot open such paths.
if not exist "build\obj" mkdir "build\obj"
if not exist "build\bin" mkdir "build\bin"
if not exist "build\tmp" mkdir "build\tmp"
set "TMP=build\tmp"
set "TEMP=build\tmp"

set "CXXFLAGS=-std=c++20 -O2 -Wall -Wextra -DNOMINMAX -DUNICODE -D_UNICODE -Isrc"
set "LIBS=-static -static-libgcc -static-libstdc++ -lshell32 -luser32 -lgdi32 -lole32 -luuid -lwinhttp -ladvapi32 -limm32 -lversion"
set "FW=canvas cjk view layout store http secrets prompt proc skin board board_layout appenv"

echo [build] framework
set "OBJ="
for %%M in (%FW%) do (
    echo   - %%M
    g++ %CXXFLAGS% -c "src\widget\%%M.cpp" -o "build\obj\%%M.o"
    if errorlevel 1 exit /b 1
    set "OBJ=!OBJ! "build\obj\%%M.o""
)
if exist "build\libwg.a" del /q "build\libwg.a"
ar rcs "build\libwg.a" %OBJ%
if errorlevel 1 (echo [build] ar failed & exit /b 1)

echo [build] desk
if not exist "build\obj\desk" mkdir "build\obj\desk"
if not exist "build\obj\desk\aim" mkdir "build\obj\desk\aim"
if not exist "build\obj\desk\srv" mkdir "build\obj\desk\srv"
if not exist "build\obj\desk\ps" mkdir "build\obj\desk\ps"
set "DOBJ="
for %%F in ("src\apps\desk\*.cpp") do (
    g++ %CXXFLAGS% -municode -c "%%F" -o "build\obj\desk\%%~nF.o"
    if errorlevel 1 exit /b 1
    set "DOBJ=!DOBJ! "build\obj\desk\%%~nF.o""
)
set "PANELCOUNT=0"
for /d %%D in (src\panels\*) do (
    set "PNAME=%%~nxD"
    echo   - !PNAME!
    if not exist "build\obj\panels\!PNAME!" mkdir "build\obj\panels\!PNAME!"
    for %%F in ("%%D\*.cpp") do (
        g++ %CXXFLAGS% -municode -c "%%F" -o "build\obj\panels\!PNAME!\%%~nF.o"
        if errorlevel 1 exit /b 1
        set "DOBJ=!DOBJ! "build\obj\panels\!PNAME!\%%~nF.o""
    )
    set /a PANELCOUNT+=1
)
if "!PANELCOUNT!"=="0" (echo [build] no panels under src\panels & exit /b 1)

rem Shared data sources: the cards and srvctl use the same code.
for %%M in (providers token_usage commandcode collect) do (
    g++ %CXXFLAGS% -c "src\data\aimeter\%%M.cpp" -o "build\obj\desk\aim\%%M.o"
    if errorlevel 1 exit /b 1
    set "DOBJ=!DOBJ! "build\obj\desk\aim\%%M.o""
)
for %%M in (providers status_view collect) do (
    g++ %CXXFLAGS% -c "src\data\server\%%M.cpp" -o "build\obj\desk\srv\%%M.o"
    if errorlevel 1 exit /b 1
    set "DOBJ=!DOBJ! "build\obj\desk\srv\%%M.o""
)
rem The PowerShell card: pipes, decoding, ANSI, line editing.
for %%M in (text shell input selection) do (
    g++ %CXXFLAGS% -c "src\data\ps\%%M.cpp" -o "build\obj\desk\ps\%%M.o"
    if errorlevel 1 exit /b 1
    set "DOBJ=!DOBJ! "build\obj\desk\ps\%%M.o""
)
g++ -municode -mwindows !DOBJ! "build\libwg.a" -o "build\bin\desk.exe.new" %LIBS% -s
if errorlevel 1 exit /b 1
move /y "build\bin\desk.exe.new" "build\bin\desk.exe" >nul
if errorlevel 1 (echo [build] cannot replace desk.exe & exit /b 1)

echo [build] cli
if not exist "build\obj\cli" mkdir "build\obj\cli"
set "COBJ="
for %%F in ("src\cli\*.cpp") do (
    g++ %CXXFLAGS% -municode -c "%%F" -o "build\obj\cli\%%~nF.o"
    if errorlevel 1 exit /b 1
    set "COBJ=!COBJ! "build\obj\cli\%%~nF.o""
)
g++ %CXXFLAGS% -c "src\data\server\providers.cpp" -o "build\obj\cli\providers.o"
if errorlevel 1 exit /b 1
g++ -municode -mconsole !COBJ! "build\obj\cli\providers.o" "build\libwg.a" -o "build\bin\srvctl.exe.new" %LIBS% -s
if errorlevel 1 exit /b 1
move /y "build\bin\srvctl.exe.new" "build\bin\srvctl.exe" >nul
if errorlevel 1 (echo [build] cannot replace srvctl.exe & exit /b 1)

if /i "%~1"=="test" (
    echo [build] tests
    if not exist "build\obj\test" mkdir "build\obj\test"

    echo   - render
    g++ %CXXFLAGS% -municode -c "tests\render.cpp" -o "build\obj\test\render.o"
    if errorlevel 1 exit /b 1
    g++ -municode -mconsole "build\obj\test\render.o" "build\libwg.a" -o "build\render-test.exe.new" %LIBS%
    if errorlevel 1 exit /b 1
    move /y "build\render-test.exe.new" "build\render-test.exe" >nul
    if errorlevel 1 exit /b 1

    echo   - board layout
    g++ %CXXFLAGS% -municode -c "tests\board_layout.cpp" -o "build\obj\test\board_layout.o"
    if errorlevel 1 exit /b 1
    g++ -municode -mconsole "build\obj\test\board_layout.o" "build\libwg.a" -o "build\board-layout-test.exe.new" %LIBS%
    if errorlevel 1 exit /b 1
    move /y "build\board-layout-test.exe.new" "build\board-layout-test.exe" >nul
    if errorlevel 1 exit /b 1

    echo   - desk render
    g++ %CXXFLAGS% -municode -c "src\panels\account\account_view.cpp" -o "build\obj\test\account_view.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\panels\servers\servers_view.cpp" -o "build\obj\test\servers_view.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\server\status_view.cpp" -o "build\obj\test\status_view.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\server\providers.cpp" -o "build\obj\test\server_providers.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\aimeter\token_usage.cpp" -o "build\obj\test\token_usage.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\aimeter\providers.cpp" -o "build\obj\test\aimeter_providers.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\panels\ps\ps_view.cpp" -o "build\obj\test\ps_view.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\ps\input.cpp" -o "build\obj\test\ps_input.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\ps\selection.cpp" -o "build\obj\test\ps_selection.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "tests\desk_render.cpp" -o "build\obj\test\desk_render.o"
    if errorlevel 1 exit /b 1
    g++ -municode -mconsole "build\obj\test\desk_render.o" "build\obj\test\account_view.o" "build\obj\test\servers_view.o" "build\obj\test\ps_view.o" "build\obj\test\ps_input.o" "build\obj\test\ps_selection.o" "build\obj\test\status_view.o" "build\obj\test\server_providers.o" "build\obj\test\token_usage.o" "build\obj\test\aimeter_providers.o" "build\libwg.a" -o "build\desk-render-test.exe.new" %LIBS%
    if errorlevel 1 exit /b 1
    move /y "build\desk-render-test.exe.new" "build\desk-render-test.exe" >nul
    if errorlevel 1 exit /b 1

    echo   - powershell session
    g++ %CXXFLAGS% -municode -c "src\data\ps\text.cpp" -o "build\obj\test\ps_text.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\data\ps\shell.cpp" -o "build\obj\test\ps_shell.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "tests\ps_tests.cpp" -o "build\obj\test\ps_tests.o"
    if errorlevel 1 exit /b 1
    g++ -municode -mconsole "build\obj\test\ps_tests.o" "build\obj\test\ps_text.o" "build\obj\test\ps_shell.o" "build\obj\test\ps_input.o" "build\obj\test\ps_selection.o" "build\libwg.a" -o "build\ps-test.exe.new" %LIBS%
    if errorlevel 1 exit /b 1
    move /y "build\ps-test.exe.new" "build\ps-test.exe" >nul
    if errorlevel 1 exit /b 1

    echo   - cli
    g++ %CXXFLAGS% -municode -c "src\cli\args.cpp" -o "build\obj\test\args.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\cli\config.cpp" -o "build\obj\test\config.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\cli\output.cpp" -o "build\obj\test\output.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "src\cli\targets.cpp" -o "build\obj\test\targets.o"
    if errorlevel 1 exit /b 1
    g++ %CXXFLAGS% -municode -c "tests\cli_tests.cpp" -o "build\obj\test\cli_tests.o"
    if errorlevel 1 exit /b 1
    g++ -municode -mconsole "build\obj\test\args.o" "build\obj\test\config.o" "build\obj\test\output.o" "build\obj\test\targets.o" "build\obj\test\cli_tests.o" "build\libwg.a" -o "build\cli-test.exe.new" %LIBS%
    if errorlevel 1 exit /b 1
    move /y "build\cli-test.exe.new" "build\cli-test.exe" >nul
    if errorlevel 1 exit /b 1

    echo [build] sign tests
    powershell -ExecutionPolicy Bypass -File tools\sign-local.ps1 -Path "build\*-test.exe"
    if errorlevel 1 (echo [build] test signing failed & exit /b 1)

    echo [build] run tests
    "build\render-test.exe" "build\preview.bmp"
    if errorlevel 1 exit /b 1
    "build\board-layout-test.exe"
    if errorlevel 1 exit /b 1
    "build\desk-render-test.exe" "build\desk-board-preview.bmp"
    if errorlevel 1 exit /b 1
    "build\ps-test.exe"
    if errorlevel 1 exit /b 1
    "build\cli-test.exe"
    if errorlevel 1 exit /b 1
)

echo [build] ok -^> build\bin
