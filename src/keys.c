#include "keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <process.h>
#include <shellapi.h>

typedef struct {
    const char *name;
    WORD vk;
} KeyMapping;

static const KeyMapping KEY_TABLE[] = {
    /* Navigation & Editing */
    { "up",            VK_UP },
    { "down",          VK_DOWN },
    { "left",          VK_LEFT },
    { "right",         VK_RIGHT },
    { "home",          VK_HOME },
    { "end",           VK_END },
    { "pageup",        VK_PRIOR },
    { "pgup",          VK_PRIOR },
    { "pagedown",      VK_NEXT },
    { "pgdn",          VK_NEXT },
    { "insert",        VK_INSERT },
    { "ins",           VK_INSERT },
    { "delete",        VK_DELETE },
    { "del",           VK_DELETE },
    { "backspace",     VK_BACK },
    { "bksp",          VK_BACK },
    { "bs",            VK_BACK },
    { "tab",           VK_TAB },
    { "enter",         VK_RETURN },
    { "return",        VK_RETURN },
    { "space",         VK_SPACE },
    { "spacebar",      VK_SPACE },
    { "escape",        VK_ESCAPE },
    { "esc",           VK_ESCAPE },
    { "capslock",      VK_CAPITAL },
    { "caps",          VK_CAPITAL },

    /* Modifiers */
    { "ctrl",          VK_CONTROL },
    { "control",       VK_CONTROL },
    { "lctrl",         VK_LCONTROL },
    { "rctrl",         VK_RCONTROL },
    { "alt",           VK_MENU },
    { "lalt",          VK_LMENU },
    { "ralt",          VK_RMENU },
    { "shift",         VK_SHIFT },
    { "lshift",        VK_LSHIFT },
    { "rshift",        VK_RSHIFT },
    { "win",           VK_LWIN },
    { "lwin",          VK_LWIN },
    { "rwin",          VK_RWIN },
    { "super",         VK_LWIN },

    /* Media & Volume */
    { "volume_up",     VK_VOLUME_UP },
    { "volumedown",    VK_VOLUME_DOWN },
    { "volume_down",   VK_VOLUME_DOWN },
    { "mute",          VK_VOLUME_MUTE },
    { "volume_mute",   VK_VOLUME_MUTE },
    { "play_pause",    VK_MEDIA_PLAY_PAUSE },
    { "playpause",     VK_MEDIA_PLAY_PAUSE },
    { "next_track",    VK_MEDIA_NEXT_TRACK },
    { "nexttrack",     VK_MEDIA_NEXT_TRACK },
    { "prev_track",    VK_MEDIA_PREV_TRACK },
    { "prevtrack",     VK_MEDIA_PREV_TRACK },
    { "stop",          VK_MEDIA_STOP },

    /* Locks & Miscellaneous */
    { "printscreen",   VK_SNAPSHOT },
    { "prtscn",        VK_SNAPSHOT },
    { "prtsc",         VK_SNAPSHOT },
    { "scrolllock",    VK_SCROLL },
    { "pause",         VK_PAUSE },
    { "numlock",       VK_NUMLOCK },
    { "apps",          VK_APPS },
    { "menu",          VK_APPS },

    /* Common OEM / Punctuation */
    { ";",             VK_OEM_1 },
    { "semicolon",     VK_OEM_1 },
    { "=",             VK_OEM_PLUS },
    { "plus",          VK_OEM_PLUS },
    { "equal",         VK_OEM_PLUS },
    { ",",             VK_OEM_COMMA },
    { "comma",         VK_OEM_COMMA },
    { "-",             VK_OEM_MINUS },
    { "minus",         VK_OEM_MINUS },
    { ".",             VK_OEM_PERIOD },
    { "period",        VK_OEM_PERIOD },
    { "dot",           VK_OEM_PERIOD },
    { "/",             VK_OEM_2 },
    { "slash",         VK_OEM_2 },
    { "`",             VK_OEM_3 },
    { "backtick",      VK_OEM_3 },
    { "grave",         VK_OEM_3 },
    { "tilde",         VK_OEM_3 },
    { "[",             VK_OEM_4 },
    { "leftbracket",   VK_OEM_4 },
    { "openbracket",   VK_OEM_4 },
    { "\\",            VK_OEM_5 },
    { "backslash",     VK_OEM_5 },
    { "]",             VK_OEM_6 },
    { "rightbracket",  VK_OEM_6 },
    { "closebracket",  VK_OEM_6 },
    { "'",             VK_OEM_7 },
    { "quote",         VK_OEM_7 },
    { "apostrophe",    VK_OEM_7 },

    /* Numpad Keys */
    { "numpad0",       VK_NUMPAD0 },
    { "numpad1",       VK_NUMPAD1 },
    { "numpad2",       VK_NUMPAD2 },
    { "numpad3",       VK_NUMPAD3 },
    { "numpad4",       VK_NUMPAD4 },
    { "numpad5",       VK_NUMPAD5 },
    { "numpad6",       VK_NUMPAD6 },
    { "numpad7",       VK_NUMPAD7 },
    { "numpad8",       VK_NUMPAD8 },
    { "numpad9",       VK_NUMPAD9 },
    { "multiply",      VK_MULTIPLY },
    { "add",           VK_ADD },
    { "subtract",      VK_SUBTRACT },
    { "decimal",       VK_DECIMAL },
    { "divide",        VK_DIVIDE },

    { NULL, 0 }
};

WORD key_name_to_vk(const char *name)
{
    if (!name || !*name) {
        return 0;
    }

    /* Fast path: single alphanumeric character */
    if (name[1] == '\0') {
        char c = (char)tolower((unsigned char)name[0]);
        if (c >= 'a' && c <= 'z') {
            return (WORD)(0x41 + (c - 'a'));
        }
        if (c >= '0' && c <= '9') {
            return (WORD)(0x30 + (c - '0'));
        }
    }

    /* Check Function keys: f1 - f24 */
    if ((name[0] == 'f' || name[0] == 'F') && isdigit((unsigned char)name[1])) {
        int fnum = atoi(&name[1]);
        if (fnum >= 1 && fnum <= 24) {
            return (WORD)(VK_F1 + (fnum - 1));
        }
    }

    /* Lookup named keys in static table */
    for (size_t i = 0; KEY_TABLE[i].name != NULL; ++i) {
        if (_stricmp(name, KEY_TABLE[i].name) == 0) {
            return KEY_TABLE[i].vk;
        }
    }

    /* Fallback: layout-aware VkKeyScanW for arbitrary unicode/OEM characters */
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wide_len > 1) {
        wchar_t wide_buf[16];
        if (wide_len < 16) {
            MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_buf, wide_len);
            SHORT scan_res = VkKeyScanW(wide_buf[0]);
            if (scan_res != -1) {
                return (WORD)(scan_res & 0xFF);
            }
        }
    }

    return 0;
}

bool is_extended_key(WORD vk)
{
    switch (vk) {
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:      /* Page Up */
        case VK_NEXT:       /* Page Down */
        case VK_INSERT:
        case VK_DELETE:
        case VK_DIVIDE:     /* Numpad Slash */
        case VK_NUMLOCK:
        case VK_RCONTROL:
        case VK_RMENU:      /* Right Alt */
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:
        case VK_VOLUME_MUTE:
        case VK_VOLUME_DOWN:
        case VK_VOLUME_UP:
        case VK_MEDIA_NEXT_TRACK:
        case VK_MEDIA_PREV_TRACK:
        case VK_MEDIA_STOP:
        case VK_MEDIA_PLAY_PAUSE:
        case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}
bool is_modifier_key(WORD vk)
{
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
    }
}

bool is_modifier_down(WORD vk)
{
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
                   (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
                   (GetAsyncKeyState(VK_RSHIFT) & 0x8000);

        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
                   (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
                   (GetAsyncKeyState(VK_RCONTROL) & 0x8000);

        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return (GetAsyncKeyState(VK_MENU) & 0x8000) ||
                   (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
                   (GetAsyncKeyState(VK_RMENU) & 0x8000);

        case VK_LWIN:
        case VK_RWIN:
            return (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
                   (GetAsyncKeyState(VK_RWIN) & 0x8000);

        default:
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

void send_key_event(WORD vk, bool is_down)
{
    if (vk == 0) return;

    INPUT input;
    ZeroMemory(&input, sizeof(INPUT));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = 0;
    
    if (!is_down) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }
    if (is_extended_key(vk)) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    
    input.ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;
    input.ki.time = 0;

    SendInput(1, &input, sizeof(INPUT));
}

void send_key_tap(WORD vk)
{
    if (vk == 0) return;

    INPUT inputs[2];
    ZeroMemory(inputs, sizeof(inputs));

    WORD scan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    DWORD ext_flag = is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0;

    /* Key Down */
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.wScan = scan;
    inputs[0].ki.dwFlags = ext_flag;
    inputs[0].ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;

    /* Key Up */
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.wScan = scan;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP | ext_flag;
    inputs[1].ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;

    SendInput(2, inputs, sizeof(INPUT));
}

void send_key_combo(const WORD *vks, size_t count)
{
    if (!vks || count == 0) return;

    /* Allocate buffer for down events + up events */
    size_t total_events = count * 2;
    INPUT *inputs = (INPUT *)malloc(total_events * sizeof(INPUT));
    if (!inputs) return;

    ZeroMemory(inputs, total_events * sizeof(INPUT));

    /* Press all keys in sequence */
    for (size_t i = 0; i < count; ++i) {
        WORD vk = vks[i];
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wVk = vk;
        inputs[i].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        inputs[i].ki.dwFlags = is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
        inputs[i].ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;
    }

    /* Release all keys in reverse order */
    for (size_t i = 0; i < count; ++i) {
        size_t idx = count + i;
        WORD vk = vks[count - 1 - i];
        inputs[idx].type = INPUT_KEYBOARD;
        inputs[idx].ki.wVk = vk;
        inputs[idx].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP | (is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
        inputs[idx].ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;
    }

    SendInput((UINT)total_events, inputs, sizeof(INPUT));
    free(inputs);
}
typedef struct {
    wchar_t *command;
} AsyncExecParam;

static unsigned __stdcall async_exec_worker(void *arg)
{
    AsyncExecParam *param = (AsyncExecParam *)arg;
    if (!param) return 0;

    if (param->command) {
        wchar_t cmd_buf[1024];
        wcsncpy_s(cmd_buf, sizeof(cmd_buf) / sizeof(wchar_t), param->command, _TRUNCATE);

        /* Auto-expand bare shutdown commands if flags were omitted */
        if (_wcsicmp(cmd_buf, L"shutdown.exe") == 0 ||
            _wcsicmp(cmd_buf, L"shutdown") == 0 ||
            _wcsicmp(cmd_buf, L"C:\\WINDOWS\\system32\\shutdown.exe") == 0 ||
            _wcsicmp(cmd_buf, L"C:\\Windows\\System32\\shutdown.exe") == 0) {
            wcscpy_s(cmd_buf, sizeof(cmd_buf) / sizeof(wchar_t), L"shutdown.exe /s /t 0");
        }

        /* Check if target is a .lnk shortcut or web URL */
        bool is_shell_target = (wcsstr(cmd_buf, L".lnk") != NULL) ||
                               (wcsstr(cmd_buf, L".LNK") != NULL) ||
                               (wcsstr(cmd_buf, L"http://") != NULL) ||
                               (wcsstr(cmd_buf, L"https://") != NULL);

        if (!is_shell_target) {
            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            /* Use CREATE_NO_WINDOW so background system commands don't flash a console window */
            if (CreateProcessW(NULL, cmd_buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                free(param->command);
                free(param);
                return 0;
            }
        }

        /* Fallback / Shell target (e.g. .lnk shortcuts) */
        SHELLEXECUTEINFOW sei;
        ZeroMemory(&sei, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.lpFile = cmd_buf;
        sei.nShow = SW_SHOWNORMAL;
        sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        ShellExecuteExW(&sei);

        free(param->command);
    }
    free(param);
    return 0;
}

void spawn_process_async(const char *command_utf8)
{
    if (!command_utf8 || !*command_utf8) return;

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, command_utf8, -1, NULL, 0);
    if (wide_len <= 0) return;

    wchar_t *wide_cmd = (wchar_t *)malloc(wide_len * sizeof(wchar_t));
    if (!wide_cmd) return;

    MultiByteToWideChar(CP_UTF8, 0, command_utf8, -1, wide_cmd, wide_len);

    AsyncExecParam *param = (AsyncExecParam *)malloc(sizeof(AsyncExecParam));
    if (!param) {
        free(wide_cmd);
        return;
    }
    param->command = wide_cmd;

    uintptr_t th = _beginthreadex(NULL, 0, async_exec_worker, param, 0, NULL);
    if (th != 0) {
        CloseHandle((HANDLE)th);
    } else {
        free(wide_cmd);
        free(param);
    }
}
