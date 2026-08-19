@echo off
setlocal EnableDelayedExpansion
title CapsLayer Uninstaller

:: Ensure script runs from its directory
cd /d "%~dp0"
set "SCRIPT_DIR=%~dp0"

:: Check for Administrator privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [CapsLayer] Requesting Administrator privileges for uninstallation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

if defined ProgramFiles(x86) (
    set "TARGET_DIR=%ProgramFiles(x86)%\capslayer"
) else (
    set "TARGET_DIR=C:\Program Files (x86)\capslayer"
)

echo ===================================================
echo             CapsLayer Uninstaller
echo ===================================================
echo.

:: 1. Terminate running processes
echo [1/4] Stopping CapsLayer process...
taskkill /F /IM capslayer.exe >nul 2>&1
timeout /t 1 /nobreak >nul

:: 2. Remove Task Scheduler startup task
echo [2/4] Removing Task Scheduler startup task...
schtasks /Delete /TN "CapsLayer" /F >nul 2>&1

:: 3. Remove Registry & Start Menu shortcuts
echo [3/4] Cleaning Registry startup keys and Start Menu shortcuts...
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1

if exist "%ProgramData%\Microsoft\Windows\Start Menu\Programs\CapsLayer.lnk" (
    del /f /q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\CapsLayer.lnk" >nul 2>&1
)
if exist "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk" (
    del /f /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk" >nul 2>&1
)
if exist "%ProgramData%\Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk" (
    del /f /q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk" >nul 2>&1
)

:: 4. Remove installation folder (if script is running outside target folder or schedule folder delete)
echo [4/4] Removing installed files in "%TARGET_DIR%"...
if /i "%CD%"=="%TARGET_DIR%" (
    cd /d "%TEMP%"
)

if exist "%TARGET_DIR%" (
    rd /s /q "%TARGET_DIR%" >nul 2>&1
)

echo.
echo ===================================================
echo [SUCCESS] CapsLayer has been completely uninstalled!
echo ===================================================
echo.
pause
