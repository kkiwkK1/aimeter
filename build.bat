@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
set "TOOLROOT=C:\Tools\mingw64"
if exist "%TOOLROOT%\bin\g++.exe" set "PATH=%TOOLROOT%\bin;%PATH%"
where g++ >nul 2>nul || (echo [build] g++ not found & exit /b 1)

rem MinGW + non-ASCII %TEMP% (Chinese user name) breaks the assembler.
set "TMP=C:\Temp\aimeter-build"
set "TEMP=%TMP%"
if not exist "%TMP%" mkdir "%TMP%" >nul 2>nul

g++ -std=c++20 -O2 -s -municode -mwindows -Wall -Wextra ^
    -DNOMINMAX -DUNICODE -D_UNICODE -static -static-libgcc -static-libstdc++ ^
    src\app.cpp -o "%TMP%\aimeter.exe" ^
    -lshell32 -luser32 -lgdi32 -lole32 -luuid -lwinhttp -ladvapi32
if errorlevel 1 (echo [build] FAILED & exit /b 1)
copy /y "%TMP%\aimeter.exe" "%~dp0aimeter.exe" >nul
echo [build] ok
