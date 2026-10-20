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
rem A relative TMP keeps temp files below build\tmp instead of using %TEMP%.
if not exist "build\obj" mkdir "build\obj"
if not exist "build\bin" mkdir "build\bin"
if not exist "build\tmp" mkdir "build\tmp"
set "TMP=build\tmp"
set "TEMP=build\tmp"

set "CXXFLAGS=-std=c++20 -O2 -Wall -Wextra -DNOMINMAX -DUNICODE -D_UNICODE -Isrc"
set "LIBS=-static -static-libgcc -static-libstdc++ -lshell32 -luser32 -lgdi32 -lole32 -luuid -lwinhttp -ladvapi32"
set "FW=canvas view store http secrets host prompt"

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

echo [build] widgets
set "COUNT=0"
for /d %%D in (src\widgets\*) do (
    set "NAME=%%~nxD"
    echo   - !NAME!
    if not exist "build\obj\!NAME!" mkdir "build\obj\!NAME!"
    set "OBJ="
    for %%F in ("%%D\*.cpp") do (
        g++ %CXXFLAGS% -municode -c "%%F" -o "build\obj\!NAME!\%%~nF.o"
        if errorlevel 1 exit /b 1
        set "OBJ=!OBJ! "build\obj\!NAME!\%%~nF.o""
    )
    g++ -municode -mwindows !OBJ! "build\libwg.a" -o "build\bin\!NAME!.exe" %LIBS% -s
    if errorlevel 1 exit /b 1
    set /a COUNT+=1
)
if "!COUNT!"=="0" (echo [build] no widgets under src\widgets & exit /b 1)

if /i "%~1"=="test" (
    echo [build] render test
    g++ %CXXFLAGS% -municode -c "tests\render.cpp" -o "build\obj\render-test.o"
    if errorlevel 1 exit /b 1
    g++ -municode -mconsole "build\obj\render-test.o" "build\libwg.a" -o "build\render-test.exe" %LIBS%
    if errorlevel 1 exit /b 1
    "build\render-test.exe" "build\preview.bmp"
    if errorlevel 1 exit /b 1
)

rem One-time migration: keep the widget where the old single-file build left it.
if exist "aimeter.ini" if not exist "build\bin\aimeter.ini" copy /y "aimeter.ini" "build\bin\aimeter.ini" >nul

echo [build] ok -^> build\bin
