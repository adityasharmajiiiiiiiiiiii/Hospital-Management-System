@echo off
rem Builds hms.exe with MinGW-w64 g++ (preferred) or Visual Studio's cl.exe
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 goto trycl
echo Building with g++ ...
g++ -std=c++17 -O2 -pthread -o hms.exe backend\main.cpp -lws2_32
goto check

:trycl
where cl >nul 2>nul
if errorlevel 1 goto nocompiler
echo Building with Visual Studio cl ...
cl /nologo /EHsc /O2 /std:c++17 /Fe:hms.exe backend\main.cpp
goto check

:nocompiler
echo.
echo No C++ compiler found.
echo Install MSYS2 (https://www.msys2.org) and run in its UCRT64 shell:
echo     pacman -S mingw-w64-ucrt-x86_64-gcc
echo then add C:\msys64\ucrt64\bin to your PATH and open a NEW terminal.
echo Alternatively open "x64 Native Tools Command Prompt for VS" and run build.bat there.
exit /b 1

:check
if not exist hms.exe goto failed
echo.
echo Build successful: hms.exe
exit /b 0

:failed
echo.
echo Build failed.
exit /b 1
