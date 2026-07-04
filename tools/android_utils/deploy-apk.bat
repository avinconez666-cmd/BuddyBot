@echo off
REM Build BuddyBot Kids APK and copy to dist\ + Downloads for Phone Link transfer.
REM Double-click this file, or run: deploy-apk.bat
REM Options: deploy-apk.bat clean

setlocal
cd /d "%~dp0"

set "ARGS="
if /i "%~1"=="clean" set "ARGS=-Clean"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy-apk.ps1" %ARGS%
set "RC=%ERRORLEVEL%"

if %RC% NEQ 0 (
    echo.
    echo Deploy failed.
    pause
    exit /b %RC%
)

echo.
pause
exit /b 0