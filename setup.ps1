<#
.SYNOPSIS
    CapsLayer Installer for Windows
.DESCRIPTION
    Installs CapsLayer to "C:\Program Files (x86)\capslayer", configures silent elevated startup
    via Windows Task Scheduler with zero UAC prompts, sets folder permissions, and starts the daemon.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

# Self-elevation check
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[CapsLayer Setup] Elevating to Administrator..." -ForegroundColor Yellow
    Start-Process -FilePath powershell.exe -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

$scriptDir = $PSScriptRoot
if (-not $scriptDir) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
}

$progFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
if (-not $progFilesX86) {
    $progFilesX86 = "C:\Program Files (x86)"
}

$targetDir = Join-Path $progFilesX86 "capslayer"
$targetExe = Join-Path $targetDir "capslayer.exe"
$targetCfg = Join-Path $targetDir "config.json"
$targetUninstall = Join-Path $targetDir "uninstall.bat"

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "       CapsLayer Automated Setup & Installer       " -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "Source directory : $scriptDir"
Write-Host "Target directory : $targetDir`n"

# 1. Verify source capslayer.exe
$srcExe = Join-Path $scriptDir "capslayer.exe"
if (-not (Test-Path $srcExe)) {
    Write-Error "[ERROR] capslayer.exe was not found in '$scriptDir'."
    exit 1
}

# 2. Stop running instances
Write-Host "[1/6] Stopping any running CapsLayer instances..." -ForegroundColor Yellow
Get-Process -Name "capslayer" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

# 3. Create target directory
Write-Host "[2/6] Creating installation directory..." -ForegroundColor Yellow
if (-not (Test-Path $targetDir)) {
    New-Item -Path $targetDir -ItemType Directory -Force | Out-Null
}

# 4. Copy necessary files
Write-Host "[3/6] Copying necessary files..." -ForegroundColor Yellow
Copy-Item -Path $srcExe -Destination $targetExe -Force
Write-Host "  - Copied capslayer.exe" -ForegroundColor Green

$srcCfg = Join-Path $scriptDir "config.json"
if (Test-Path $srcCfg) {
    if (-not (Test-Path $targetCfg)) {
        Copy-Item -Path $srcCfg -Destination $targetCfg -Force
        Write-Host "  - Copied default config.json" -ForegroundColor Green
    } else {
        Write-Host "  - Preserved existing config.json" -ForegroundColor DarkGray
    }
}

$srcUninstall = Join-Path $scriptDir "uninstall.bat"
if (Test-Path $srcUninstall) {
    Copy-Item -Path $srcUninstall -Destination $targetUninstall -Force
    Write-Host "  - Copied uninstall.bat" -ForegroundColor Green
}

# 5. Set folder permissions
Write-Host "[4/6] Configuring file permissions..." -ForegroundColor Yellow
try {
    $acl = Get-Acl $targetDir
    $rule = New-Object System.Security.AccessControl.FileSystemAccessRule("Users", "Modify, Synchronize", "ContainerInherit, ObjectInherit", "None", "Allow")
    $acl.AddAccessRule($rule)
    Set-Acl -Path $targetDir -AclObject $acl
    Write-Host "  - Granted standard users permission to edit config.json" -ForegroundColor Green
} catch {
    & icacls "$targetDir" /grant "*S-1-5-32-545:(OI)(CI)M" /T /Q | Out-Null
}

# 6. Clean up Registry and Startup folders (which cause unelevated UAC prompts)
Write-Host "[5/6] Registering elevated startup (Zero UAC prompts)..." -ForegroundColor Yellow
Remove-ItemProperty -Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "CapsLayer" -ErrorAction SilentlyContinue
Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "CapsLayer" -ErrorAction SilentlyContinue

$userStartup = [Environment]::GetFolderPath([Environment+SpecialFolder]::Startup)
$commonStartup = [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonStartup)
Remove-Item -Path (Join-Path $userStartup "CapsLayer.lnk") -Force -ErrorAction SilentlyContinue
Remove-Item -Path (Join-Path $commonStartup "CapsLayer.lnk") -Force -ErrorAction SilentlyContinue

# Register Task Scheduler task with highest privileges
try {
    $action = New-ScheduledTaskAction -Execute $targetExe -WorkingDirectory $targetDir
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $principal = New-ScheduledTaskPrincipal -GroupId "BUILTIN\Users" -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0 -Priority 4 -MultipleInstances IgnoreNew
    $task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal -Settings $settings

    Register-ScheduledTask -TaskName "CapsLayer" -InputObject $task -Force | Out-Null
    Write-Host "  - Task Scheduler task 'CapsLayer' registered (Runs elevated on logon with ZERO UAC prompts)" -ForegroundColor Green
} catch {
    & schtasks /Create /TN "CapsLayer" /TR "`"$targetExe`"" /SC ONLOGON /RL HIGHEST /F /DELAY 0000:00 2>&1 | Out-Null
}

# 7. Create Start Menu Shortcut
Write-Host "[6/6] Creating Start Menu shortcut & launching..." -ForegroundColor Yellow
try {
    $wscript = New-Object -ComObject WScript.Shell
    $progData = [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonPrograms)
    $shortcutPath = Join-Path $progData "CapsLayer.lnk"
    $shortcut = $wscript.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $targetExe
    $shortcut.WorkingDirectory = $targetDir
    $shortcut.Description = "CapsLayer Keyboard Remapper & Layer Daemon"
    $shortcut.Save()
    Write-Host "  - Start Menu shortcut created" -ForegroundColor Green
} catch {
    Write-Warning "Could not create Start Menu shortcut: $_"
}

# 8. Start the daemon process
Start-Process -FilePath $targetExe -WorkingDirectory $targetDir

Write-Host "`n===================================================" -ForegroundColor Cyan
Write-Host "[SUCCESS] CapsLayer has been installed successfully!" -ForegroundColor Green
Write-Host "  - Location : $targetDir"
Write-Host "  - Startup  : Elevated (Runs automatically on logon with ZERO UAC prompts)"
Write-Host "  - Status   : Daemon started in background"
Write-Host "===================================================`n" -ForegroundColor Cyan
