# CapsLayer

**CapsLayer** is a lightweight, ultra-low-latency Windows keyboard remapper and modal layer daemon written in pure C using the Win32 API.

It turns **CapsLock** (or any configured modifier key such as **Right Alt**, **Left Ctrl**, etc.) into a high-performance dual-role modifier:
- **Tap Modifier Alone**: Acts as **Escape** (or CapsLock / original key depending on configuration).
- **Hold Modifier**: Activates a customizable navigation, editing, and shortcut layer (e.g. `Modifier + I/J/K/L` for Arrow keys).
- **Modifier + P**: Toggles **Persistent Layer Lock** mode with visual tray feedback and keyboard LED pulsing.
- **Direct Key Remapping**: Remap individual keys globally (e.g. swap `CapsLock` and `Escape` or remap arbitrary keys).
- **Global Shortcuts**: Launch applications and execute commands asynchronously without blocking input.

> 📖 **Looking for full internal architecture, file structure, and function-by-function call graphs?** Read **[DOCUMENTATION.md](DOCUMENTATION.md)**.

---

## Key Features

- **Zero Latency & Low Memory**: Written in pure C with Win32 low-level keyboard hooks (`WH_KEYBOARD_LL`). Zero external runtime dependencies.
- **Configurable Layer Modifier**:
  - Support for `CapsLock`, `Right Alt` (`AltGr`), `Left Ctrl`, `Right Ctrl`, `Left Alt`, `Windows Key`, or any arbitrary key.
- **Dual-Role Modifier & Isolated Tap**:
  - Quick tap sends `Escape` (or original modifier / toggled CapsLock based on settings).
  - Holding the modifier activates navigation layer bindings.
  - Transparent modifier passthrough when holding the modifier with unmapped keys (e.g. `Right Alt + Tab`).
- **Direct 1-to-1 Key Remapping (`remap`)**:
  - Easily swap or remap keys globally (e.g. `"capslock": "esc"`, `"esc": "capslock"`).
- **Persistent Layer Mode (Layer Lock)**:
  - Press `Modifier + P` to lock the navigation layer on.
  - Press `Escape` (or `P`) to unlock back to normal typing mode.
  - Pulses keyboard CapsLock LED and updates the tray icon while locked.
- **Live Config Hot-Reloading**:
  - Built-in directory watcher (`ReadDirectoryChangesW`) monitors `config.json` and hot-reloads bindings instantly upon saving (250ms debounced).
- **System Tray Integration**:
  - Status indicator icon: **Green (Active)**, **Cyan (Layer Locked)**, **Gray (Paused)**.
  - Double-click tray icon to toggle pause/active.
  - Context menu for Reloading Config, Editing Config, Status, and Exiting.
- **Windows Task Scheduler Startup**:
  - Installs to `C:\Program Files (x86)\capslayer` and registers with Task Scheduler to run elevated at user logon with **zero UAC prompts**.
- **Native Background Daemon**:
  - Runs quietly as a Windows GUI subsystem process (`IMAGE_SUBSYSTEM_WINDOWS_GUI`) without showing any console window.

---

## Default Layer Mappings (Hold Modifier)

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
    "modifier_key": "capslock",
    "capslock_tap_as_esc": true,
    "esc_tap_as_capslock": true,
    "swap_esc_and_capslock": false,
    "modifier_tap_as_esc": false,
    "unmapped_passthrough": true,
    "show_tray_icon": true,
    "start_minimized": false
  },
  "remap": {
    "capslock": "esc",
    "esc": "capslock"
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

### Configuration Options Reference

#### `settings`
| Option | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `modifier_key` | `string` | `"capslock"` | Layer modifier key. Accepts `"capslock"`, `"right alt"`, `"ralt"`, `"left ctrl"`, `"lctrl"`, `"left alt"`, `"rwin"`, etc. |
| `capslock_tap_as_esc` | `boolean` | `true` | When `true`, tapping CapsLock emits Escape. When using a custom modifier, automatically remaps CapsLock to Escape unless explicitly remapped. |
| `esc_tap_as_capslock` | `boolean` | `true` | When `true`, tapping Escape emits CapsLock. |
| `swap_esc_and_capslock` | `boolean` | `false` | When `true`, swaps Escape and CapsLock bidirectionally. |
| `modifier_tap_as_esc` | `boolean` | `false` | When `true`, tapping the custom modifier key alone emits Escape. |
| `unmapped_passthrough` | `boolean` | `true` | When `true`, unmapped keys pressed while holding the modifier pass through to Windows (transparently synthesizing the modifier key). |
| `show_tray_icon` | `boolean` | `true` | Displays the status icon in the Windows notification area. |
| `start_minimized` | `boolean` | `false` | Starts daemon without initial UI popups. |

#### `remap`
Direct 1-to-1 key mappings applied in normal typing mode.
```json
"remap": {
  "capslock": "esc",
  "esc": "capslock"
}
```

#### Supported Action Formats in `layer` and `shortcuts`
1. **Single Key Remap**: `"i": "up"`
2. **Key Combinations**: `"w": ["alt", "f4"]` or `"w": "alt+f4"`
3. **Application / Command Launch**:
   ```json
   "z": {
     "action": "exec",
     "command": "wt.exe"
   }
   ```
   Or shorthand for `.exe`, `.bat`, `.cmd`, or `.lnk` files: `"z": "wt.exe"`
4. **Persistent Layer Toggle**: `"p": "toggle_persistent"`

---

## Installation & Setup

### Automated Setup
Run `setup.bat` (automatically requests Administrator elevation if not already elevated):
```cmd
setup.bat
```

This will automatically:
1. Copy `capslayer.exe`, `config.json`, `setup.bat`, and `uninstall.bat` to `C:\Program Files (x86)\capslayer`.
2. Configure folder permissions (`icacls` / ACL) so standard users can edit `config.json` without administrator privileges.
3. Register a Windows Task Scheduler task (`CapsLayer`) to start elevated on user logon with **zero UAC prompts**.
4. Clean up any legacy registry Run entries or Startup folder shortcuts.
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
This terminates running processes, removes the Task Scheduler startup task, removes Start Menu and Startup shortcuts, cleans registry entries, and deletes the installation folder.

---

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
zig cc -O2 -Wl,--subsystem,windows -Isrc -D_CRT_SECURE_NO_WARNINGS `
  src/capslayer.c res/resource.res `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o capslayer.exe

# 3. Build and run unit tests
zig cc -O2 -Isrc -DCAPSLAYER_NO_MAIN -D_CRT_SECURE_NO_WARNINGS `
  tests/test_capslayer.c src/capslayer.c `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o test_capslayer.exe
.\test_capslayer.exe
```

### Using MSVC (Visual Studio Developer Command Prompt)
```cmd
:: 1. Compile resource
rc /fo res\resource.res res\resource.rc

:: 2. Build release binary
cl /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Isrc src\capslayer.c res\resource.res /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib gdi32.lib ole32.lib /OUT:capslayer.exe

:: 3. Build and run unit tests
cl /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DCAPSLAYER_NO_MAIN /Isrc tests\test_capslayer.c src\capslayer.c /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib advapi32.lib gdi32.lib ole32.lib /OUT:test_capslayer.exe
test_capslayer.exe
```

---

## License

MIT License.
