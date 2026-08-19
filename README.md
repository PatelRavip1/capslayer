# CapsLayer

**CapsLayer** is a lightweight, high-performance Windows keyboard remapper and modal layer daemon written in pure C using the Win32 API.

It transforms the **CapsLock** key into a dual-role key:
- **Tap CapsLock**: Acts as **Escape** (fast Vim-friendly escape).
- **Hold CapsLock**: Activates a customizable navigation, editing, and shortcut layer (e.g. `CapsLock + I/J/K/L` for Arrow keys).
- **CapsLock + P**: Toggles **Persistent Layer Lock** mode with visual tray feedback.
- **Global Shortcuts**: Launch applications and execute commands asynchronously without blocking input.

> 📖 **Looking for full internal architecture, file structure, and function-by-function call graphs?** Read **[DOCUMENTATION.md](DOCUMENTATION.md)**.

---

## Key Features

- **Zero Latency & Low Memory**: Written in pure C with Win32 low-level keyboard hooks (`WH_KEYBOARD_LL`). Zero external runtime dependencies.
- **Dual-Role CapsLock**:
  - Quick tap sends `Escape` (or original `CapsLock` toggle based on configuration).
  - Holding `CapsLock` activates layer mappings.
- **Persistent Layer Mode (Layer Lock)**:
  - Press `CapsLock + P` to lock the navigation layer on.
  - Press `CapsLock` (or `P`) again to unlock back to normal typing mode.
- **Live Config Hot-Reloading**:
  - Built-in directory watcher (`ReadDirectoryChangesW`) monitors `config.json` and hot-reloads bindings instantly upon saving.
- **System Tray Integration**:
  - Status indicator icon: **Green (Active)**, **Blue (Layer Locked)**, **Gray (Paused)**.
  - Double-click tray icon to toggle pause/active.
  - Context menu for Reloading Config, Editing Config, Status, and Exiting.
- **Windows Task Scheduler Startup**:
  - Installs to `C:\Program Files (x86)\capslayer` and registers with Task Scheduler to run elevated at user logon with zero UAC prompts.
- **Native Background Daemon**:
  - Runs quietly as a Windows GUI subsystem process (`IMAGE_SUBSYSTEM_WINDOWS_GUI`) without showing any console window.

---

## Default Layer Mappings (Hold CapsLock)

| Key | Action / Remap |
| :--- | :--- |
| **`I`** | **Up Arrow** (`↑`) |
| **`J`** | **Left Arrow** (`←`) |
| **`K`** | **Down Arrow** (`↓`) |
| **`L`** | **Right Arrow** (`→`) |
| **`M`** | **Delete** |
| **`N`** | **Backspace** |
| **`P`** | **Toggle Persistent Layer Lock** |
| **`W`** | **Alt + F4** (Close Window) |
| **`C`** | **Ctrl + C** (Copy) |
| **`V`** | **Ctrl + V** (Paste) |
| **`Z`** | **Launch Windows Terminal** (`wt.exe`) |

### Default Global Shortcuts

| Shortcut | Action |
| :--- | :--- |
| **`Win + Alt + Esc`** | **Shutdown** (`shutdown.exe /s /t 0`) |
| **`Win + Shift + I`** | **Launch Shortcut** (`C:\titus.lnk`) |

---

## Configuration (`config.json`)

`config.json` resides next to `capslayer.exe` (or in `C:\Program Files (x86)\capslayer\config.json` after installation).

```json
{
  "settings": {
    "capslock_tap_as_esc": true,
    "esc_tap_as_capslock": true,
    "swap_esc_and_capslock": false,
    "unmapped_passthrough": true,
    "show_tray_icon": true,
    "start_minimized": false
  },
  "layer": {
    "i": "up",
    "j": "left",
    "k": "down",
    "l": "right",
    "m": "delete",
    "n": "backspace",
    "p": "toggle_persistent",
    "w": ["alt", "f4"],
    "c": ["ctrl", "c"],
    "v": ["ctrl", "v"],
    "z": {
      "action": "exec",
      "command": "wt.exe"
    }
  },
  "shortcuts": {
    "win+alt+esc": {
      "action": "exec",
      "command": "shutdown.exe /s /t 0"
    },
    "win+shift+i": {
      "action": "exec",
      "command": "C:\\titus.lnk"
    }
  }
}
```

### Supported Action Formats
1. **Single Key Remap**: `"i": "up"`
2. **Key Combinations**: `"w": ["alt", "f4"]` or `"w": "alt+f4"`
3. **Application / Command Launch**:
   ```json
   "z": {
     "action": "exec",
     "command": "wt.exe"
   }
   ```
4. **Persistent Layer Toggle**: `"p": "toggle_persistent"`

---

## Installation & Setup

### Automated Setup
Run `setup.bat` (automatically requests Administrator elevation if not already elevated):
```cmd
setup.bat
```

This will automatically:
1. Copy `capslayer.exe`, `config.json` (preserves existing if present), `setup.bat`, and `uninstall.bat` to `C:\Program Files (x86)\capslayer`.
2. Configure file and folder permissions (`icacls` / ACL) so standard users can edit `config.json` without requiring administrator privileges.
3. Register a Windows Task Scheduler task (`CapsLayer`) to start elevated on user logon with **zero UAC prompts**.
4. Clean up any legacy registry Run entries or Startup folder shortcuts to avoid duplicate launches.
5. Add a shortcut to the Windows Start Menu (`CapsLayer.lnk`).
6. Launch the daemon in the background.

### Uninstallation
Run `uninstall.bat` (or `setup.bat /u`):
```cmd
uninstall.bat
```
Or via CLI:
```cmd
capslayer.exe --uninstall
```
This stops running processes, removes the Task Scheduler startup task, cleans Start Menu and Startup shortcuts, removes registry entries, and removes the installation folder.
## CLI Reference

```text
CapsLayer v1.0.0 - Fast Windows Keyboard Remapper & Layer Daemon

Usage: capslayer.exe [options]

Options:
  -c, --config <path>     Specify path to config.json
  -t, --test-config       Validate config file syntax and bindings, then exit
      --install           Install to 'C:\Program Files (x86)\capslayer' and enable startup
      --uninstall         Uninstall from 'C:\Program Files (x86)\capslayer' and remove startup
      --enable-startup    Register Task Scheduler startup task (elevated at logon)
      --disable-startup   Remove Task Scheduler startup task
      --status            Show installation and startup configuration status
      --console           Attach/allocate console for live diagnostic output
      --paused            Start daemon in paused (disabled) state
      --no-elevate        Skip auto-elevation to administrator
  -v, --version           Print version information and exit
  -h, --help              Display this help message and exit
```

---

## Building from Source

### Using Zig C Compiler
```powershell
# 1. Compile resource manifest
zig rc /fo res/resource.res res/resource.rc

# 2. Build CapsLayer executable
zig cc -O2 '-Wl,--subsystem,windows' -Isrc -D_CRT_SECURE_NO_WARNINGS `
  src/capslayer.c res/resource.res `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o capslayer.exe

# 3. Build and run unit tests
zig cc -O2 -Isrc -DCAPSLAYER_NO_MAIN -D_CRT_SECURE_NO_WARNINGS `
  tests/test_capslayer.c src/capslayer.c `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o test_capslayer.exe
.\test_capslayer.exe
```
---

## License

MIT License.
