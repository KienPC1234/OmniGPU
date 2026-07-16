@echo off
title OmniGPU Guest Daemon
cd /d "%~dp0"

:: Try installed location first, then current dir
set DAEMON=%ProgramFiles%\OmniGPU\omnigpu_guestd.exe
if not exist "%DAEMON%" set DAEMON=%~dp0omnigpu_guestd.exe
if not exist "%DAEMON%" (
    echo ERROR: omnigpu_guestd.exe not found!
    echo Run install.bat first.
    pause
    exit /b 1
)

echo Starting OmniGPU Guest Daemon...
start /b "" "%DAEMON%" --foreground
echo Daemon started in background.
echo Close this window to keep daemon running.
echo Use Task Manager to stop omnigpu_guestd.exe
timeout /t 3 >nul
