@echo off
setlocal EnableDelayedExpansion
title CapsLayer Setup
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
set "TARGET_EXE=%TARGET_DIR%\capslayer.exe"
set "TARGET_CFG=%TARGET_DIR%\config.json"
set "SHORTCUT=%ProgramData%\Microsoft\Windows\Start Menu\Programs\CapsLayer.lnk"

:: Check if uninstall requested (/u, -u, --uninstall, uninstall)
if /i "%~1"=="/u" goto UNINSTALL
if /i "%~1"=="-u" goto UNINSTALL
if /i "%~1"=="--uninstall" goto UNINSTALL
if /i "%~1"=="uninstall" goto UNINSTALL

:INSTALL
echo ===================================================
echo             CapsLayer Setup & Installer
echo ===================================================
if not exist "%~dp0capslayer.exe" (
    echo [ERROR] capslayer.exe not found in "%~dp0"
    pause & exit /b 1
)

echo [1/5] Stopping existing processes...
taskkill /F /IM capslayer.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo [2/5] Installing files to %TARGET_DIR%...
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
copy /Y "%~dp0capslayer.exe" "%TARGET_EXE%" >nul
if exist "%~dp0config.json" if not exist "%TARGET_CFG%" copy /Y "%~dp0config.json" "%TARGET_CFG%" >nul
copy /Y "%~f0" "%TARGET_DIR%\setup.bat" >nul
if exist "%~dp0uninstall.bat" copy /Y "%~dp0uninstall.bat" "%TARGET_DIR%\uninstall.bat" >nul
icacls "%TARGET_DIR%" /grant *S-1-5-32-545:(OI)(CI)M /T /Q >nul 2>&1

echo [3/5] Registering elevated startup (Task Scheduler)...
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "$a = New-ScheduledTaskAction -Execute '%TARGET_EXE%' -WorkingDirectory '%TARGET_DIR%'; $t = New-ScheduledTaskTrigger -AtLogOn; $p = New-ScheduledTaskPrincipal -GroupId 'BUILTIN\Users' -RunLevel Highest; $s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan); Register-ScheduledTask -TaskName 'CapsLayer' -Action $a -Trigger $t -Principal $p -Settings $s -Force" >nul 2>&1

echo [4/5] Creating Start Menu shortcut...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%SHORTCUT%'); $s.TargetPath = '%TARGET_EXE%'; $s.WorkingDirectory = '%TARGET_DIR%'; $s.Description = 'CapsLayer Keyboard Remapper'; $s.Save()" >nul 2>&1

echo [5/5] Launching CapsLayer in background...
start "" "%TARGET_EXE%"

echo.
echo ===================================================
echo [SUCCESS] CapsLayer installation complete!
echo   - Location: %TARGET_DIR%
echo   - Startup : Task Scheduler (Elevated, Zero UAC)
echo ===================================================
timeout /t 3 >nul
exit /b 0

:UNINSTALL
echo ===================================================
echo               CapsLayer Uninstaller
echo ===================================================
echo [1/3] Stopping CapsLayer process...
taskkill /F /IM capslayer.exe >nul 2>&1
timeout /t 1 /nobreak >nul

echo [2/3] Removing Task Scheduler startup and shortcuts...
schtasks /Delete /TN "CapsLayer" /F >nul 2>&1
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
if exist "%SHORTCUT%" del /F /Q "%SHORTCUT%" >nul 2>&1

echo [3/3] Removing installation folder...
if /i "%CD%"=="%TARGET_DIR%" cd /d "%TEMP%"
if exist "%TARGET_DIR%" rmdir /S /Q "%TARGET_DIR%" >nul 2>&1

echo.
echo [SUCCESS] CapsLayer has been completely uninstalled.
timeout /t 3 >nul
exit /b 0
