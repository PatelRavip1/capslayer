@echo off
setlocal EnableDelayedExpansion
title CapsLayer Setup

:: Ensure script operates in the folder where setup.bat is located
cd /d "%~dp0"
set "SRC_DIR=%~dp0"

:: Check for Administrator privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [CapsLayer Setup] Requesting Administrator privileges to configure Task Scheduler...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

:: Define target paths
if defined ProgramFiles(x86) (
    set "TARGET_DIR=%ProgramFiles(x86)%\capslayer"
) else (
    set "TARGET_DIR=C:\Program Files (x86)\capslayer"
)

set "TARGET_EXE=%TARGET_DIR%\capslayer.exe"
set "TARGET_CFG=%TARGET_DIR%\config.json"
set "TARGET_UNINSTALL=%TARGET_DIR%\uninstall.bat"

echo ===================================================
echo       CapsLayer Automated Setup & Installer
echo ===================================================
echo.
echo Source directory : %SRC_DIR%
echo Target directory : %TARGET_DIR%
echo.

:: 1. Verify necessary files exist in source folder
if not exist "%SRC_DIR%capslayer.exe" (
    echo [ERROR] capslayer.exe was not found in "%SRC_DIR%".
    echo Please make sure capslayer.exe is present before running setup.
    pause
    exit /b 1
)

:: 2. Stop any existing capslayer.exe instances
echo [1/6] Stopping any running CapsLayer instances...
taskkill /F /IM capslayer.exe >nul 2>&1
timeout /t 1 /nobreak >nul

:: 3. Create target directory
echo [2/6] Creating installation directory...
if not exist "%TARGET_DIR%" (
    mkdir "%TARGET_DIR%"
    if %errorlevel% neq 0 (
        echo [ERROR] Failed to create directory "%TARGET_DIR%".
        pause
        exit /b 1
    )
)

:: 4. Copy necessary files
echo [3/6] Copying necessary files to "%TARGET_DIR%"...

echo   - Copying capslayer.exe...
copy /Y "%SRC_DIR%capslayer.exe" "%TARGET_EXE%" >nul
if %errorlevel% neq 0 (
    echo [ERROR] Failed to copy capslayer.exe.
    pause
    exit /b 1
)

if exist "%SRC_DIR%config.json" (
    if not exist "%TARGET_CFG%" (
        echo   - Copying default config.json...
        copy /Y "%SRC_DIR%config.json" "%TARGET_CFG%" >nul
    ) else (
        echo   - Preserving existing config.json in target directory...
    )
)

if exist "%SRC_DIR%uninstall.bat" (
    echo   - Copying uninstall.bat...
    copy /Y "%SRC_DIR%uninstall.bat" "%TARGET_UNINSTALL%" >nul
)

:: 5. Set folder permissions so standard users can edit config.json
echo [4/6] Configuring file permissions for config editing...
icacls "%TARGET_DIR%" /grant *S-1-5-32-545:(OI)(CI)M /T /Q >nul 2>&1

:: 6. Clean up legacy Registry Run keys and Startup folder shortcuts (which cause unelevated UAC popups)
echo [5/6] Registering elevated startup with zero UAC prompts...
reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "CapsLayer" /f >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p1 = [System.IO.Path]::Combine($env:APPDATA, 'Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk'); $p2 = [System.IO.Path]::Combine($env:ProgramData, 'Microsoft\Windows\Start Menu\Programs\Startup\CapsLayer.lnk'); Remove-Item -Path $p1, $p2 -Force -ErrorAction SilentlyContinue" >nul 2>&1

:: Configure Task Scheduler for elevated startup without UAC prompt
powershell -NoProfile -ExecutionPolicy Bypass -Command "$action = New-ScheduledTaskAction -Execute '%TARGET_EXE%' -WorkingDirectory '%TARGET_DIR%'; $trigger = New-ScheduledTaskTrigger -AtLogOn; $principal = New-ScheduledTaskPrincipal -GroupId 'BUILTIN\Users' -RunLevel Highest; $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0 -Priority 4 -MultipleInstances IgnoreNew; $task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal -Settings $settings; Register-ScheduledTask -TaskName 'CapsLayer' -InputObject $task -Force" >nul 2>&1
if %errorlevel% neq 0 (
    schtasks /Create /TN "CapsLayer" /TR "\"%TARGET_EXE%\"" /SC ONLOGON /RL HIGHEST /F /DELAY 0000:00 >nul 2>&1
)
echo   - Windows Task Scheduler registered: Runs elevated on logon with ZERO UAC prompts.

:: 7. Create Start Menu shortcut
echo [6/6] Creating Start Menu shortcut and launching...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ws = New-Object -ComObject WScript.Shell; $scPath = [System.IO.Path]::Combine($env:ProgramData, 'Microsoft\Windows\Start Menu\Programs\CapsLayer.lnk'); $sc = $ws.CreateShortcut($scPath); $sc.TargetPath = '%TARGET_EXE%'; $sc.WorkingDirectory = '%TARGET_DIR%'; $sc.Description = 'CapsLayer Keyboard Remapper'; $sc.Save()" >nul 2>&1

:: 8. Launch CapsLayer daemon in background
start "" "%TARGET_EXE%"

echo.
echo ===================================================
echo [SUCCESS] CapsLayer installation complete!
echo ===================================================
echo   - Installed to : %TARGET_DIR%
echo   - Startup      : Administrator (Elevated) via Task Scheduler
echo   - UAC Prompts  : ZERO (Starts silently on logon with full Admin privs)
echo   - Status       : Running in background (Check System Tray)
echo ===================================================
echo.
pause
