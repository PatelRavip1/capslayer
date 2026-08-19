# CapsLayer

**CapsLayer** is a lightweight, high-performance Windows keyboard remapper and modal layer daemon written in pure C using the Win32 API.

It transforms the **CapsLock** key into a dual-role key:
- **Tap CapsLock**: Acts as **Escape** (fast Vim-friendly escape).
- **Hold CapsLock**: Activates a customizable navigation, editing, and shortcut layer (e.g. `CapsLock + I/J/K/L` for Arrow keys).
- **CapsLock + P**: Toggles **Persistent Layer Lock** mode with visual tray feedback.
- **Global Shortcuts**: Launch applications and execute commands asynchronously without blocking input.

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
  - Installs to `C:\Program Files\capslayer` and registers with Task Scheduler to run elevated at user logon with zero UAC prompts.
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
| **`H`** | **Home** |
| **`;`** | **End** |
| **`U`** | **Page Up** |
| **`D`** | **Page Down** |
| **`Backspace`** / **`M`** | **Delete** |
| **`N`** | **Backspace** |
| **`P`** | **Toggle Persistent Layer Lock** |
| **`W`** | **Alt + F4** (Close Window) |
| **`C`** | **Ctrl + C** (Copy) |
| **`V`** | **Ctrl + V** (Paste) |
| **`Z`** | **Launch Windows Terminal** (`wt.exe`) |

---

## Configuration (`config.json`)

`config.json` resides next to `capslayer.exe` (or in `C:\Program Files\capslayer\config.json` after installation).

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
    "h": "home",
    ";": "end",
    "u": "pageup",
    "d": "pagedown",
    "backspace": "delete",
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
    "win+alt+capslock": {
      "action": "exec",
      "command": "C:\\Windows\\System32\\shutdown.exe /s /t 0"
    },
    "win+shift+i": {
      "action": "exec",
      "command": "wt.exe"
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

### Automatic Setup
Run `setup.bat` (or `install.bat`) as Administrator:
```cmd
setup.bat
```
This will:
1. Copy `capslayer.exe` and `config.json` to `C:\Program Files\capslayer`.
2. Configure permissions for user configuration edits.
3. Register a Windows Task Scheduler task (`CapsLayer`) to start elevated on user logon without UAC prompts.
4. Add a shortcut to the Windows Start Menu.
5. Launch the daemon in the background.

### Uninstallation
Run `uninstall.bat` as Administrator:
```cmd
uninstall.bat
```
Or via CLI:
```cmd
capslayer.exe --uninstall
```

---

## CLI Reference

```text
CapsLayer v1.0.0 - Fast Windows Keyboard Remapper & Layer Daemon

Usage: capslayer.exe [options]

Options:
  -c, --config <path>     Specify path to config.json
  -t, --test-config       Validate config file syntax and bindings, then exit
      --install           Install to 'C:\Program Files\capslayer' and enable startup
      --uninstall         Uninstall from 'C:\Program Files\capslayer' and remove startup
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
cd res
zig rc /fo resource.res resource.rc
cd ..

# 2. Build CapsLayer executable
zig cc -O2 '-Wl,--subsystem,windows' -Isrc -D_CRT_SECURE_NO_WARNINGS `
  src/main.c src/keys.c src/hook.c src/config.c src/tray.c src/cJSON.c res/resource.res `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o capslayer.exe

# 3. Build and run unit tests
zig cc -O2 -Isrc -D_CRT_SECURE_NO_WARNINGS `
  tests/test_capslayer.c src/keys.c src/config.c src/hook.c src/cJSON.c `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o test_capslayer.exe
.\test_capslayer.exe
```

---

## License

MIT License.
