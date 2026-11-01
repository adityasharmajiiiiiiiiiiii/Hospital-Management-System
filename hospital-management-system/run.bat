@echo off
rem Builds (if needed), starts the server and opens the browser
cd /d "%~dp0"

if exist hms.exe goto start
call build.bat
if not exist hms.exe goto end

:start
start "" cmd /c "timeout /t 2 /nobreak >nul && start http://localhost:8080"
hms.exe 8080 frontend data

:end
pause
