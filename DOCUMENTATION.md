# CapsLayer: Comprehensive Architecture & Codebase Documentation

> **A human-understandable, complete technical guide to the CapsLayer keyboard daemon, covering architecture, design principles, file structure, and every function with full bidirectional call graphs.**

---

## Table of Contents

1. [Executive Summary & Mental Model](#1-executive-summary--mental-model)
2. [Project File Structure](#2-project-file-structure)
3. [System Architecture & Runtime Concurrency](#3-system-architecture--runtime-concurrency)
   - [3.1 The Life of a Key Event](#31-the-life-of-a-key-event)
   - [3.2 Concurrency & Threading Model](#32-concurrency--threading-model)
   - [3.3 Mutex & Single-Instance Guarantee](#33-mutex--single-instance-guarantee)
4. [Complete Function Reference & Call Graph](#4-complete-function-reference--call-graph)
   - [4.1 JSON Parser (Zero-Dependency)](#41-json-parser-zero-dependency)
   - [4.2 Key Translation & Input Synthesis](#42-key-translation--input-synthesis)
   - [4.3 Configuration Management](#43-configuration-management)
   - [4.4 Keyboard Hook & State Engine](#44-keyboard-hook--state-engine)
   - [4.5 System Tray & UI](#45-system-tray--ui)
   - [4.6 Process Lifecycle, Installation & CLI](#46-process-lifecycle-installation--cli)
   - [4.7 Unit Test Suite Functions](#47-unit-test-suite-functions)
5. [Scripts & Windows Automation](#5-scripts--windows-automation)
6. [Configuration Reference (`config.json`)](#6-configuration-reference-configjson)
7. [Building & Testing](#7-building--testing)

---

## 1. Executive Summary & Mental Model

**CapsLayer** is a lightweight, ultra-low-latency Windows background utility written in C. It transforms the physical **CapsLock** key into a multi-functional dual-role modifier:

```
                          ┌───────────────────────────┐
                          │   Physical CapsLock Key   │
                          └─────────────┬─────────────┘
                                        │
                 ┌──────────────────────┴──────────────────────┐
                 │                                             │
      [ Tapped alone (< 200ms) ]                    [ Held with other keys ]
                 │                                             │
                 ▼                                             ▼
       Emits ESCAPE Key                           Activates Navigation Layer:
    (or CapsLock if swapped)                     • Caps + I/J/K/L -> Up/Left/Down/Right
                                                 • Caps + H/;/U/D -> Home/End/PgUp/PgDn
                                                 • Caps + M/N     -> Delete/Backspace
                                                 • Caps + W/C/V   -> Alt+F4 / Ctrl+C / Ctrl+V
                                                 • Caps + P       -> Lock Persistent Layer
```

### Core Design Pillars
1. **Zero External Dependencies**: Implements its own compact JSON parser and Windows API bindings without external DLLs or runtime frameworks.
2. **Deterministic Single-Translation Unit**: Core functionality is encapsulated in `src/capslayer.c` and `src/capslayer.h` for fast builds and LTO (Link-Time Optimization).
3. **Anti-Recursion Safety**: All synthetic keystrokes injected via `SendInput()` are tagged with `MAGIC_INJECTED_FLAG` (`0xC4951A9E`), ensuring the low-level hook never intercepts its own events.
4. **Non-Blocking Execution**: External commands triggered by key combinations are executed asynchronously on dedicated background worker threads so typing latency is never blocked.
5. **Live Hot-Reloading**: A directory watcher monitoring `config.json` via `ReadDirectoryChangesW` reloads configuration on the fly without restarting the process.

---

## 2. Project File Structure

```
D:/new/
│
├── src/
│   ├── capslayer.h             # Public headers, data structures, constants, and API definitions
│   └── capslayer.c             # Complete implementation (hook, tray, JSON parser, daemon lifecycle)
│
├── res/
│   ├── resource.rc             # Windows resource script embedding application manifest & metadata
│   └── capslayer.manifest      # Win32 Application Manifest specifying DPI awareness and execution level
│
├── tests/
│   └── test_capslayer.c        # Unit test suite verifying key mappings, JSON parsing, and hook state logic
│
├── config.json                 # User configuration file (layer bindings, global hotkeys, settings)
├── setup.bat                   # Automated installer script (auto-elevates, deploys, configures Task Scheduler)
├── uninstall.bat               # Automated uninstaller script (removes files, shortcuts, and scheduled tasks)
├── capslayer.exe               # Pre-compiled high-performance Windows binary
├── .gitignore                  # Git ignore rules for build artifacts and intermediate files
├── README.md                   # Quick-start documentation and general project overview
└── DOCUMENTATION.md            # Comprehensive architecture and codebase explanation (this document)
```

### File Details

| File Path | Role & Purpose |
|---|---|
| `src/capslayer.h` | Defines public types (`capslayer_config_t`, `layer_action_t`, `global_shortcut_t`), action enums, Win32 constants, and API prototypes. |
| `src/capslayer.c` | Contains the entire application logic organized into 6 modules: JSON parser, Key resolver, Configuration loader, Keyboard hook engine, System tray GUI, and Main daemon loop. |
| `res/resource.rc` | Windows resource description compiled by `zig rc` or `rc.exe` into binary `.res` format. |
| `res/capslayer.manifest` | Declares compatibility with Windows 7 through Windows 11 and sets `requestedExecutionLevel` to `asInvoker`. |
| `tests/test_capslayer.c` | Autonomous test runner with 132 test assertions verifying key parsing, file loading, JSON correctness, and hook behavior. |
| `config.json` | JSON configuration containing default navigation bindings (`I/J/K/L`), shortcut commands (`win+alt+esc`), and timing options. |
| `setup.bat` | Command-line script with UAC auto-elevation that installs CapsLayer to `%ProgramFiles(x86)%\capslayer`, registers an elevated logon task in Task Scheduler, creates Start Menu shortcuts, and launches the daemon. |
| `uninstall.bat` | Cleans up all registry keys, Task Scheduler entries, Start Menu shortcuts, and deletes the installation directory. |

---

## 3. System Architecture & Runtime Concurrency

### 3.1 The Life of a Key Event

```
 [ Hardware Keyboard Event ]
              │
              ▼
   [ Windows OS Subsystem ]
              │
              ▼ (WH_KEYBOARD_LL)
 ┌─────────────────────────────────────────────────────────────┐
 │                LowLevelKeyboardProc()                       │
 └────────────────────────────┬────────────────────────────────┘
                              │
    Is event injected or is hook paused?
         ├── YES ──► CallNextHookEx() [Pass through to OS]
         └── NO
              │
    Is event matching a Global Shortcut (e.g. Win+Alt+Esc)?
         ├── YES ──► [Consume Event] ──► spawn_process_async() / send_key_combo()
         └── NO
              │
    Is physical CapsLock event?
         ├── KeyDown ──► Suppress CapsLock, set g_capslock_down = true
         └── KeyUp   ──► If unused as layer modifier, emit ESCAPE (send_key_tap)
              │
    Is Layer Active (CapsLock held OR Persistent Lock active)?
         ├── Mapping Found (e.g. 'I') ──► [Consume Event] ──► send_key_event(VK_UP)
         ├── Toggle Lock Key ('P')    ──► [Consume Event] ──► hook_toggle_persistent_layer()
         └── Unmapped Key             ──► Pass through (if unmapped_passthrough is true)
```

---

### 3.2 Concurrency & Threading Model

CapsLayer utilizes a multi-threaded architecture to ensure zero dropped keystrokes and instantaneous response times:

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           CapsLayer Process                              │
│                                                                          │
│  ┌─────────────────────────┐           ┌──────────────────────────────┐  │
│  │    Main Thread (UI)     │           │    Config Watcher Thread     │  │
│  │ ─────────────────────── │           │ ──────────────────────────── │  │
│  │ • Win32 Message Loop    │           │ • ReadDirectoryChangesW()    │  │
│  │ • WH_KEYBOARD_LL Hook   │◄──PostMsg─┤ • Monitors config.json edits │  │
│  │ • System Tray GUI Menu  │           │ • Debounced auto-reload      │  │
│  └─────────────────────────┘           └──────────────────────────────┘  │
│                                                                          │
│  ┌─────────────────────────┐           ┌──────────────────────────────┐  │
│  │  Persistent Blink Thread│           │ Async Process Spawner Pool   │  │
│  │ ─────────────────────── │           │ ──────────────────────────── │  │
│  │ • Toggles CapsLock LED  │           │ • Short-lived worker threads │  │
│  │ • Visual feedback when  │           │ • Spawns external apps/cmds  │  │
│  │   Layer Lock is active  │           │ • Zero lag on main hook      │  │
│  └─────────────────────────┘           └──────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

1. **Main UI & Hook Thread**: Handles the Win32 message pump (`GetMessageW`/`DispatchMessageW`), receives low-level keyboard input notifications directly from the OS, and manages the notification area (system tray) icon.
2. **Config Watcher Thread (`config_watcher`)**: Uses `ReadDirectoryChangesW` in an overlapped asynchronous loop to monitor file modifications in the directory containing `config.json`. When a change is detected and debounced (250ms), it sends `WM_USER_RELOAD_CONFIG` to the main window.
3. **Blinker Thread (`blink_worker`)**: When Persistent Layer Lock is toggled on (`Caps + P`), this thread pulses the keyboard CapsLock LED at 1-second intervals to visually indicate that navigation mode is locked on.
4. **Async Process Workers (`async_exec_worker`)**: Spawns external commands (such as Windows Terminal `wt.exe` or custom scripts) in detached background threads using `CreateProcessW` / `ShellExecuteExW`.

---

### 3.3 Mutex & Single-Instance Guarantee

To avoid multiple hook instances conflicting with each other, CapsLayer creates a named Win32 Mutex:
`MUTEX_NAME = L"CapsLayer_SingleInstance_Mutex"`

If `CreateMutexW` returns `ERROR_ALREADY_EXISTS`, the newly launched process immediately exits cleanly.

---

## 4. Complete Function Reference & Call Graph

Below is the complete catalog of every function in the project, categorized by subsystem, with explanation and bidirectional markdown links.

---

### Quick Function Index

| Module | Functions |
|---|---|
| **JSON Parser** | [`skip_ws`](#skip_ws), [`parse_str`](#parse_str), [`jnode_free`](#jnode_free), [`parse_val`](#parse_val), [`parse_arr`](#parse_arr), [`parse_obj`](#parse_obj), [`jget`](#jget), [`jget_str`](#jget_str), [`jget_bool`](#jget_bool) |
| **Key Translation** | [`key_name_to_vk`](#key_name_to_vk), [`is_extended_key`](#is_extended_key), [`is_modifier_key`](#is_modifier_key), [`is_modifier_down`](#is_modifier_down), [`send_key_event`](#send_key_event), [`send_key_tap`](#send_key_tap), [`send_key_combo`](#send_key_combo), [`async_exec_worker`](#async_exec_worker), [`spawn_process_async`](#spawn_process_async) |
| **Configuration** | [`is_toggle_persistent_name`](#is_toggle_persistent_name), [`parse_combo_str`](#parse_combo_str), [`parse_layer_action_node`](#parse_layer_action_node), [`parse_shortcut_combo`](#parse_shortcut_combo), [`config_init_defaults`](#config_init_defaults), [`config_load_from_json_string`](#config_load_from_json_string), [`config_load_from_file`](#config_load_from_file), [`config_get_default_path`](#config_get_default_path), [`config_get_dir_path`](#config_get_dir_path) |
| **Keyboard Hook Engine** | [`init_cs_once`](#init_cs_once), [`hook_update_config`](#hook_update_config), [`get_config_snapshot`](#get_config_snapshot), [`hook_is_installed`](#hook_is_installed), [`hook_is_paused`](#hook_is_paused), [`hook_is_persistent_layer`](#hook_is_persistent_layer), [`blink_worker`](#blink_worker), [`start_blinker`](#start_blinker), [`stop_blinker`](#stop_blinker), [`release_injected_keys`](#release_injected_keys), [`hook_set_paused`](#hook_set_paused), [`hook_set_persistent_layer`](#hook_set_persistent_layer), [`hook_toggle_persistent_layer`](#hook_toggle_persistent_layer), [`hook_set_state_callback`](#hook_set_state_callback), [`LowLevelKeyboardProc`](#lowlevelkeyboardproc), [`hook_install`](#hook_install), [`hook_uninstall`](#hook_uninstall) |
| **System Tray GUI** | [`create_status_icon`](#create_status_icon), [`tray_init`](#tray_init), [`tray_update_status`](#tray_update_status), [`tray_show_menu`](#tray_show_menu), [`tray_cleanup`](#tray_cleanup) |
| **Daemon & Lifecycle** | [`log_msg`](#log_msg), [`run_cmd_hidden`](#run_cmd_hidden), [`get_prog_files`](#get_prog_files), [`is_admin`](#is_admin), [`relaunch_admin`](#relaunch_admin), [`setup_enable_startup`](#setup_enable_startup), [`setup_disable_startup`](#setup_disable_startup), [`setup_install`](#setup_install), [`setup_uninstall`](#setup_uninstall), [`reload_config`](#reload_config), [`config_watcher`](#config_watcher), [`on_layer_state`](#on_layer_state), [`MainWndProc`](#mainwndproc), [`attach_console`](#attach_console), [`main`](#main), [`WinMain`](#winmain) |
| **Test Suite** | [`test_key_name_to_vk`](#test_key_name_to_vk), [`test_is_extended_key`](#test_is_extended_key), [`test_config_defaults`](#test_config_defaults), [`test_config_json_parsing`](#test_config_json_parsing), [`test_config_file_loading`](#test_config_file_loading), [`test_path_helpers`](#test_path_helpers), [`test_hook_state_management`](#test_hook_state_management) |

---

### 4.1 JSON Parser (Zero-Dependency)

#### `skip_ws`
- **Signature**: `static const char *skip_ws(const char *s)`
- **Visibility**: `static` (internal to `src/capslayer.c`)
- **Description**: Advances the string pointer past whitespace characters (spaces, tabs, newlines, carriage returns) and C/C++ style comments (`// line comment` and `/* block comment */`).
- **Called By**: [`parse_arr`](#parse_arr), [`parse_obj`](#parse_obj), [`parse_val`](#parse_val)
- **Calls**: Standard C character checks.

#### `parse_str`
- **Signature**: `static char *parse_str(const char **ps)`
- **Visibility**: `static`
- **Description**: Parses a double-quoted JSON string token, unescaping JSON escape sequences (`\n`, `\r`, `\t`, `\"`, `\\`, `\/`, `\b`, `\f`, and `\uXXXX` 4-digit hex unicode sequences). Dynamically grows heap buffer as needed.
- **Called By**: [`parse_obj`](#parse_obj), [`parse_val`](#parse_val)
- **Calls**: `malloc`, `realloc`, `isdigit`, `tolower`, `free`.

#### `jnode_free`
- **Signature**: `static void jnode_free(jnode_t *node)`
- **Visibility**: `static`
- **Description**: Recursively frees all heap memory allocated by a JSON AST node (object keys, string contents, array elements, and child object key-value pairs).
- **Called By**: [`parse_arr`](#parse_arr), [`parse_obj`](#parse_obj), [`config_load_from_json_string`](#config_load_from_json_string), [`jnode_free`](#jnode_free) (recursive)
- **Calls**: [`jnode_free`](#jnode_free) (recursive), `free`.

#### `parse_val`
- **Signature**: `static bool parse_val(const char **ps, jnode_t *out)`
- **Visibility**: `static`
- **Description**: Dispatches parsing for any JSON value: objects (`{}`), arrays (`[]`), strings (`""`), booleans (`true`/`false`), nulls (`null`), or floating-point/integer numbers.
- **Called By**: [`parse_arr`](#parse_arr), [`parse_obj`](#parse_obj), [`config_load_from_json_string`](#config_load_from_json_string)
- **Calls**: [`skip_ws`](#skip_ws), [`parse_obj`](#parse_obj), [`parse_arr`](#parse_arr), [`parse_str`](#parse_str), `strncmp`, `strtod`, `isdigit`.

#### `parse_arr`
- **Signature**: `static bool parse_arr(const char **ps, jnode_t *out)`
- **Visibility**: `static`
- **Description**: Parses a JSON array `[ item1, item2, ... ]` into a dynamic array of `jnode_t` elements.
- **Called By**: [`parse_val`](#parse_val)
- **Calls**: [`skip_ws`](#skip_ws), [`parse_val`](#parse_val), [`jnode_free`](#jnode_free), `calloc`, `realloc`.

#### `parse_obj`
- **Signature**: `static bool parse_obj(const char **ps, jnode_t *out)`
- **Visibility**: `static`
- **Description**: Parses a JSON object `{ "key": value, ... }` into key-value pairs stored in a dynamically allocated array.
- **Called By**: [`parse_val`](#parse_val)
- **Calls**: [`skip_ws`](#skip_ws), [`parse_str`](#parse_str), [`parse_val`](#parse_val), [`jnode_free`](#jnode_free), `calloc`, `realloc`, `free`.

#### `jget`
- **Signature**: `static jnode_t *jget(const jnode_t *obj, const char *key)`
- **Visibility**: `static`
- **Description**: Performs a case-insensitive key lookup on a JSON object node and returns a pointer to the matching child node, or `NULL` if not found.
- **Called By**: [`jget_str`](#jget_str), [`jget_bool`](#jget_bool), [`parse_layer_action_node`](#parse_layer_action_node), [`config_load_from_json_string`](#config_load_from_json_string)
- **Calls**: `_stricmp`.

#### `jget_str`
- **Signature**: `static const char *jget_str(const jnode_t *obj, const char *key)`
- **Visibility**: `static`
- **Description**: Helper that queries a key in a JSON object and returns its string pointer if the node is of type `JSON_STRING`.
- **Called By**: [`parse_layer_action_node`](#parse_layer_action_node), [`config_load_from_json_string`](#config_load_from_json_string)
- **Calls**: [`jget`](#jget).

#### `jget_bool`
- **Signature**: `static bool jget_bool(const jnode_t *obj, const char *key, bool def)`
- **Visibility**: `static`
- **Description**: Helper that queries a key in a JSON object and returns its boolean value, falling back to `def` if the key does not exist or is not a boolean.
- **Called By**: [`config_load_from_json_string`](#config_load_from_json_string)
- **Calls**: [`jget`](#jget).

---

### 4.2 Key Translation & Input Synthesis

#### `key_name_to_vk`
- **Signature**: `WORD key_name_to_vk(const char *name)`
- **Visibility**: `extern` (Declared in `capslayer.h`)
- **Description**: Converts human-readable key names (e.g., `"up"`, `"escape"`, `"f5"`, `"ctrl"`, `";"`, `"volume_up"`) to Windows Virtual Key codes (`VK_*`). Handles single alphanumeric letters, function keys `f1`-`f24`, predefined alias tables, and falls back to `VkKeyScanW` for OEM keyboard layouts.
- **Called By**: [`parse_combo_str`](#parse_combo_str), [`parse_layer_action_node`](#parse_layer_action_node), [`parse_shortcut_combo`](#parse_shortcut_combo), [`config_init_defaults`](#config_init_defaults), [`config_load_from_json_string`](#config_load_from_json_string), [`test_key_name_to_vk`](#test_key_name_to_vk), [`test_config_defaults`](#test_config_defaults), [`test_config_json_parsing`](#test_config_json_parsing), [`test_config_file_loading`](#test_config_file_loading)
- **Calls**: `tolower`, `isdigit`, `atoi`, `_stricmp`, Win32 `MultiByteToWideChar`, Win32 `VkKeyScanW`.

#### `is_extended_key`
- **Signature**: `bool is_extended_key(WORD vk)`
- **Visibility**: `extern`
- **Description**: Determines whether a Virtual Key code requires the `KEYEVENTF_EXTENDEDKEY` flag in `INPUT` structures (e.g. arrow keys, navigation cluster, right Ctrl/Alt, multimedia keys, Windows keys).
- **Called By**: [`send_key_event`](#send_key_event), [`send_key_tap`](#send_key_tap), [`send_key_combo`](#send_key_combo), [`test_is_extended_key`](#test_is_extended_key)
- **Calls**: Switch lookup table.

#### `is_modifier_key`
- **Signature**: `bool is_modifier_key(WORD vk)`
- **Visibility**: `extern`
- **Description**: Returns `true` if the specified Virtual Key is a modifier key (Shift, Ctrl, Alt, or Win), including left/right variants.
- **Called By**: [`parse_shortcut_combo`](#parse_shortcut_combo), [`config_load_from_json_string`](#config_load_from_json_string), [`test_is_extended_key`](#test_is_extended_key)
- **Calls**: Switch lookup table.

#### `is_modifier_down`
- **Signature**: `bool is_modifier_down(WORD vk)`
- **Visibility**: `extern`
- **Description**: Checks the physical asynchronous state of a modifier key using `GetAsyncKeyState()`, checking generic and left/right variants simultaneously.
- **Called By**: [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)
- **Calls**: Win32 `GetAsyncKeyState`.

#### `send_key_event`
- **Signature**: `void send_key_event(WORD vk, bool is_down)`
- **Visibility**: `extern`
- **Description**: Emits a single key-down or key-up event to the Windows OS input stream via `SendInput()`. Computes the hardware scan code via `MapVirtualKeyW` and tags the event with `MAGIC_INJECTED_FLAG`.
- **Called By**: [`release_injected_keys`](#release_injected_keys), [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)
- **Calls**: Win32 `MapVirtualKeyW`, [`is_extended_key`](#is_extended_key), Win32 `SendInput`.

#### `send_key_tap`
- **Signature**: `void send_key_tap(WORD vk)`
- **Visibility**: `extern`
- **Description**: Emits an atomic key press and release (key down followed immediately by key up) in a single `SendInput()` batch call.
- **Called By**: [`blink_worker`](#blink_worker), [`stop_blinker`](#stop_blinker), [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)
- **Calls**: Win32 `MapVirtualKeyW`, [`is_extended_key`](#is_extended_key), Win32 `SendInput`.

#### `send_key_combo`
- **Signature**: `void send_key_combo(const WORD *vks, size_t count)`
- **Visibility**: `extern`
- **Description**: Synthesizes a key combination (e.g. `Alt + F4` or `Ctrl + Shift + Esc`). Presses keys in forward sequence (`0 .. N-1`) and releases them in reverse order (`N-1 .. 0`) within a single atomic `SendInput()` packet.
- **Called By**: [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)
- **Calls**: Win32 `ZeroMemory`, `MapVirtualKeyW`, [`is_extended_key`](#is_extended_key), Win32 `SendInput`.

#### `async_exec_worker`
- **Signature**: `static unsigned __stdcall async_exec_worker(void *arg)`
- **Visibility**: `static`
- **Description**: Background worker thread entry point that executes external application commands. Attempts process execution via `CreateProcessW` (with `CREATE_NO_WINDOW`); if direct execution fails, delegates to `ShellExecuteExW` to support document shortcuts and shell extensions. Frees command string upon completion.
- **Called By**: [`spawn_process_async`](#spawn_process_async) (via `_beginthreadex`)
- **Calls**: `_wcsicmp`, `wcscpy_s`, Win32 `CreateProcessW`, Win32 `ShellExecuteExW`, Win32 `CloseHandle`, `free`.

#### `spawn_process_async`
- **Signature**: `void spawn_process_async(const char *cmd_utf8)`
- **Visibility**: `extern`
- **Description**: Converts a UTF-8 command string to UTF-16 and launches it asynchronously on a background worker thread (`async_exec_worker`) so the low-level hook thread is never blocked.
- **Called By**: [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)
- **Calls**: Win32 `MultiByteToWideChar`, `malloc`, `_beginthreadex` (spawns [`async_exec_worker`](#async_exec_worker)), Win32 `CloseHandle`, `free`.

---

### 4.3 Configuration Management

#### `is_toggle_persistent_name`
- **Signature**: `static bool is_toggle_persistent_name(const char *s)`
- **Visibility**: `static`
- **Description**: Checks if an action string matches any alias for toggling the persistent layer (`"toggle_persistent"`, `"toggle_lock"`, `"lock"`, `"persistent"`, `"toggle"`, etc.).
- **Called By**: [`parse_layer_action_node`](#parse_layer_action_node)
- **Calls**: `_stricmp`.

#### `parse_combo_str`
- **Signature**: `static bool parse_combo_str(const char *str, layer_action_t *action)`
- **Visibility**: `static`
- **Description**: Parses a plus-delimited combo string (e.g. `"ctrl+shift+esc"`, `"alt+f4"`) into an array of Virtual Key codes. If only one key is present, sets action to `ACTION_KEY`; otherwise sets `ACTION_COMBO`.
- **Called By**: [`parse_layer_action_node`](#parse_layer_action_node)
- **Calls**: `strncpy_s`, `strtok_s`, `isspace`, `strlen`, [`key_name_to_vk`](#key_name_to_vk), `memcpy`.

#### `parse_layer_action_node`
- **Signature**: `static bool parse_layer_action_node(const jnode_t *node, layer_action_t *action)`
- **Visibility**: `static`
- **Description**: Parses a JSON node representing a layer action. Handles string format (`"up"`, `"alt+f4"`, `"wt.exe"`), array format (`["ctrl", "c"]`), and rich object format (`{ "action": "exec", "command": "calc.exe" }`).
- **Called By**: [`parse_layer_action_node`](#parse_layer_action_node) (recursive), [`config_load_from_json_string`](#config_load_from_json_string)
- **Calls**: [`is_toggle_persistent_name`](#is_toggle_persistent_name), `strchr`, [`parse_combo_str`](#parse_combo_str), [`key_name_to_vk`](#key_name_to_vk), `strstr`, `strncpy_s`, [`parse_layer_action_node`](#parse_layer_action_node) (recursive), [`jget_str`](#jget_str), [`jget`](#jget), `_stricmp`.

#### `parse_shortcut_combo`
- **Signature**: `static bool parse_shortcut_combo(const char *combo_str, global_shortcut_t *sc)`
- **Visibility**: `static`
- **Description**: Parses a global shortcut key definition (e.g. `"win+alt+esc"`). Identifies which key is the trigger key (non-modifier) and which keys are modifier requirements (`mod_count` up to 4).
- **Called By**: [`config_load_from_json_string`](#config_load_from_json_string)
- **Calls**: `memset`, `strncpy_s`, `strtok_s`, `isspace`, `strlen`, [`key_name_to_vk`](#key_name_to_vk), [`is_modifier_key`](#is_modifier_key).

#### `config_init_defaults`
- **Signature**: `void config_init_defaults(capslayer_config_t *cfg)`
- **Visibility**: `extern`
- **Description**: Initializes configuration structure with built-in default settings (`capslock_tap_as_esc = true`, `unmapped_passthrough = true`, `I/J/K/L -> Up/Left/Down/Right`, `H/;/U/D -> Home/End/PgUp/PgDn`, `M/N -> Del/Bksp`, `P -> toggle_persistent`, `W -> Alt+F4`, `C/V -> Ctrl+C/Ctrl+V`, `Z -> wt.exe`).
- **Called By**: [`config_load_from_json_string`](#config_load_from_json_string), [`reload_config`](#reload_config), [`test_config_defaults`](#test_config_defaults), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: `memset`, [`key_name_to_vk`](#key_name_to_vk), `strncpy_s`.

#### `config_load_from_json_string`
- **Signature**: `bool config_load_from_json_string(const char *json_str, capslayer_config_t *cfg)`
- **Visibility**: `extern`
- **Description**: Parses a JSON configuration string into `capslayer_config_t`. Overrides defaults with custom settings, layer bindings, and global shortcuts.
- **Called By**: [`config_load_from_file`](#config_load_from_file), [`test_config_json_parsing`](#test_config_json_parsing)
- **Calls**: [`parse_val`](#parse_val), [`jnode_free`](#jnode_free), [`config_init_defaults`](#config_init_defaults), [`jget`](#jget), [`jget_bool`](#jget_bool), [`jget_str`](#jget_str), [`key_name_to_vk`](#key_name_to_vk), [`parse_layer_action_node`](#parse_layer_action_node), [`parse_shortcut_combo`](#parse_shortcut_combo), [`is_modifier_key`](#is_modifier_key).

#### `config_load_from_file`
- **Signature**: `bool config_load_from_file(const char *path, capslayer_config_t *cfg)`
- **Visibility**: `extern`
- **Description**: Reads a JSON configuration file from disk into a memory buffer and parses it via [`config_load_from_json_string`](#config_load_from_json_string).
- **Called By**: [`reload_config`](#reload_config), [`main`](#main), [`test_config_file_loading`](#test_config_file_loading)
- **Calls**: `fopen_s`, `fseek`, `ftell`, `malloc`, `fread`, `fclose`, [`config_load_from_json_string`](#config_load_from_json_string), `free`.

#### `config_get_default_path`
- **Signature**: `bool config_get_default_path(char *buf, size_t size)`
- **Visibility**: `extern`
- **Description**: Resolves the full path to `config.json` in the same directory as the currently running `capslayer.exe` binary via `GetModuleFileNameA`.
- **Called By**: [`main`](#main), [`test_path_helpers`](#test_path_helpers)
- **Calls**: Win32 `GetModuleFileNameA`, `strrchr`, `strncpy_s`, `snprintf`.

#### `config_get_dir_path`
- **Signature**: `bool config_get_dir_path(const char *cfg_path, char *dir_buf, size_t dir_size)`
- **Visibility**: `extern`
- **Description**: Extracts the directory path containing `config.json` for filesystem change monitoring.
- **Called By**: [`main`](#main), [`test_path_helpers`](#test_path_helpers)
- **Calls**: `strncpy_s`, `strrchr`.

---

### 4.4 Keyboard Hook & State Engine

#### `init_cs_once`
- **Signature**: `static void init_cs_once(void)`
- **Visibility**: `static`
- **Description**: Thread-safe initialization of the Win32 `CRITICAL_SECTION` protecting active configuration reads/writes.
- **Called By**: [`hook_update_config`](#hook_update_config), [`get_config_snapshot`](#get_config_snapshot), [`hook_install`](#hook_install)
- **Calls**: Win32 `InitializeCriticalSection`.

#### `hook_update_config`
- **Signature**: `void hook_update_config(const capslayer_config_t *cfg)`
- **Visibility**: `extern`
- **Description**: Thread-safely replaces the runtime active configuration inside the critical section.
- **Called By**: [`reload_config`](#reload_config), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: [`init_cs_once`](#init_cs_once), Win32 `EnterCriticalSection`, Win32 `LeaveCriticalSection`.

#### `get_config_snapshot`
- **Signature**: `static void get_config_snapshot(capslayer_config_t *out)`
- **Visibility**: `static`
- **Description**: Takes a thread-safe snapshot copy of the active configuration to prevent data races while processing hook events.
- **Called By**: [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)
- **Calls**: [`init_cs_once`](#init_cs_once), Win32 `EnterCriticalSection`, Win32 `LeaveCriticalSection`.

#### `hook_is_installed`
- **Signature**: `bool hook_is_installed(void)`
- **Visibility**: `extern`
- **Description**: Returns `true` if the Windows low-level keyboard hook handle (`HHOOK`) is active.
- **Called By**: External diagnostic queries.
- **Calls**: None.

#### `hook_is_paused`
- **Signature**: `bool hook_is_paused(void)`
- **Visibility**: `extern`
- **Description**: Queries whether CapsLayer remapping is currently paused.
- **Called By**: [`tray_show_menu`](#tray_show_menu), [`MainWndProc`](#mainwndproc), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: None.

#### `hook_is_persistent_layer`
- **Signature**: `bool hook_is_persistent_layer(void)`
- **Visibility**: `extern`
- **Description**: Queries whether Persistent Layer Lock mode (`Caps + P`) is currently engaged.
- **Called By**: [`tray_show_menu`](#tray_show_menu), [`MainWndProc`](#mainwndproc), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: None.

#### `blink_worker`
- **Signature**: `static unsigned __stdcall blink_worker(void *arg)`
- **Visibility**: `static`
- **Description**: Background thread procedure that pulses the keyboard CapsLock LED by sending synthetic CapsLock taps every 1000ms while Persistent Layer Lock is active. Restores original LED state on termination.
- **Called By**: [`start_blinker`](#start_blinker) (via `_beginthreadex`)
- **Calls**: Win32 `GetKeyState`, [`send_key_tap`](#send_key_tap), Win32 `WaitForSingleObject`.

#### `start_blinker`
- **Signature**: `static void start_blinker(void)`
- **Visibility**: `static`
- **Description**: Spawns the blinker thread when Persistent Layer Lock is enabled.
- **Called By**: [`hook_set_persistent_layer`](#hook_set_persistent_layer)
- **Calls**: Win32 `GetKeyState`, `CreateEventW`, `ResetEvent`, `_beginthreadex` (spawns [`blink_worker`](#blink_worker)).

#### `stop_blinker`
- **Signature**: `static void stop_blinker(void)`
- **Visibility**: `static`
- **Description**: Signals the stop event to terminate `blink_worker` and waits up to 1.5 seconds for clean thread exit.
- **Called By**: [`hook_set_paused`](#hook_set_paused), [`hook_set_persistent_layer`](#hook_set_persistent_layer), [`hook_uninstall`](#hook_uninstall)
- **Calls**: Win32 `SetEvent`, `WaitForSingleObject`, `CloseHandle`, `GetKeyState`, [`send_key_tap`](#send_key_tap).

#### `release_injected_keys`
- **Signature**: `static void release_injected_keys(void)`
- **Visibility**: `static`
- **Description**: Iterates through the `g_active_injected` array and sends key-up events for any synthetic keys currently held down to prevent stuck keys when switching layers or pausing.
- **Called By**: [`hook_set_paused`](#hook_set_paused), [`hook_set_persistent_layer`](#hook_set_persistent_layer), [`LowLevelKeyboardProc`](#lowlevelkeyboardproc), [`hook_uninstall`](#hook_uninstall)
- **Calls**: [`send_key_event`](#send_key_event).

#### `hook_set_paused`
- **Signature**: `void hook_set_paused(bool paused)`
- **Visibility**: `extern`
- **Description**: Pauses or resumes CapsLayer remapping. When paused, stops the LED blinker, releases held synthetic keys, and fires the state change callback.
- **Called By**: [`MainWndProc`](#mainwndproc), [`main`](#main), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: [`stop_blinker`](#stop_blinker), [`release_injected_keys`](#release_injected_keys), callback `g_state_cb`.

#### `hook_set_persistent_layer`
- **Signature**: `void hook_set_persistent_layer(bool active)`
- **Visibility**: `extern`
- **Description**: Sets the Persistent Layer Lock state. Starts/stops the LED blinker and notifies the registered state callback.
- **Called By**: [`hook_toggle_persistent_layer`](#hook_toggle_persistent_layer), [`LowLevelKeyboardProc`](#lowlevelkeyboardproc), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: [`start_blinker`](#start_blinker), [`stop_blinker`](#stop_blinker), [`release_injected_keys`](#release_injected_keys), callback `g_state_cb`.

#### `hook_toggle_persistent_layer`
- **Signature**: `bool hook_toggle_persistent_layer(void)`
- **Visibility**: `extern`
- **Description**: Toggles the Persistent Layer Lock state between active and inactive.
- **Called By**: [`LowLevelKeyboardProc`](#lowlevelkeyboardproc), [`MainWndProc`](#mainwndproc), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: [`hook_set_persistent_layer`](#hook_set_persistent_layer).

#### `hook_set_state_callback`
- **Signature**: `void hook_set_state_callback(layer_state_callback_t cb)`
- **Visibility**: `extern`
- **Description**: Registers a callback function invoked whenever the pause status or persistent lock status changes.
- **Called By**: [`main`](#main), [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: None.

#### `LowLevelKeyboardProc`
- **Signature**: `LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)`
- **Visibility**: `extern`
- **Description**: The core low-level Windows keyboard hook callback (`WH_KEYBOARD_LL`). Intercepts hardware keystrokes before any application sees them:
  1. Filters injected events to prevent infinite loops.
  2. Evaluates global shortcuts (e.g. `Win+Alt+Esc`).
  3. Manages physical Escape remapping / persistent layer exit.
  4. Manages physical CapsLock press/release (tracks hold vs tap for dual-role Escape).
  5. Translates layer keys (e.g. `I/J/K/L -> Up/Left/Down/Right`) and tracks active keys to guarantee clean key-up delivery.
- **Called By**: Windows OS Hook Subsystem, [`test_hook_state_management`](#test_hook_state_management)
- **Calls**: Win32 `CallNextHookEx`, [`get_config_snapshot`](#get_config_snapshot), [`is_modifier_down`](#is_modifier_down), [`spawn_process_async`](#spawn_process_async), [`send_key_combo`](#send_key_combo), [`send_key_tap`](#send_key_tap), [`hook_toggle_persistent_layer`](#hook_toggle_persistent_layer), [`hook_set_persistent_layer`](#hook_set_persistent_layer), [`release_injected_keys`](#release_injected_keys), [`send_key_event`](#send_key_event).

#### `hook_install`
- **Signature**: `bool hook_install(HINSTANCE hInstance)`
- **Visibility**: `extern`
- **Description**: Installs the global low-level keyboard hook (`SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, ...)`).
- **Called By**: [`main`](#main)
- **Calls**: [`init_cs_once`](#init_cs_once), Win32 `SetWindowsHookExW` (registers [`LowLevelKeyboardProc`](#lowlevelkeyboardproc)).

#### `hook_uninstall`
- **Signature**: `void hook_uninstall(void)`
- **Visibility**: `extern`
- **Description**: Removes the global low-level keyboard hook via `UnhookWindowsHookEx`, stops the LED blinker, and releases any held keys.
- **Called By**: [`main`](#main)
- **Calls**: [`stop_blinker`](#stop_blinker), Win32 `UnhookWindowsHookEx`, [`release_injected_keys`](#release_injected_keys).

---

### 4.5 System Tray & UI

#### `create_status_icon`
- **Signature**: `static HICON create_status_icon(COLORREF bg_color, COLORREF fg_color, const wchar_t *letter)`
- **Visibility**: `static`
- **Description**: Dynamically generates high-DPI compatible 32-bit RGBA tray icons in memory using Win32 GDI (green **C** for Active, cyan **L** for Layer Locked, gray **C** for Paused).
- **Called By**: [`tray_init`](#tray_init)
- **Calls**: Win32 `GetSystemMetrics`, `GetDC`, `CreateCompatibleDC`, `CreateDIBSection`, `CreateBitmap`, `SelectObject`, `CreateSolidBrush`, `FillRect`, `DeleteObject`, `GetStockObject`, `SetBkMode`, `SetTextColor`, `CreateFontW`, `DrawTextW`, `DeleteDC`, `ReleaseDC`, `CreateIconIndirect`.

#### `tray_init`
- **Signature**: `bool tray_init(HWND hwnd, HINSTANCE hInstance)`
- **Visibility**: `extern`
- **Description**: Initializes dynamic tray icons and creates the taskbar notification icon via `Shell_NotifyIconW(NIM_ADD, ...)`.
- **Called By**: [`main`](#main)
- **Calls**: [`create_status_icon`](#create_status_icon), Win32 `ZeroMemory`, `LoadIcon`, `wcscpy_s`, `Shell_NotifyIconW`.

#### `tray_update_status`
- **Signature**: `void tray_update_status(bool paused, bool persistent)`
- **Visibility**: `extern`
- **Description**: Updates the tray icon graphic and tooltip text based on whether the daemon is Active, Layer Locked, or Paused.
- **Called By**: [`MainWndProc`](#mainwndproc), [`main`](#main)
- **Calls**: Win32 `LoadIcon`, `wcscpy_s`, `Shell_NotifyIconW`.

#### `tray_show_menu`
- **Signature**: `void tray_show_menu(HWND hwnd)`
- **Visibility**: `extern`
- **Description**: Displays the right-click popup context menu (Status, Lock Layer toggle, Enabled checkbox, Reload Configuration, Open config.json, About, Exit).
- **Called By**: [`MainWndProc`](#mainwndproc)
- **Calls**: Win32 `GetCursorPos`, `CreatePopupMenu`, [`hook_is_paused`](#hook_is_paused), [`hook_is_persistent_layer`](#hook_is_persistent_layer), Win32 `AppendMenuW`, `SetForegroundWindow`, `TrackPopupMenu`, `PostMessage`, `DestroyMenu`.

#### `tray_cleanup`
- **Signature**: `void tray_cleanup(void)`
- **Visibility**: `extern`
- **Description**: Removes the tray icon from the Windows taskbar (`NIM_DELETE`) and destroys dynamic GDI icon resources.
- **Called By**: [`main`](#main)
- **Calls**: Win32 `Shell_NotifyIconW`, `DestroyIcon`.

---

### 4.6 Process Lifecycle, Installation & CLI

#### `log_msg`
- **Signature**: `static void log_msg(const char *fmt, ...)`
- **Visibility**: `static`
- **Description**: Formats and prints diagnostic messages to stdout when running in `--console` mode.
- **Called By**: [`reload_config`](#reload_config)
- **Calls**: `va_start`, `vprintf`, `va_end`, `printf`, `fflush`.

#### `run_cmd_hidden`
- **Signature**: `static bool run_cmd_hidden(const wchar_t *cmd)`
- **Visibility**: `static`
- **Description**: Executes a command synchronously with `CREATE_NO_WINDOW` / `SW_HIDE` and waits up to 10 seconds for completion, returning true if exit code is 0.
- **Called By**: [`setup_enable_startup`](#setup_enable_startup), [`setup_disable_startup`](#setup_disable_startup), [`setup_install`](#setup_install), [`setup_uninstall`](#setup_uninstall), [`main`](#main)
- **Calls**: `wcsncpy_s`, Win32 `CreateProcessW`, `WaitForSingleObject`, `GetExitCodeProcess`, `CloseHandle`.

#### `get_prog_files`
- **Signature**: `static void get_prog_files(wchar_t *buf, size_t len)`
- **Visibility**: `static`
- **Description**: Retrieves `%ProgramFiles(x86)%` or `%ProgramFiles%` directory path.
- **Called By**: [`setup_enable_startup`](#setup_enable_startup), [`setup_install`](#setup_install), [`setup_uninstall`](#setup_uninstall), [`main`](#main)
- **Calls**: Win32 `GetEnvironmentVariableW`, `wcscpy_s`.

#### `is_admin`
- **Signature**: `static bool is_admin(void)`
- **Visibility**: `static`
- **Description**: Inspects process elevation token to verify if the current process has Administrator privileges.
- **Called By**: [`main`](#main)
- **Calls**: Win32 `OpenProcessToken`, `GetTokenInformation`, `CloseHandle`.

#### `relaunch_admin`
- **Signature**: `static bool relaunch_admin(int argc, char *argv[])`
- **Visibility**: `static`
- **Description**: Relaunches `capslayer.exe` with the `runas` verb via `ShellExecuteExW` to request UAC elevation when running installation/uninstallation commands.
- **Called By**: [`main`](#main)
- **Calls**: Win32 `GetModuleFileNameW`, `MultiByteToWideChar`, `wcscat_s`, `ShellExecuteExW`.

#### `setup_enable_startup`
- **Signature**: `static bool setup_enable_startup(void)`
- **Visibility**: `static`
- **Description**: Registers an elevated task in Windows Task Scheduler (`TaskName: CapsLayer`) configured to run at user logon with `Highest` privileges, zero battery restrictions, and unlimited execution duration.
- **Called By**: [`setup_install`](#setup_install), [`main`](#main)
- **Calls**: [`get_prog_files`](#get_prog_files), `swprintf_s`, [`run_cmd_hidden`](#run_cmd_hidden).

#### `setup_disable_startup`
- **Signature**: `static bool setup_disable_startup(void)`
- **Visibility**: `static`
- **Description**: Deletes the Task Scheduler startup task and any legacy Run registry keys.
- **Called By**: [`setup_uninstall`](#setup_uninstall), [`main`](#main)
- **Calls**: [`run_cmd_hidden`](#run_cmd_hidden).

#### `setup_install`
- **Signature**: `static bool setup_install(void)`
- **Visibility**: `static`
- **Description**: Programmatic installer: copies executable and default config to Program Files, sets user-write ACLs via `icacls`, configures Task Scheduler startup, creates Start Menu shortcuts, and launches the daemon.
- **Called By**: [`main`](#main)
- **Calls**: Win32 `GetModuleFileNameW`, [`get_prog_files`](#get_prog_files), `swprintf_s`, `printf`, [`run_cmd_hidden`](#run_cmd_hidden), Win32 `Sleep`, `CreateDirectoryW`, `CopyFileW`, `wcscpy_s`, `wcsrchr`, `wcscat_s`, `GetFileAttributesW`, [`setup_enable_startup`](#setup_enable_startup), `ShellExecuteW`.

#### `setup_uninstall`
- **Signature**: `static bool setup_uninstall(void)`
- **Visibility**: `static`
- **Description**: Programmatic uninstaller: terminates running processes, removes Task Scheduler tasks, deletes shortcuts, and deletes the installation folder.
- **Called By**: [`main`](#main)
- **Calls**: [`get_prog_files`](#get_prog_files), `swprintf_s`, `printf`, [`run_cmd_hidden`](#run_cmd_hidden), Win32 `Sleep`, [`setup_disable_startup`](#setup_disable_startup), `_wgetenv`.

#### `reload_config`
- **Signature**: `static void reload_config(void)`
- **Visibility**: `static`
- **Description**: Loads configuration from `config.json` and pushes the update to the active hook engine.
- **Called By**: [`MainWndProc`](#mainwndproc), [`main`](#main)
- **Calls**: [`config_load_from_file`](#config_load_from_file), [`log_msg`](#log_msg), [`config_init_defaults`](#config_init_defaults), [`hook_update_config`](#hook_update_config).

#### `config_watcher`
- **Signature**: `static unsigned __stdcall config_watcher(void *arg)`
- **Visibility**: `static`
- **Description**: Background thread that uses `ReadDirectoryChangesW` to monitor disk modifications to `config.json`. Posts `WM_USER_RELOAD_CONFIG` to `MainWndProc` after a 250ms debounce delay.
- **Called By**: [`main`](#main) (via `_beginthreadex`)
- **Calls**: Win32 `MultiByteToWideChar`, `CreateFileW`, `CreateEventW`, `ResetEvent`, `ReadDirectoryChangesW`, `WaitForMultipleObjects`, `CancelIo`, `GetTickCount`, `Sleep`, `PostMessageW`, `CloseHandle`.

#### `on_layer_state`
- **Signature**: `static void on_layer_state(bool p, bool l)`
- **Visibility**: `static`
- **Description**: State callback registered with the hook engine. Posts `WM_USER_STATE_CHANGED` to the main window to update the tray icon.
- **Called By**: [`hook_set_paused`](#hook_set_paused), [`hook_set_persistent_layer`](#hook_set_persistent_layer) (via `g_state_cb`)
- **Calls**: Win32 `PostMessageW`.

#### `MainWndProc`
- **Signature**: `static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)`
- **Visibility**: `static`
- **Description**: Win32 window message handler for the invisible daemon message window. Handles tray icon interactions, context menu selections, state change messages, and config reload notifications.
- **Called By**: Windows OS Window Dispatcher (via `DispatchMessageW`)
- **Calls**: [`tray_show_menu`](#tray_show_menu), [`hook_is_paused`](#hook_is_paused), [`hook_set_paused`](#hook_set_paused), [`hook_is_persistent_layer`](#hook_is_persistent_layer), [`tray_update_status`](#tray_update_status), [`hook_toggle_persistent_layer`](#hook_toggle_persistent_layer), [`reload_config`](#reload_config), Win32 `MultiByteToWideChar`, `ShellExecuteW`, `MessageBoxW`, `DestroyWindow`, `PostQuitMessage`, `DefWindowProcW`.

#### `attach_console`
- **Signature**: `static void attach_console(bool force_alloc)`
- **Visibility**: `static`
- **Description**: Attaches the GUI application to the parent command prompt console or allocates a new console when running CLI arguments.
- **Called By**: [`main`](#main)
- **Calls**: Win32 `GetStdHandle`, `GetFileType`, `AttachConsole`, `AllocConsole`, `freopen_s`.

#### `main`
- **Signature**: `int main(int argc, char *argv[])`
- **Visibility**: `extern` (Entry Point)
- **Description**: Main application entry point. Parses CLI flags (`--install`, `--uninstall`, `--status`, `--test-config`, `--console`, `--paused`, etc.), ensures single-instance mutex, creates message window, initializes tray icon, installs keyboard hook, starts config watcher thread, and runs the Win32 message loop.
- **Called By**: [`WinMain`](#winmain), C Runtime Startup
- **Calls**: [`attach_console`](#attach_console), `memset`, [`config_get_default_path`](#config_get_default_path), `strcmp`, `printf`, [`config_get_dir_path`](#config_get_dir_path), [`config_load_from_file`](#config_load_from_file), [`get_prog_files`](#get_prog_files), `swprintf_s`, `GetFileAttributesW`, [`run_cmd_hidden`](#run_cmd_hidden), [`is_admin`](#is_admin), [`relaunch_admin`](#relaunch_admin), [`setup_install`](#setup_install), [`setup_uninstall`](#setup_uninstall), [`setup_enable_startup`](#setup_enable_startup), [`setup_disable_startup`](#setup_disable_startup), Win32 `CreateMutexW`, `GetLastError`, `CloseHandle`, `GetModuleHandle`, `RegisterClassExW`, `CreateWindowExW`, [`reload_config`](#reload_config), [`tray_init`](#tray_init), [`hook_set_paused`](#hook_set_paused), [`tray_update_status`](#tray_update_status), [`hook_set_state_callback`](#hook_set_state_callback), [`hook_install`](#hook_install), [`tray_cleanup`](#tray_cleanup), `DestroyWindow`, `UnregisterClassW`, `CreateEventW`, `_beginthreadex` (spawns [`config_watcher`](#config_watcher)), `GetMessageW`, `TranslateMessage`, `DispatchMessageW`, `SetEvent`, `WaitForSingleObject`, [`hook_uninstall`](#hook_uninstall), `ReleaseMutex`.

#### `WinMain`
- **Signature**: `int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)`
- **Visibility**: `extern`
- **Description**: Win32 GUI entry point that forwards execution to [`main`](#main) with global `__argc` and `__argv`.
- **Called By**: Windows Operating System
- **Calls**: [`main`](#main).

---

### 4.7 Unit Test Suite Functions

Defined in `tests/test_capslayer.c`:

#### `test_key_name_to_vk`
- **Signature**: `static void test_key_name_to_vk(void)`
- **Description**: Tests key name resolution for alphanumeric keys, arrows, function keys `F1-F24`, modifiers, punctuation, and aliases.
- **Calls**: [`key_name_to_vk`](#key_name_to_vk), `printf`, `fflush`.

#### `test_is_extended_key`
- **Signature**: `static void test_is_extended_key(void)`
- **Description**: Validates classification of extended keys and modifier keys.
- **Calls**: [`is_extended_key`](#is_extended_key), [`is_modifier_key`](#is_modifier_key), `printf`, `fflush`.

#### `test_config_defaults`
- **Signature**: `static void test_config_defaults(void)`
- **Description**: Validates that default settings and layer bindings match expected initial states.
- **Calls**: [`config_init_defaults`](#config_init_defaults), [`key_name_to_vk`](#key_name_to_vk), `printf`, `fflush`.

#### `test_config_json_parsing`
- **Signature**: `static void test_config_json_parsing(void)`
- **Description**: Validates JSON parser against complex configurations, actions, global shortcuts, and malformed strings.
- **Calls**: [`config_load_from_json_string`](#config_load_from_json_string), [`key_name_to_vk`](#key_name_to_vk), `printf`, `fflush`.

#### `test_config_file_loading`
- **Signature**: `static void test_config_file_loading(void)`
- **Description**: Tests loading and parsing `config.json` directly from the filesystem.
- **Calls**: [`config_load_from_file`](#config_load_from_file), [`key_name_to_vk`](#key_name_to_vk), `printf`, `fflush`.

#### `test_path_helpers`
- **Signature**: `static void test_path_helpers(void)`
- **Description**: Tests path resolution helpers for extracting executable directories and relative paths.
- **Calls**: [`config_get_default_path`](#config_get_default_path), [`config_get_dir_path`](#config_get_dir_path), `strstr`, `strcmp`, `printf`, `fflush`.

#### `test_hook_state_management`
- **Signature**: `static void test_hook_state_management(void)`
- **Description**: Verifies pause state transitions, persistent lock toggles, state callbacks, and simulated hook procedure event processing.
- **Calls**: [`config_init_defaults`](#config_init_defaults), [`hook_update_config`](#hook_update_config), [`hook_is_paused`](#hook_is_paused), [`hook_set_paused`](#hook_set_paused), [`hook_set_state_callback`](#hook_set_state_callback), [`hook_is_persistent_layer`](#hook_is_persistent_layer), [`hook_set_persistent_layer`](#hook_set_persistent_layer), [`LowLevelKeyboardProc`](#lowlevelkeyboardproc), [`hook_toggle_persistent_layer`](#hook_toggle_persistent_layer), `printf`, `fflush`.

---

## 5. Scripts & Windows Automation

### 5.1 `setup.bat` (Automated Installer)

The setup script handles deployment to `%ProgramFiles(x86)%\capslayer`:

```bat
:: Key steps executed by setup.bat:
1. Auto-Elevation Check:
   Uses `net session` to detect admin rights; if not elevated, re-launches itself via
   `powershell Start-Process -Verb RunAs`.

2. Process Termination:
   Runs `taskkill /F /IM capslayer.exe` to stop existing instances before copying files.

3. File Deployment:
   Copies `capslayer.exe`, `config.json`, `setup.bat`, and `uninstall.bat` to the target directory.

4. Permission Configuration:
   Grants full write permissions on the directory to all standard users via:
   `icacls "%TARGET_DIR%" /grant *S-1-5-32-545:(OI)(CI)M /T /Q`
   This allows users to edit config.json without needing administrative rights each time.

5. Elevated Scheduled Task Creation:
   Registers a logon trigger in Task Scheduler using highest run level:
   `Register-ScheduledTask -TaskName 'CapsLayer' -Principal [Highest] -Trigger [AtLogOn]`
   This ensures CapsLayer can intercept elevated application windows (such as Task Manager or Regedit).

6. Start Menu Shortcut & Startup:
   Creates Start Menu shortcut via WScript.Shell COM object and launches `capslayer.exe`.
```

---

### 5.2 `uninstall.bat` (Automated Uninstaller)

```bat
:: Key steps executed by uninstall.bat:
1. Auto-elevates to Administrator.
2. Terminates running `capslayer.exe` processes.
3. Deletes Task Scheduler task `CapsLayer`.
4. Removes Start Menu shortcuts.
5. Recursively deletes the installation folder (`rmdir /S /Q "%TARGET_DIR%"`).
```

---

## 6. Configuration Reference (`config.json`)

`config.json` allows full customization of settings, layer remappings, and global hotkeys:

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

### Action Types Supported in `layer` and `shortcuts`:

1. **Simple Key Remapping**: `"i": "up"`, `"m": "delete"`
2. **Key Combinations**: `"w": ["alt", "f4"]`, `"c": "ctrl+c"`
3. **Command Execution**: `"z": { "action": "exec", "command": "wt.exe" }`
4. **Layer Lock Toggle**: `"p": "toggle_persistent"`

---

## 7. Building & Testing

### Building with Zig C Compiler

```powershell
# 1. Compile resource file (manifest & icon)
zig rc /fo res/resource.res res/resource.rc

# 2. Build production Windows GUI binary
zig cc -O2 -Wl,--subsystem,windows -Isrc -D_CRT_SECURE_NO_WARNINGS `
  src/capslayer.c res/resource.res `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o capslayer.exe

# 3. Build and execute unit test suite
zig cc -O2 -Isrc -DCAPSLAYER_NO_MAIN -D_CRT_SECURE_NO_WARNINGS `
  tests/test_capslayer.c src/capslayer.c `
  -luser32 -lshell32 -ladvapi32 -lgdi32 -lole32 -o test_capslayer.exe
.\test_capslayer.exe
```

### Building with Microsoft Visual C++ (MSVC)

```cmd
:: 1. Compile resource
rc /fo res\resource.res res\resource.rc

:: 2. Build release binary
cl /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /Isrc src\capslayer.c res\resource.res /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib gdi32.lib ole32.lib /OUT:capslayer.exe

:: 3. Build and run unit tests
cl /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DCAPSLAYER_NO_MAIN /Isrc tests\test_capslayer.c src\capslayer.c /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib advapi32.lib gdi32.lib ole32.lib /OUT:test_capslayer.exe
test_capslayer.exe
```
