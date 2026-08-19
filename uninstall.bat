@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM CapsLayer - Windows Uninstaller
REM Removes CapsLayer from C:\Program Files\capslayer and disables Startup
REM ============================================================================

title CapsLayer Uninstaller

REM 1. Check for Administrator Privileges and Request UAC Elevation
fltmc >nul 2>&1
if %errorlevel% neq 0 (
    echo ===================================================
    echo       CapsLayer Uninstall - Elevation Required
    echo ===================================================
    echo.
    echo [Uninstall] Requesting Administrator privileges via UAC...
    echo [Uninstall] Please click 'Yes' in the Windows prompt to continue.
    echo.

    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~dp0capslayer.exe' -ArgumentList '--uninstall' -WorkingDirectory '%~dp0.' -Verb RunAs" 2>nul
    if %errorlevel% equ 0 exit /b 0

    REM Fallback via VBScript
    set "VBS_FILE=%temp%\capslayer_uninst_%random%.vbs"
    echo Set UAC = CreateObject("Shell.Application") > "!VBS_FILE!"
    echo UAC.ShellExecute "%~dp0capslayer.exe", "--uninstall", "%~dp0.", "runas", 1 >> "!VBS_FILE!"
    cscript //nologo "!VBS_FILE!" >nul 2>&1
    del /f /q "!VBS_FILE!" 2>nul
    exit /b 0
)

REM 2. When running elevated, run capslayer.exe --uninstall
"%~dp0capslayer.exe" --uninstall
pause
exit /b %errorlevel%
