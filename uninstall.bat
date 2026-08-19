@echo off
setlocal EnableDelayedExpansion
title CapsLayer Uninstaller
cd /d "%~dp0"

:: Check Administrator privileges; auto-elevate if needed
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [CapsLayer] Elevating to Administrator...
    if "%~1"=="" (
        powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    ) else (
        powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -ArgumentList '%*' -Verb RunAs"
    )
    exit /b
)

if defined ProgramFiles(x86) (
    set "TARGET_DIR=%ProgramFiles(x86)%\capslayer"
) else (
    set "TARGET_DIR=C:\Program Files (x86)\capslayer"
)
set "SHORTCUT=%ProgramData%\Microsoft\Windows\Start Menu\Programs\CapsLayer.lnk"
set "STARTUP_USER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk"
set "STARTUP_ALL=%ProgramData%\Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk"

echo ===================================================
echo               CapsLayer Uninstaller
echo ===================================================

echo [1/3] Stopping CapsLayer process...
taskkill /F /IM capslayer.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo [2/3] Removing Task Scheduler startup, registry entries, and shortcuts...
schtasks /Delete /TN "CapsLayer" /F >nul 2>&1
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
if exist "%SHORTCUT%" del /F /Q "%SHORTCUT%" >nul 2>&1
if exist "%STARTUP_USER%" del /F /Q "%STARTUP_USER%" >nul 2>&1
if exist "%STARTUP_ALL%" del /F /Q "%STARTUP_ALL%" >nul 2>&1

echo [3/3] Removing installation folder...
if /i "%CD%"=="%TARGET_DIR%" cd /d "%TEMP%"
if exist "%TARGET_DIR%" rmdir /S /Q "%TARGET_DIR%" >nul 2>&1

echo.
echo ===================================================
echo [SUCCESS] CapsLayer has been completely uninstalled.
echo ===================================================
timeout /t 3 >nul
exit /b 0
