#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "capslayer.h"

#define WINDOW_CLASS_NAME L"CapsLayer_MessageWindow_Class"
#define WINDOW_TITLE      L"CapsLayer Daemon"
#define MUTEX_NAME        L"CapsLayer_SingleInstance_Mutex"

/* ========================================================================= */
/* 1. Minimal Fast JSON Parser (Zero Dependencies)                          */
/* ========================================================================= */

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } jtype_t;

typedef struct jnode {
    jtype_t type;
    char *key;
    union {
        bool b;
        double num;
        char *str;
        struct { struct jnode *items; size_t count; } arr;
        struct { struct jnode *pairs; size_t count; } obj;
    };
} jnode_t;

static const char *skip_ws(const char *s) {
    while (*s && ((unsigned char)*s <= ' ' || *s == '/')) {
        if (*s == '/' && s[1] == '/') { while (*s && *s != '\n') s++; }
        else if (*s == '/' && s[1] == '*') { s += 2; while (*s && !(*s == '*' && s[1] == '/')) s++; if (*s) s += 2; }
        else s++;
    }
    return s;
}

static char *parse_str(const char **ps) {
    const char *s = *ps;
    if (*s != '"') return NULL;
    s++;
    size_t cap = 32, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    while (*s && *s != '"') {
        char c = *s++;
        if (c == '\\' && *s) {
            c = *s++;
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    int hex = 0;
                    for (int i = 0; i < 4 && *s; i++) {
                        char h = *s++;
                        hex = (hex << 4) | (isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10));
                    }
                    c = (hex < 128) ? (char)hex : '?';
                    break;
                }
            }
        }
        if (len + 2 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = c;
    }
    if (*s == '"') s++;
    buf[len] = '\0';
    *ps = s;
    return buf;
}

static void jnode_free(jnode_t *node) {
    if (!node) return;
    free(node->key);
    node->key = NULL;
    if (node->type == JSON_STRING) {
        free(node->str);
        node->str = NULL;
    } else if (node->type == JSON_ARRAY) {
        if (node->arr.items) {
            for (size_t i = 0; i < node->arr.count; i++) jnode_free(&node->arr.items[i]);
            free(node->arr.items);
            node->arr.items = NULL;
        }
    } else if (node->type == JSON_OBJECT) {
        if (node->obj.pairs) {
            for (size_t i = 0; i < node->obj.count; i++) jnode_free(&node->obj.pairs[i]);
            free(node->obj.pairs);
            node->obj.pairs = NULL;
        }
    }
    node->type = JSON_NULL;
}

static bool parse_val(const char **ps, jnode_t *out);

static bool parse_arr(const char **ps, jnode_t *out) {
    const char *s = *ps;
    if (*s != '[') return false;
    s = skip_ws(s + 1);
    memset(out, 0, sizeof(*out));
    out->type = JSON_ARRAY;
    if (*s == ']') { *ps = s + 1; return true; }
    size_t cap = 8;
    out->arr.items = (jnode_t *)calloc(cap, sizeof(jnode_t));
    if (!out->arr.items) return false;
    while (*s) {
        if (out->arr.count >= cap) {
            cap *= 2;
            jnode_t *ni = (jnode_t *)realloc(out->arr.items, cap * sizeof(jnode_t));
            if (!ni) { jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
            out->arr.items = ni;
        }
        jnode_t item;
        memset(&item, 0, sizeof(item));
        if (!parse_val(&s, &item)) { jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
        out->arr.items[out->arr.count++] = item;
        s = skip_ws(s);
        if (*s == ',') s = skip_ws(s + 1);
        else if (*s == ']') { s++; break; }
        else { jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
    }
    *ps = s;
    return true;
}

static bool parse_obj(const char **ps, jnode_t *out) {
    const char *s = *ps;
    if (*s != '{') return false;
    s = skip_ws(s + 1);
    memset(out, 0, sizeof(*out));
    out->type = JSON_OBJECT;
    if (*s == '}') { *ps = s + 1; return true; }
    size_t cap = 8;
    out->obj.pairs = (jnode_t *)calloc(cap, sizeof(jnode_t));
    if (!out->obj.pairs) return false;
    while (*s) {
        if (*s != '"') { jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
        char *key = parse_str(&s);
        if (!key) { jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
        s = skip_ws(s);
        if (*s != ':') { free(key); jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
        s = skip_ws(s + 1);
        if (out->obj.count >= cap) {
            cap *= 2;
            jnode_t *np = (jnode_t *)realloc(out->obj.pairs, cap * sizeof(jnode_t));
            if (!np) { free(key); jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
            out->obj.pairs = np;
        }
        jnode_t val;
        memset(&val, 0, sizeof(val));
        if (!parse_val(&s, &val)) { free(key); jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
        val.key = key;
        out->obj.pairs[out->obj.count++] = val;
        s = skip_ws(s);
        if (*s == ',') s = skip_ws(s + 1);
        else if (*s == '}') { s++; break; }
        else { jnode_free(out); memset(out, 0, sizeof(*out)); return false; }
    }
    *ps = s;
    return true;
}

static bool parse_val(const char **ps, jnode_t *out) {
    const char *s = skip_ws(*ps);
    if (!*s) return false;
    memset(out, 0, sizeof(*out));
    if (*s == '{') { bool r = parse_obj(&s, out); if (r) *ps = s; return r; }
    if (*s == '[') { bool r = parse_arr(&s, out); if (r) *ps = s; return r; }
    if (*s == '"') {
        char *str = parse_str(&s);
        if (!str) return false;
        out->type = JSON_STRING; out->str = str; *ps = s; return true;
    }
    if (strncmp(s, "true", 4) == 0) { out->type = JSON_BOOL; out->b = true; *ps = s + 4; return true; }
    if (strncmp(s, "false", 5) == 0) { out->type = JSON_BOOL; out->b = false; *ps = s + 5; return true; }
    if (strncmp(s, "null", 4) == 0) { out->type = JSON_NULL; *ps = s + 4; return true; }
    if (*s == '-' || isdigit((unsigned char)*s)) {
        char *end = NULL;
        out->type = JSON_NUMBER;
        out->num = strtod(s, &end);
        if (end == s) return false;
        *ps = end;
        return true;
    }
    return false;
}

static jnode_t *jget(const jnode_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (size_t i = 0; i < obj->obj.count; i++) {
        if (obj->obj.pairs[i].key && _stricmp(obj->obj.pairs[i].key, key) == 0)
            return &obj->obj.pairs[i];
    }
    return NULL;
}

static const char *jget_str(const jnode_t *obj, const char *key) {
    jnode_t *n = jget(obj, key);
    return (n && n->type == JSON_STRING) ? n->str : NULL;
}

static bool jget_bool(const jnode_t *obj, const char *key, bool def) {
    jnode_t *n = jget(obj, key);
    return (n && n->type == JSON_BOOL) ? n->b : def;
}

/* ========================================================================= */
/* 2. Key Name Resolution & Input Synthesis                                 */
/* ========================================================================= */

typedef struct { const char *name; WORD vk; } KeyMapping;

static const KeyMapping KEY_TABLE[] = {
    { "up", VK_UP }, { "down", VK_DOWN }, { "left", VK_LEFT }, { "right", VK_RIGHT },
    { "home", VK_HOME }, { "end", VK_END }, { "pageup", VK_PRIOR }, { "pgup", VK_PRIOR },
    { "pagedown", VK_NEXT }, { "pgdn", VK_NEXT }, { "insert", VK_INSERT }, { "ins", VK_INSERT },
    { "delete", VK_DELETE }, { "del", VK_DELETE }, { "backspace", VK_BACK }, { "bksp", VK_BACK }, { "bs", VK_BACK },
    { "tab", VK_TAB }, { "enter", VK_RETURN }, { "return", VK_RETURN }, { "space", VK_SPACE }, { "spacebar", VK_SPACE },
    { "escape", VK_ESCAPE }, { "esc", VK_ESCAPE }, { "capslock", VK_CAPITAL }, { "caps", VK_CAPITAL },
    { "ctrl", VK_CONTROL }, { "control", VK_CONTROL }, { "lctrl", VK_LCONTROL }, { "rctrl", VK_RCONTROL },
    { "alt", VK_MENU }, { "lalt", VK_LMENU }, { "ralt", VK_RMENU },
    { "shift", VK_SHIFT }, { "lshift", VK_LSHIFT }, { "rshift", VK_RSHIFT },
    { "win", VK_LWIN }, { "lwin", VK_LWIN }, { "rwin", VK_RWIN }, { "super", VK_LWIN },
    { "volume_up", VK_VOLUME_UP }, { "volume_down", VK_VOLUME_DOWN }, { "volumedown", VK_VOLUME_DOWN },
    { "mute", VK_VOLUME_MUTE }, { "volume_mute", VK_VOLUME_MUTE },
    { "play_pause", VK_MEDIA_PLAY_PAUSE }, { "playpause", VK_MEDIA_PLAY_PAUSE },
    { "next_track", VK_MEDIA_NEXT_TRACK }, { "nexttrack", VK_MEDIA_NEXT_TRACK },
    { "prev_track", VK_MEDIA_PREV_TRACK }, { "prevtrack", VK_MEDIA_PREV_TRACK }, { "stop", VK_MEDIA_STOP },
    { "printscreen", VK_SNAPSHOT }, { "prtscn", VK_SNAPSHOT }, { "prtsc", VK_SNAPSHOT },
    { "scrolllock", VK_SCROLL }, { "pause", VK_PAUSE }, { "numlock", VK_NUMLOCK },
    { "apps", VK_APPS }, { "menu", VK_APPS },
    { ";", VK_OEM_1 }, { "semicolon", VK_OEM_1 }, { "=", VK_OEM_PLUS }, { "plus", VK_OEM_PLUS }, { "equal", VK_OEM_PLUS },
    { ",", VK_OEM_COMMA }, { "comma", VK_OEM_COMMA }, { "-", VK_OEM_MINUS }, { "minus", VK_OEM_MINUS },
    { ".", VK_OEM_PERIOD }, { "period", VK_OEM_PERIOD }, { "dot", VK_OEM_PERIOD },
    { "/", VK_OEM_2 }, { "slash", VK_OEM_2 }, { "`", VK_OEM_3 }, { "backtick", VK_OEM_3 },
    { "grave", VK_OEM_3 }, { "tilde", VK_OEM_3 }, { "[", VK_OEM_4 }, { "leftbracket", VK_OEM_4 },
    { "openbracket", VK_OEM_4 }, { "\\", VK_OEM_5 }, { "backslash", VK_OEM_5 },
    { "]", VK_OEM_6 }, { "rightbracket", VK_OEM_6 }, { "closebracket", VK_OEM_6 },
    { "'", VK_OEM_7 }, { "quote", VK_OEM_7 }, { "apostrophe", VK_OEM_7 },
    { "numpad0", VK_NUMPAD0 }, { "numpad1", VK_NUMPAD1 }, { "numpad2", VK_NUMPAD2 }, { "numpad3", VK_NUMPAD3 },
    { "numpad4", VK_NUMPAD4 }, { "numpad5", VK_NUMPAD5 }, { "numpad6", VK_NUMPAD6 }, { "numpad7", VK_NUMPAD7 },
    { "numpad8", VK_NUMPAD8 }, { "numpad9", VK_NUMPAD9 }, { "multiply", VK_MULTIPLY }, { "add", VK_ADD },
    { "subtract", VK_SUBTRACT }, { "decimal", VK_DECIMAL }, { "divide", VK_DIVIDE },
    { NULL, 0 }
};

WORD key_name_to_vk(const char *name) {
    if (!name || !*name) return 0;
    if (name[1] == '\0') {
        char c = (char)tolower((unsigned char)name[0]);
        if (c >= 'a' && c <= 'z') return (WORD)(0x41 + (c - 'a'));
        if (c >= '0' && c <= '9') return (WORD)(0x30 + (c - '0'));
    }
    if ((name[0] == 'f' || name[0] == 'F') && isdigit((unsigned char)name[1])) {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 24) return (WORD)(VK_F1 + n - 1);
    }
    for (size_t i = 0; KEY_TABLE[i].name != NULL; ++i) {
        if (_stricmp(name, KEY_TABLE[i].name) == 0) return KEY_TABLE[i].vk;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen > 1 && wlen < 16) {
        wchar_t wbuf[16];
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wbuf, wlen);
        SHORT res = VkKeyScanW(wbuf[0]);
        if (res != -1) return (WORD)(res & 0xFF);
    }
    return 0;
}

bool is_extended_key(WORD vk) {
    switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
        case VK_RCONTROL: case VK_RMENU: case VK_LWIN: case VK_RWIN:
        case VK_APPS: case VK_VOLUME_MUTE: case VK_VOLUME_DOWN: case VK_VOLUME_UP:
        case VK_MEDIA_NEXT_TRACK: case VK_MEDIA_PREV_TRACK: case VK_MEDIA_STOP:
        case VK_MEDIA_PLAY_PAUSE: case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}

bool is_modifier_key(WORD vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

bool is_modifier_down(WORD vk) {
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT)
        return ((GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_LSHIFT) | GetAsyncKeyState(VK_RSHIFT)) & 0x8000) != 0;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL)
        return ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_LCONTROL) | GetAsyncKeyState(VK_RCONTROL)) & 0x8000) != 0;
    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)
        return ((GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LMENU) | GetAsyncKeyState(VK_RMENU)) & 0x8000) != 0;
    if (vk == VK_LWIN || vk == VK_RWIN)
        return ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void send_key_event(WORD vk, bool is_down) {
    if (!vk) return;
    INPUT in = { .type = INPUT_KEYBOARD, .ki = {
        .wVk = vk,
        .wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC),
        .dwFlags = (is_down ? 0 : KEYEVENTF_KEYUP) | (is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0),
        .dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG
    }};
    SendInput(1, &in, sizeof(INPUT));
}

void send_key_tap(WORD vk) {
    if (!vk) return;
    WORD scan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    DWORD ext = is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
    INPUT in[2] = {
        { .type = INPUT_KEYBOARD, .ki = { .wVk = vk, .wScan = scan, .dwFlags = ext, .dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG } },
        { .type = INPUT_KEYBOARD, .ki = { .wVk = vk, .wScan = scan, .dwFlags = KEYEVENTF_KEYUP | ext, .dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG } }
    };
    SendInput(2, in, sizeof(INPUT));
}

void send_key_combo(const WORD *vks, size_t count) {
    if (!vks || count == 0 || count > MAX_COMBO_KEYS) return;
    INPUT in[MAX_COMBO_KEYS * 2];
    ZeroMemory(in, sizeof(in));
    UINT n = (UINT)count;
    for (UINT i = 0; i < n; ++i) {
        WORD vk = vks[i];
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = vk;
        in[i].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        in[i].ki.dwFlags = is_extended_key(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
        in[i].ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;

        WORD rvk = vks[n - 1 - i];
        in[n + i].type = INPUT_KEYBOARD;
        in[n + i].ki.wVk = rvk;
        in[n + i].ki.wScan = (WORD)MapVirtualKeyW(rvk, MAPVK_VK_TO_VSC);
        in[n + i].ki.dwFlags = KEYEVENTF_KEYUP | (is_extended_key(rvk) ? KEYEVENTF_EXTENDEDKEY : 0);
        in[n + i].ki.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;
    }
    SendInput(n * 2, in, sizeof(INPUT));
}

static unsigned __stdcall async_exec_worker(void *arg) {
    wchar_t *cmd = (wchar_t *)arg;
    if (cmd) {
        if (_wcsicmp(cmd, L"shutdown.exe") == 0 || _wcsicmp(cmd, L"shutdown") == 0) {
            wcscpy_s(cmd, 1024, L"shutdown.exe /s /t 0");
        }
        STARTUPINFOW si = { .cb = sizeof(si) };
        PROCESS_INFORMATION pi;
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            SHELLEXECUTEINFOW sei = { .cbSize = sizeof(sei), .lpFile = cmd, .nShow = SW_SHOWNORMAL, .fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI };
            ShellExecuteExW(&sei);
        } else {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        free(cmd);
    }
    return 0;
}

void spawn_process_async(const char *cmd_utf8) {
    if (!cmd_utf8 || !*cmd_utf8) return;
    int len = MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, NULL, 0);
    if (len <= 0) return;
    wchar_t *wcmd = (wchar_t *)malloc(max(len, 1024) * sizeof(wchar_t));
    if (!wcmd) return;
    MultiByteToWideChar(CP_UTF8, 0, cmd_utf8, -1, wcmd, len);
    uintptr_t th = _beginthreadex(NULL, 0, async_exec_worker, wcmd, 0, NULL);
    if (th) CloseHandle((HANDLE)th);
    else free(wcmd);
}

/* ========================================================================= */
/* 3. Configuration Loading & Parsing                                       */
/* ========================================================================= */

static bool is_toggle_persistent_name(const char *s) {
    if (!s) return false;
    return (_stricmp(s, "toggle_persistent") == 0 || _stricmp(s, "toggle_lock") == 0 ||
            _stricmp(s, "lock") == 0 || _stricmp(s, "lock_layer") == 0 ||
            _stricmp(s, "persistent") == 0 || _stricmp(s, "toggle_persistent_layer") == 0 ||
            _stricmp(s, "layer_lock") == 0 || _stricmp(s, "toggle") == 0);
}

static bool parse_combo_str(const char *str, layer_action_t *action) {
    if (!str || !*str) return false;
    char buf[256];
    strncpy_s(buf, sizeof(buf), str, _TRUNCATE);
    WORD vks[MAX_COMBO_KEYS];
    uint8_t count = 0;
    char *ctx = NULL, *tok = strtok_s(buf, "+", &ctx);
    while (tok) {
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
        if (*tok) {
            if (count >= MAX_COMBO_KEYS) return false;
            WORD vk = key_name_to_vk(tok);
            if (!vk) return false;
            vks[count++] = vk;
        }
        tok = strtok_s(NULL, "+", &ctx);
    }
    if (!count) return false;
    if (count == 1) {
        action->type = ACTION_KEY;
        action->data.target_vk = vks[0];
    } else {
        action->type = ACTION_COMBO;
        action->data.combo.count = count;
        memcpy(action->data.combo.vks, vks, count * sizeof(WORD));
    }
    return true;
}

static bool parse_layer_action_node(const jnode_t *node, layer_action_t *action) {
    if (!node || !action) return false;
    memset(action, 0, sizeof(*action));

    if (node->type == JSON_STRING && node->str) {
        const char *s = node->str;
        if (is_toggle_persistent_name(s)) {
            action->type = ACTION_TOGGLE_PERSISTENT;
            return true;
        }
        if (strchr(s, '+')) return parse_combo_str(s, action);
        WORD vk = key_name_to_vk(s);
        if (vk) {
            action->type = ACTION_KEY;
            action->data.target_vk = vk;
            return true;
        }
        if (strchr(s, '\\') || strchr(s, '/') || strstr(s, ".exe") || strstr(s, ".lnk") || strstr(s, ".bat")) {
            action->type = ACTION_EXEC;
            strncpy_s(action->data.command, MAX_COMMAND_LEN, s, _TRUNCATE);
            return true;
        }
        return false;
    }

    if (node->type == JSON_ARRAY) {
        if (node->arr.count == 0 || node->arr.count > MAX_COMBO_KEYS) return false;
        action->type = ACTION_COMBO;
        action->data.combo.count = 0;
        for (size_t i = 0; i < node->arr.count; i++) {
            if (node->arr.items[i].type != JSON_STRING) return false;
            WORD vk = key_name_to_vk(node->arr.items[i].str);
            if (!vk) return false;
            action->data.combo.vks[action->data.combo.count++] = vk;
        }
        return (action->data.combo.count > 0);
    }

    if (node->type == JSON_OBJECT) {
        const char *act_str = jget_str(node, "action");
        const char *cmd_str = jget_str(node, "command");
        if (!cmd_str) cmd_str = jget_str(node, "cmd");
        const char *tgt_str = jget_str(node, "target");
        if (!tgt_str) tgt_str = jget_str(node, "key");
        jnode_t *keys_arr = jget(node, "keys");

        if (act_str) {
            if (is_toggle_persistent_name(act_str)) {
                action->type = ACTION_TOGGLE_PERSISTENT;
                return true;
            }
            if (_stricmp(act_str, "exec") == 0 || _stricmp(act_str, "launch") == 0) {
                if (cmd_str) {
                    action->type = ACTION_EXEC;
                    strncpy_s(action->data.command, MAX_COMMAND_LEN, cmd_str, _TRUNCATE);
                    return true;
                }
            } else if (_stricmp(act_str, "combo") == 0) {
                if (keys_arr) return parse_layer_action_node(keys_arr, action);
                if (tgt_str) return parse_combo_str(tgt_str, action);
            } else if (_stricmp(act_str, "key") == 0 || _stricmp(act_str, "remap") == 0) {
                if (tgt_str) {
                    if (is_toggle_persistent_name(tgt_str)) {
                        action->type = ACTION_TOGGLE_PERSISTENT;
                        return true;
                    }
                    WORD vk = key_name_to_vk(tgt_str);
                    if (vk) {
                        action->type = ACTION_KEY;
                        action->data.target_vk = vk;
                        return true;
                    }
                }
            } else {
                WORD vk = key_name_to_vk(act_str);
                if (vk) {
                    action->type = ACTION_KEY;
                    action->data.target_vk = vk;
                    return true;
                }
            }
        }

        if (cmd_str) {
            action->type = ACTION_EXEC;
            strncpy_s(action->data.command, MAX_COMMAND_LEN, cmd_str, _TRUNCATE);
            return true;
        }
        if (tgt_str) {
            if (is_toggle_persistent_name(tgt_str)) {
                action->type = ACTION_TOGGLE_PERSISTENT;
                return true;
            }
            WORD vk = key_name_to_vk(tgt_str);
            if (vk) {
                action->type = ACTION_KEY;
                action->data.target_vk = vk;
                return true;
            }
        }
        if (keys_arr) return parse_layer_action_node(keys_arr, action);
    }
    return false;
}

static bool parse_shortcut_combo(const char *combo_str, global_shortcut_t *sc) {
    if (!combo_str || !*combo_str || !sc) return false;
    memset(sc, 0, sizeof(*sc));
    char buf[256];
    strncpy_s(buf, sizeof(buf), combo_str, _TRUNCATE);
    WORD toks[MAX_SHORTCUT_MODIFIERS + 2];
    uint8_t count = 0;
    char *ctx = NULL, *tok = strtok_s(buf, "+", &ctx);
    while (tok) {
        while (*tok && isspace((unsigned char)*tok)) tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
        if (*tok) {
            if (count >= MAX_SHORTCUT_MODIFIERS + 2) return false;
            WORD vk = key_name_to_vk(tok);
            if (!vk) return false;
            toks[count++] = vk;
        }
        tok = strtok_s(NULL, "+", &ctx);
    }
    if (!count) return false;
    int non_mod = -1;
    for (int i = (int)count - 1; i >= 0; i--) {
        if (!is_modifier_key(toks[i])) { non_mod = i; break; }
    }
    if (non_mod >= 0) {
        sc->trigger_vk = toks[non_mod];
        for (uint8_t i = 0; i < count; i++) {
            if ((int)i != non_mod && sc->mod_count < MAX_SHORTCUT_MODIFIERS)
                sc->modifiers[sc->mod_count++] = toks[i];
        }
    } else {
        sc->trigger_vk = toks[count - 1];
        for (uint8_t i = 0; i < count - 1; i++) {
            if (sc->mod_count < MAX_SHORTCUT_MODIFIERS)
                sc->modifiers[sc->mod_count++] = toks[i];
        }
    }
    return (sc->trigger_vk != 0);
}

void config_init_defaults(capslayer_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->settings.swap_esc_and_capslock = false;
    cfg->settings.capslock_tap_as_esc = true;
    cfg->settings.esc_tap_as_capslock = true;
    cfg->settings.unmapped_passthrough = true;
    cfg->settings.show_tray_icon = true;

    WORD vk_i = key_name_to_vk("i"), vk_j = key_name_to_vk("j"), vk_k = key_name_to_vk("k"), vk_l = key_name_to_vk("l");
    if (vk_i < 256) { cfg->layer_map[vk_i].type = ACTION_KEY; cfg->layer_map[vk_i].data.target_vk = VK_UP; }
    if (vk_j < 256) { cfg->layer_map[vk_j].type = ACTION_KEY; cfg->layer_map[vk_j].data.target_vk = VK_LEFT; }
    if (vk_k < 256) { cfg->layer_map[vk_k].type = ACTION_KEY; cfg->layer_map[vk_k].data.target_vk = VK_DOWN; }
    if (vk_l < 256) { cfg->layer_map[vk_l].type = ACTION_KEY; cfg->layer_map[vk_l].data.target_vk = VK_RIGHT; }

    WORD vk_h = key_name_to_vk("h"), vk_semi = key_name_to_vk(";"), vk_u = key_name_to_vk("u"), vk_d = key_name_to_vk("d");
    if (vk_h < 256) { cfg->layer_map[vk_h].type = ACTION_KEY; cfg->layer_map[vk_h].data.target_vk = VK_HOME; }
    if (vk_semi < 256) { cfg->layer_map[vk_semi].type = ACTION_KEY; cfg->layer_map[vk_semi].data.target_vk = VK_END; }
    if (vk_u < 256) { cfg->layer_map[vk_u].type = ACTION_KEY; cfg->layer_map[vk_u].data.target_vk = VK_PRIOR; }
    if (vk_d < 256) { cfg->layer_map[vk_d].type = ACTION_KEY; cfg->layer_map[vk_d].data.target_vk = VK_NEXT; }

    WORD vk_bs = key_name_to_vk("backspace"), vk_m = key_name_to_vk("m"), vk_n = key_name_to_vk("n"), vk_p = key_name_to_vk("p");
    if (vk_bs < 256) { cfg->layer_map[vk_bs].type = ACTION_KEY; cfg->layer_map[vk_bs].data.target_vk = VK_DELETE; }
    if (vk_m < 256) { cfg->layer_map[vk_m].type = ACTION_KEY; cfg->layer_map[vk_m].data.target_vk = VK_DELETE; }
    if (vk_n < 256) { cfg->layer_map[vk_n].type = ACTION_KEY; cfg->layer_map[vk_n].data.target_vk = VK_BACK; }
    if (vk_p < 256) { cfg->layer_map[vk_p].type = ACTION_TOGGLE_PERSISTENT; }

    WORD vk_w = key_name_to_vk("w"), vk_c = key_name_to_vk("c"), vk_v = key_name_to_vk("v"), vk_z = key_name_to_vk("z");
    if (vk_w < 256) {
        cfg->layer_map[vk_w].type = ACTION_COMBO;
        cfg->layer_map[vk_w].data.combo.count = 2;
        cfg->layer_map[vk_w].data.combo.vks[0] = VK_MENU;
        cfg->layer_map[vk_w].data.combo.vks[1] = VK_F4;
    }
    if (vk_c < 256) {
        cfg->layer_map[vk_c].type = ACTION_COMBO;
        cfg->layer_map[vk_c].data.combo.count = 2;
        cfg->layer_map[vk_c].data.combo.vks[0] = VK_CONTROL;
        cfg->layer_map[vk_c].data.combo.vks[1] = 0x43;
    }
    if (vk_v < 256) {
        cfg->layer_map[vk_v].type = ACTION_COMBO;
        cfg->layer_map[vk_v].data.combo.count = 2;
        cfg->layer_map[vk_v].data.combo.vks[0] = VK_CONTROL;
        cfg->layer_map[vk_v].data.combo.vks[1] = 0x56;
    }
    if (vk_z < 256) {
        cfg->layer_map[vk_z].type = ACTION_EXEC;
        strncpy_s(cfg->layer_map[vk_z].data.command, MAX_COMMAND_LEN, "wt.exe", _TRUNCATE);
    }
}

bool config_load_from_json_string(const char *json_str, capslayer_config_t *cfg) {
    if (!json_str || !*json_str || !cfg) return false;
    const char *s = json_str;
    jnode_t root;
    if (!parse_val(&s, &root) || root.type != JSON_OBJECT) {
        jnode_free(&root);
        return false;
    }

    config_init_defaults(cfg);

    jnode_t *st = jget(&root, "settings");
    if (st && st->type == JSON_OBJECT) {
        cfg->settings.swap_esc_and_capslock = jget_bool(st, "swap_esc_and_capslock", cfg->settings.swap_esc_and_capslock);
        cfg->settings.capslock_tap_as_esc = jget_bool(st, "capslock_tap_as_esc", cfg->settings.capslock_tap_as_esc);
        cfg->settings.esc_tap_as_capslock = jget_bool(st, "esc_tap_as_capslock", cfg->settings.esc_tap_as_capslock);
        cfg->settings.unmapped_passthrough = jget_bool(st, "unmapped_passthrough", cfg->settings.unmapped_passthrough);
        cfg->settings.show_tray_icon = jget_bool(st, "show_tray_icon", cfg->settings.show_tray_icon);
        cfg->settings.start_minimized = jget_bool(st, "start_minimized", cfg->settings.start_minimized);
    }

    jnode_t *layer = jget(&root, "layer");
    if (!layer) layer = jget(&root, "mappings");
    if (!layer) layer = jget(&root, "layer_mappings");
    if (!layer) layer = jget(&root, "bindings");

    if (layer && layer->type == JSON_OBJECT) {
        for (size_t i = 0; i < layer->obj.count; i++) {
            const char *kname = layer->obj.pairs[i].key;
            if (!kname) continue;
            WORD vk = key_name_to_vk(kname);
            if (vk > 0 && vk < 256) {
                layer_action_t act;
                if (parse_layer_action_node(&layer->obj.pairs[i], &act)) {
                    cfg->layer_map[vk] = act;
                }
            }
        }
    }

    jnode_t *scs = jget(&root, "shortcuts");
    if (!scs) scs = jget(&root, "hotkeys");
    if (!scs) scs = jget(&root, "global");
    if (!scs) scs = jget(&root, "programs");

    if (scs) {
        cfg->shortcut_count = 0;
        if (scs->type == JSON_OBJECT) {
            for (size_t i = 0; i < scs->obj.count && cfg->shortcut_count < MAX_SHORTCUTS; i++) {
                const char *cstr = scs->obj.pairs[i].key;
                global_shortcut_t sc;
                if (parse_shortcut_combo(cstr, &sc)) {
                    if (parse_layer_action_node(&scs->obj.pairs[i], &sc.action)) {
                        cfg->shortcuts[cfg->shortcut_count++] = sc;
                    }
                }
            }
        } else if (scs->type == JSON_ARRAY) {
            for (size_t i = 0; i < scs->arr.count && cfg->shortcut_count < MAX_SHORTCUTS; i++) {
                jnode_t *item = &scs->arr.items[i];
                if (item->type != JSON_OBJECT) continue;
                global_shortcut_t sc;
                memset(&sc, 0, sizeof(sc));
                jnode_t *karr = jget(item, "keys");
                if (karr && karr->type == JSON_ARRAY) {
                    WORD toks[MAX_SHORTCUT_MODIFIERS + 2];
                    uint8_t kcount = 0;
                    for (size_t k = 0; k < karr->arr.count && kcount < MAX_SHORTCUT_MODIFIERS + 2; k++) {
                        if (karr->arr.items[k].type == JSON_STRING) {
                            WORD vk = key_name_to_vk(karr->arr.items[k].str);
                            if (vk) toks[kcount++] = vk;
                        }
                    }
                    if (kcount > 0) {
                        int non_mod = -1;
                        for (int k = (int)kcount - 1; k >= 0; k--) {
                            if (!is_modifier_key(toks[k])) { non_mod = k; break; }
                        }
                        if (non_mod >= 0) {
                            sc.trigger_vk = toks[non_mod];
                            for (uint8_t k = 0; k < kcount; k++) {
                                if ((int)k != non_mod && sc.mod_count < MAX_SHORTCUT_MODIFIERS)
                                    sc.modifiers[sc.mod_count++] = toks[k];
                            }
                        } else {
                            sc.trigger_vk = toks[kcount - 1];
                            for (uint8_t k = 0; k < kcount - 1 && sc.mod_count < MAX_SHORTCUT_MODIFIERS; k++)
                                sc.modifiers[sc.mod_count++] = toks[k];
                        }
                    }
                } else {
                    const char *cstr = jget_str(item, "combo");
                    if (!cstr) cstr = jget_str(item, "shortcut");
                    if (cstr) parse_shortcut_combo(cstr, &sc);
                }
                if (sc.trigger_vk && parse_layer_action_node(item, &sc.action)) {
                    cfg->shortcuts[cfg->shortcut_count++] = sc;
                }
            }
        }
    }

    jnode_free(&root);
    return true;
}

bool config_load_from_file(const char *path, capslayer_config_t *cfg) {
    if (!path || !cfg) return false;
    FILE *f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (1024 * 1024)) { fclose(f); return false; }
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, len, f);
    fclose(f);
    buf[rd] = '\0';
    bool ok = config_load_from_json_string(buf, cfg);
    free(buf);
    return ok;
}

bool config_get_default_path(char *buf, size_t size) {
    if (!buf || size == 0) return false;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) {
        strncpy_s(buf, size, "config.json", _TRUNCATE);
        return true;
    }
    char *slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (slash) {
        *(slash + 1) = '\0';
        snprintf(buf, size, "%sconfig.json", path);
    } else {
        strncpy_s(buf, size, "config.json", _TRUNCATE);
    }
    return true;
}

bool config_get_dir_path(const char *cfg_path, char *dir_buf, size_t dir_size) {
    if (!cfg_path || !dir_buf || dir_size == 0) return false;
    strncpy_s(dir_buf, dir_size, cfg_path, _TRUNCATE);
    char *slash = strrchr(dir_buf, '\\');
    if (!slash) slash = strrchr(dir_buf, '/');
    if (slash) *slash = '\0';
    else strncpy_s(dir_buf, dir_size, ".", _TRUNCATE);
    return true;
}

/* ========================================================================= */
/* 4. Keyboard Hook & State Management                                      */
/* ========================================================================= */

static HHOOK g_hook_handle = NULL;
static CRITICAL_SECTION g_config_cs;
static bool g_cs_init = false;
static capslayer_config_t g_active_config;
static volatile bool g_paused = false;
static volatile bool g_persistent_layer = false;
static layer_state_callback_t g_state_cb = NULL;

static HANDLE g_blink_thread = NULL;
static HANDLE g_stop_blink_event = NULL;
static bool g_init_caps_state = false;

static bool g_capslock_down = false;
static bool g_capslock_layer_used = false;
static WORD g_active_injected[256] = { 0 };

static void init_cs_once(void) {
    if (!g_cs_init) { InitializeCriticalSection(&g_config_cs); g_cs_init = true; }
}

void hook_update_config(const capslayer_config_t *cfg) {
    if (!cfg) return;
    init_cs_once();
    EnterCriticalSection(&g_config_cs);
    g_active_config = *cfg;
    LeaveCriticalSection(&g_config_cs);
}

static void get_config_snapshot(capslayer_config_t *out) {
    init_cs_once();
    EnterCriticalSection(&g_config_cs);
    *out = g_active_config;
    LeaveCriticalSection(&g_config_cs);
}

bool hook_is_installed(void) { return (g_hook_handle != NULL); }
bool hook_is_paused(void) { return g_paused; }
bool hook_is_persistent_layer(void) { return g_persistent_layer; }

static unsigned __stdcall blink_worker(void *arg) {
    (void)arg;
    while (g_persistent_layer && !g_paused) {
        if (!(GetKeyState(VK_CAPITAL) & 0x0001)) send_key_tap(VK_CAPITAL);
        if (WaitForSingleObject(g_stop_blink_event, 1000) != WAIT_TIMEOUT) break;
        if (!g_persistent_layer || g_paused) break;
        if (GetKeyState(VK_CAPITAL) & 0x0001) send_key_tap(VK_CAPITAL);
        if (WaitForSingleObject(g_stop_blink_event, 1000) != WAIT_TIMEOUT) break;
    }
    if (((GetKeyState(VK_CAPITAL) & 0x0001) != 0) != g_init_caps_state) send_key_tap(VK_CAPITAL);
    return 0;
}

static void start_blinker(void) {
    if (g_blink_thread) return;
    g_init_caps_state = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    if (!g_stop_blink_event) g_stop_blink_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    else ResetEvent(g_stop_blink_event);
    uintptr_t th = _beginthreadex(NULL, 0, blink_worker, NULL, 0, NULL);
    if (th) g_blink_thread = (HANDLE)th;
}

static void stop_blinker(void) {
    if (g_stop_blink_event) SetEvent(g_stop_blink_event);
    if (g_blink_thread) {
        WaitForSingleObject(g_blink_thread, 1500);
        CloseHandle(g_blink_thread);
        g_blink_thread = NULL;
    }
    if (((GetKeyState(VK_CAPITAL) & 0x0001) != 0) != g_init_caps_state) send_key_tap(VK_CAPITAL);
}

static void release_injected_keys(void) {
    for (int i = 0; i < 256; ++i) {
        if (g_active_injected[i]) {
            send_key_event(g_active_injected[i], false);
            g_active_injected[i] = 0;
        }
    }
}

void hook_set_paused(bool paused) {
    g_paused = paused;
    if (paused) {
        stop_blinker();
        release_injected_keys();
        g_capslock_down = false;
        g_capslock_layer_used = false;
        g_persistent_layer = false;
    }
    if (g_state_cb) g_state_cb(g_paused, g_persistent_layer);
}

void hook_set_persistent_layer(bool active) {
    if (g_persistent_layer == active) return;
    g_persistent_layer = active;
    if (active) start_blinker();
    else { stop_blinker(); release_injected_keys(); }
    if (g_state_cb) g_state_cb(g_paused, g_persistent_layer);
}

bool hook_toggle_persistent_layer(void) {
    hook_set_persistent_layer(!g_persistent_layer);
    return g_persistent_layer;
}

void hook_set_state_callback(layer_state_callback_t cb) { g_state_cb = cb; }

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT *)lParam;
    if (!kbd || (kbd->flags & LLKHF_INJECTED) || (kbd->dwExtraInfo == (ULONG_PTR)MAGIC_INJECTED_FLAG) || g_paused) {
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    bool is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool is_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    WORD vk = (WORD)(kbd->vkCode & 0xFF);

    capslayer_config_t cfg;
    get_config_snapshot(&cfg);

    /* 1. Global Shortcuts */
    for (uint8_t i = 0; i < cfg.shortcut_count; ++i) {
        const global_shortcut_t *sc = &cfg.shortcuts[i];
        if (sc->trigger_vk == vk) {
            bool all_mods = true;
            for (uint8_t m = 0; m < sc->mod_count; ++m) {
                if (!is_modifier_down(sc->modifiers[m])) { all_mods = false; break; }
            }
            if (all_mods) {
                if (vk == VK_CAPITAL) g_capslock_layer_used = true;
                if (is_down) {
                    switch (sc->action.type) {
                        case ACTION_EXEC: spawn_process_async(sc->action.data.command); break;
                        case ACTION_COMBO: send_key_combo(sc->action.data.combo.vks, sc->action.data.combo.count); break;
                        case ACTION_KEY: send_key_tap(sc->action.data.target_vk); break;
                        case ACTION_TOGGLE_PERSISTENT: hook_toggle_persistent_layer(); break;
                        default: break;
                    }
                }
                return 1;
            }
        }
    }

    /* 2. Physical Escape */
    if (vk == VK_ESCAPE) {
        if (g_persistent_layer) {
            if (is_down) hook_set_persistent_layer(false);
            return 1;
        }
        if (cfg.settings.swap_esc_and_capslock || cfg.settings.esc_tap_as_capslock) {
            if (is_down) send_key_tap(VK_CAPITAL);
            return 1;
        }
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    /* 3. Physical CapsLock */
    if (vk == VK_CAPITAL) {
        if (is_down) {
            if (!g_capslock_down) { g_capslock_down = true; g_capslock_layer_used = false; }
            return 1;
        } else if (is_up) {
            g_capslock_down = false;
            release_injected_keys();
            if (!g_capslock_layer_used && (cfg.settings.capslock_tap_as_esc || cfg.settings.swap_esc_and_capslock)) {
                send_key_tap(VK_ESCAPE);
            }
            return 1;
        }
    }

    /* 4. Layer Navigation & Remapping */
    bool layer_active = g_capslock_down || g_persistent_layer;
    if (layer_active) {
        const layer_action_t *act = &cfg.layer_map[vk];
        if (act->type == ACTION_TOGGLE_PERSISTENT || (g_capslock_down && vk == 0x50)) {
            if (g_capslock_down) g_capslock_layer_used = true;
            if (is_down) hook_toggle_persistent_layer();
            return 1;
        }

        switch (act->type) {
            case ACTION_KEY: {
                if (g_capslock_down) g_capslock_layer_used = true;
                WORD tgt = act->data.target_vk;
                if (is_down) { g_active_injected[vk] = tgt; send_key_event(tgt, true); }
                else if (is_up) {
                    WORD active_vk = g_active_injected[vk] ? g_active_injected[vk] : tgt;
                    g_active_injected[vk] = 0;
                    send_key_event(active_vk, false);
                }
                return 1;
            }
            case ACTION_COMBO: {
                if (g_capslock_down) g_capslock_layer_used = true;
                if (is_down) send_key_combo(act->data.combo.vks, act->data.combo.count);
                return 1;
            }
            case ACTION_EXEC: {
                if (g_capslock_down) g_capslock_layer_used = true;
                if (is_down) spawn_process_async(act->data.command);
                return 1;
            }
            default: {
                if (is_up && g_active_injected[vk]) {
                    send_key_event(g_active_injected[vk], false);
                    g_active_injected[vk] = 0;
                    return 1;
                }
                if (cfg.settings.unmapped_passthrough) return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
                return 1;
            }
        }
    } else {
        if (is_up && g_active_injected[vk]) {
            send_key_event(g_active_injected[vk], false);
            g_active_injected[vk] = 0;
            return 1;
        }
    }

    return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
}

bool hook_install(HINSTANCE hInstance) {
    if (g_hook_handle) return true;
    init_cs_once();
    g_hook_handle = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
    return (g_hook_handle != NULL);
}

void hook_uninstall(void) {
    stop_blinker();
    if (g_hook_handle) { UnhookWindowsHookEx(g_hook_handle); g_hook_handle = NULL; }
    release_injected_keys();
    g_capslock_down = false;
    g_capslock_layer_used = false;
    g_persistent_layer = false;
}

/* ========================================================================= */
/* 5. System Tray Implementation                                            */
/* ========================================================================= */

static NOTIFYICONDATAW g_nid;
static bool g_tray_active = false;
static HICON g_icon_active = NULL, g_icon_locked = NULL, g_icon_paused = NULL;

static HICON create_status_icon(COLORREF bg_color, COLORREF fg_color, const wchar_t *letter) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;
    HDC hdc_screen = GetDC(NULL);
    HDC hdc_color = CreateCompatibleDC(hdc_screen), hdc_mask = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi = { .bmiHeader = {
        .biSize = sizeof(BITMAPINFOHEADER), .biWidth = size, .biHeight = -size, .biPlanes = 1, .biBitCount = 32, .biCompression = BI_RGB
    }};
    void *bits = NULL;
    HBITMAP hbm_color = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP hbm_mask = CreateBitmap(size, size, 1, 1, NULL);

    HBITMAP old_col = (HBITMAP)SelectObject(hdc_color, hbm_color);
    HBITMAP old_msk = (HBITMAP)SelectObject(hdc_mask, hbm_mask);

    HBRUSH bg = CreateSolidBrush(bg_color);
    RECT rc = { 0, 0, size, size };
    FillRect(hdc_color, &rc, bg);
    DeleteObject(bg);
    FillRect(hdc_mask, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    SetBkMode(hdc_color, TRANSPARENT);
    SetTextColor(hdc_color, fg_color);
    HFONT font = CreateFontW(size - 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT old_font = (HFONT)SelectObject(hdc_color, font);
    DrawTextW(hdc_color, letter ? letter : L"C", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc_color, old_font); DeleteObject(font);
    SelectObject(hdc_color, old_col); SelectObject(hdc_mask, old_msk);
    DeleteDC(hdc_color); DeleteDC(hdc_mask); ReleaseDC(NULL, hdc_screen);

    ICONINFO ii = { .fIcon = TRUE, .hbmColor = hbm_color, .hbmMask = hbm_mask };
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hbm_color); DeleteObject(hbm_mask);
    return hIcon;
}

bool tray_init(HWND hwnd, HINSTANCE hInstance) {
    (void)hInstance;
    if (g_tray_active) return true;
    g_icon_active = create_status_icon(RGB(0, 200, 83), RGB(255, 255, 255), L"C");
    g_icon_locked = create_status_icon(RGB(0, 176, 255), RGB(255, 255, 255), L"L");
    g_icon_paused = create_status_icon(RGB(117, 117, 117), RGB(255, 255, 255), L"C");

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_icon_active ? g_icon_active : LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"CapsLayer - Active");

    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) { g_tray_active = true; return true; }
    return false;
}

void tray_update_status(bool paused, bool persistent) {
    if (!g_tray_active) return;
    if (paused) {
        g_nid.hIcon = g_icon_paused ? g_icon_paused : LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"CapsLayer - Paused");
    } else if (persistent) {
        g_nid.hIcon = g_icon_locked ? g_icon_locked : (g_icon_active ? g_icon_active : LoadIcon(NULL, IDI_APPLICATION));
        wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"CapsLayer - Layer Locked (Caps+P)");
    } else {
        g_nid.hIcon = g_icon_active ? g_icon_active : LoadIcon(NULL, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"CapsLayer - Active");
    }
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void tray_show_menu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool paused = hook_is_paused(), persistent = hook_is_persistent_layer();
    if (paused) AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, L"CapsLayer (Paused)");
    else if (persistent) AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, L"CapsLayer (Layer Locked - Caps+P)");
    else AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, L"CapsLayer (Active)");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    UINT lock_fl = MF_STRING | (persistent ? MF_CHECKED : MF_UNCHECKED);
    if (paused) lock_fl |= (MF_DISABLED | MF_GRAYED);
    AppendMenuW(hMenu, lock_fl, IDM_TOGGLE_PERSISTENT, L"&Lock Layer (Caps+P)");
    AppendMenuW(hMenu, MF_STRING | (paused ? MF_UNCHECKED : MF_CHECKED), IDM_TOGGLE_ENABLE, L"&Enabled");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_RELOAD_CONFIG, L"&Reload Configuration");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_CONFIG, L"&Open config.json");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"&About CapsLayer");
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"E&xit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void tray_cleanup(void) {
    if (g_tray_active) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_tray_active = false; }
    if (g_icon_active) { DestroyIcon(g_icon_active); g_icon_active = NULL; }
    if (g_icon_locked) { DestroyIcon(g_icon_locked); g_icon_locked = NULL; }
    if (g_icon_paused) { DestroyIcon(g_icon_paused); g_icon_paused = NULL; }
}

/* ========================================================================= */
/* 6. Setup, Daemon & Main Entry                                             */
/* ========================================================================= */

#ifndef CAPSLAYER_NO_MAIN

typedef struct {
    char config_path[MAX_PATH];
    char config_dir[MAX_PATH];
    bool console_mode, test_config_only, paused_initial, no_elevate;
    bool do_install, do_uninstall, do_enable_startup, do_disable_startup, do_status;
    HWND hwnd;
    HANDLE stop_event, watcher_thread, mutex;
} AppContext;

static AppContext g_app;

static void log_msg(const char *fmt, ...) {
    if (!g_app.console_mode) return;
    va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args);
    printf("\n"); fflush(stdout);
}

static bool run_cmd_hidden(const wchar_t *cmd) {
    STARTUPINFOW si = { .cb = sizeof(si), .dwFlags = STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE };
    PROCESS_INFORMATION pi;
    wchar_t buf[1024];
    wcsncpy_s(buf, sizeof(buf) / sizeof(wchar_t), cmd, _TRUNCATE);
    if (!CreateProcessW(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (code == 0);
}

static void get_prog_files(wchar_t *buf, size_t len) {
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", buf, (DWORD)len) && buf[0]) return;
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, (DWORD)len) && buf[0]) return;
    wcscpy_s(buf, len, L"C:\\Program Files (x86)");
}

static bool is_admin(void) {
    BOOL adm = FALSE; HANDLE tok = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION el; DWORD sz = sizeof(el);
        if (GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz)) adm = el.TokenIsElevated != 0;
        CloseHandle(tok);
    }
    return (adm != 0);
}

static bool relaunch_admin(int argc, char *argv[]) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;
    wchar_t args[2048] = { 0 };
    for (int i = 1; i < argc; ++i) {
        wchar_t warg[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, warg, MAX_PATH);
        if (i > 1) wcscat_s(args, sizeof(args) / sizeof(wchar_t), L" ");
        wcscat_s(args, sizeof(args) / sizeof(wchar_t), warg);
    }
    SHELLEXECUTEINFOW sei = { .cbSize = sizeof(sei), .lpVerb = L"runas", .lpFile = exe, .lpParameters = args[0] ? args : NULL, .nShow = SW_SHOWNORMAL, .fMask = SEE_MASK_NOASYNC };
    return (ShellExecuteExW(&sei) != 0);
}

static bool setup_enable_startup(void) {
    wchar_t pf[MAX_PATH], dir[MAX_PATH], exe[MAX_PATH];
    get_prog_files(pf, MAX_PATH);
    swprintf_s(dir, MAX_PATH, L"%s\\capslayer", pf);
    swprintf_s(exe, MAX_PATH, L"%s\\capslayer.exe", dir);
    run_cmd_hidden(L"reg delete \"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_cmd_hidden(L"reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    wchar_t ps[2048];
    swprintf_s(ps, sizeof(ps) / sizeof(wchar_t),
        L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$a = New-ScheduledTaskAction -Execute '%ls' -WorkingDirectory '%ls'; $t = New-ScheduledTaskTrigger -AtLogOn; $p = New-ScheduledTaskPrincipal -GroupId 'BUILTIN\\Users' -RunLevel Highest; $s = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan); Register-ScheduledTask -TaskName 'CapsLayer' -Action $a -Trigger $t -Principal $p -Settings $s -Force\"",
        exe, dir);
    return run_cmd_hidden(ps);
}

static bool setup_disable_startup(void) {
    run_cmd_hidden(L"schtasks /Delete /TN \"CapsLayer\" /F");
    run_cmd_hidden(L"reg delete \"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_cmd_hidden(L"reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    return true;
}

static bool setup_install(void) {
    wchar_t src_exe[MAX_PATH], pf[MAX_PATH], dir[MAX_PATH], dst_exe[MAX_PATH], dst_cfg[MAX_PATH], src_cfg[MAX_PATH];
    if (!GetModuleFileNameW(NULL, src_exe, MAX_PATH)) return false;
    get_prog_files(pf, MAX_PATH);
    swprintf_s(dir, MAX_PATH, L"%s\\capslayer", pf);
    swprintf_s(dst_exe, MAX_PATH, L"%s\\capslayer.exe", dir);
    swprintf_s(dst_cfg, MAX_PATH, L"%s\\config.json", dir);

    printf("===================================================\n      CapsLayer Automated Installer\n===================================================\n");
    run_cmd_hidden(L"taskkill /F /IM capslayer.exe");
    Sleep(500);
    CreateDirectoryW(dir, NULL);

    if (!CopyFileW(src_exe, dst_exe, FALSE)) {
        printf("[Error] Failed to copy capslayer.exe\n");
        return false;
    }
    printf("  - Copied capslayer.exe\n");

    wcscpy_s(src_cfg, MAX_PATH, src_exe);
    wchar_t *s = wcsrchr(src_cfg, L'\\'); if (s) *s = L'\0';
    wcscat_s(src_cfg, MAX_PATH, L"\\config.json");
    if (GetFileAttributesW(dst_cfg) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(src_cfg) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(src_cfg, dst_cfg, FALSE);
        printf("  - Copied default config.json\n");
    }

    wchar_t acl[1024];
    swprintf_s(acl, sizeof(acl) / sizeof(wchar_t), L"icacls \"%ls\" /grant *S-1-5-32-545:(OI)(CI)M /T /Q", dir);
    run_cmd_hidden(acl);

    setup_enable_startup();
    printf("  - Windows Task Scheduler registered (Elevated on Logon)\n");

    wchar_t sc[2048];
    swprintf_s(sc, sizeof(sc) / sizeof(wchar_t),
        L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ws = New-Object -ComObject WScript.Shell; $p = [System.IO.Path]::Combine($env:ProgramData, 'Microsoft\\Windows\\Start Menu\\Programs\\CapsLayer.lnk'); $s = $ws.CreateShortcut($p); $s.TargetPath = '%ls'; $s.WorkingDirectory = '%ls'; $s.Save()\"", dst_exe, dir);
    run_cmd_hidden(sc);

    ShellExecuteW(NULL, L"open", dst_exe, NULL, dir, SW_SHOWNORMAL);
    printf("\n[SUCCESS] CapsLayer installed successfully to %ls\n", dir);
    return true;
}

static bool setup_uninstall(void) {
    wchar_t pf[MAX_PATH], dir[MAX_PATH], del_lnk[1024], rd[1024];
    get_prog_files(pf, MAX_PATH);
    swprintf_s(dir, MAX_PATH, L"%s\\capslayer", pf);

    printf("===================================================\n            CapsLayer Uninstaller\n===================================================\n");
    run_cmd_hidden(L"taskkill /F /IM capslayer.exe");
    Sleep(500);
    setup_disable_startup();

    swprintf_s(del_lnk, sizeof(del_lnk) / sizeof(wchar_t), L"del /F /Q \"%s\\Microsoft\\Windows\\Start Menu\\Programs\\CapsLayer.lnk\"", _wgetenv(L"ProgramData") ? _wgetenv(L"ProgramData") : L"C:\\ProgramData");
    run_cmd_hidden(del_lnk);

    swprintf_s(rd, sizeof(rd) / sizeof(wchar_t), L"rmdir /S /Q \"%ls\"", dir);
    run_cmd_hidden(rd);
    printf("[SUCCESS] CapsLayer uninstalled successfully.\n");
    return true;
}

static void reload_config(void) {
    capslayer_config_t cfg;
    if (!config_load_from_file(g_app.config_path, &cfg)) {
        log_msg("[Config] Failed to load '%s'. Using defaults.", g_app.config_path);
        config_init_defaults(&cfg);
    } else {
        log_msg("[Config] Loaded '%s'.", g_app.config_path);
    }
    hook_update_config(&cfg);
}

static unsigned __stdcall config_watcher(void *arg) {
    AppContext *app = (AppContext *)arg;
    if (!app || !app->config_dir[0]) return 0;
    wchar_t wdir[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, app->config_dir, -1, wdir, MAX_PATH);
    HANDLE hDir = CreateFileW(wdir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (hDir == INVALID_HANDLE_VALUE) return 0;

    BYTE buf[1024]; OVERLAPPED ov = { 0 }; ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE wait_h[2] = { app->stop_event, ov.hEvent };
    DWORD last_tick = 0;

    while (true) {
        ResetEvent(ov.hEvent);
        DWORD ret = 0;
        if (!ReadDirectoryChangesW(hDir, buf, sizeof(buf), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE, &ret, &ov, NULL)) break;
        DWORD wr = WaitForMultipleObjects(2, wait_h, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0) { CancelIo(hDir); break; }
        if (wr == WAIT_OBJECT_0 + 1) {
            DWORD tick = GetTickCount();
            if (tick - last_tick > 250) {
                last_tick = tick;
                Sleep(50);
                if (app->hwnd) PostMessageW(app->hwnd, WM_USER_RELOAD_CONFIG, 0, 0);
            }
        } else break;
    }
    CloseHandle(ov.hEvent); CloseHandle(hDir);
    return 0;
}

static void on_layer_state(bool p, bool l) {
    (void)p; (void)l;
    if (g_app.hwnd) PostMessageW(g_app.hwnd, WM_USER_STATE_CHANGED, 0, 0);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) tray_show_menu(hwnd);
            else if (lParam == WM_LBUTTONDBLCLK) {
                bool next = !hook_is_paused();
                hook_set_paused(next);
                tray_update_status(next, hook_is_persistent_layer());
            }
            return 0;
        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDM_TOGGLE_PERSISTENT) {
                bool l = hook_toggle_persistent_layer();
                tray_update_status(hook_is_paused(), l);
            } else if (id == IDM_TOGGLE_ENABLE) {
                bool next = !hook_is_paused();
                hook_set_paused(next);
                tray_update_status(next, hook_is_persistent_layer());
            } else if (id == IDM_RELOAD_CONFIG) reload_config();
            else if (id == IDM_OPEN_CONFIG) {
                wchar_t wp[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, g_app.config_path, -1, wp, MAX_PATH);
                ShellExecuteW(hwnd, L"open", wp, NULL, NULL, SW_SHOWNORMAL);
            } else if (id == IDM_ABOUT) {
                MessageBoxW(hwnd, L"CapsLayer v" TEXT(CAPSLAYER_VERSION) L"\n\nFast Windows Keyboard Remapper & Layer Daemon in C\n\nDual-role CapsLock / Escape, navigation layer, and persistent lock.", L"About CapsLayer", MB_OK | MB_ICONINFORMATION);
            } else if (id == IDM_EXIT) DestroyWindow(hwnd);
            return 0;
        }
        case WM_USER_STATE_CHANGED: tray_update_status(hook_is_paused(), hook_is_persistent_layer()); return 0;
        case WM_USER_RELOAD_CONFIG: reload_config(); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static void attach_console(bool force_alloc) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD ft = (hOut && hOut != INVALID_HANDLE_VALUE) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
    if (ft == FILE_TYPE_PIPE || ft == FILE_TYPE_DISK) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0 || (force_alloc && AllocConsole())) {
        FILE *fp; freopen_s(&fp, "CONOUT$", "w", stdout); freopen_s(&fp, "CONOUT$", "w", stderr);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1) attach_console(false);
    memset(&g_app, 0, sizeof(g_app));
    config_get_default_path(g_app.config_path, sizeof(g_app.config_path));

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help") || !strcmp(a, "/?")) {
            attach_console(false);
            printf("CapsLayer v%s - Fast Windows Keyboard Remapper\n\nOptions:\n  -c, --config <path>   Config path\n  -t, --test-config     Validate config and exit\n      --install         Install & enable startup\n      --uninstall       Uninstall & remove startup\n      --enable-startup  Register Task Scheduler startup\n      --disable-startup Remove Task Scheduler startup\n      --status          Show status\n      --console         Diagnostic console\n      --paused          Start paused\n      --no-elevate      Skip auto-elevation\n  -v, --version         Show version\n  -h, --help            Show help\n", CAPSLAYER_VERSION);
            return 0;
        } else if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
            attach_console(false); printf("CapsLayer version %s\n", CAPSLAYER_VERSION); return 0;
        } else if (!strcmp(a, "-t") || !strcmp(a, "--test-config")) g_app.test_config_only = true;
        else if (!strcmp(a, "--install") || !strcmp(a, "/install") || !strcmp(a, "-i")) g_app.do_install = true;
        else if (!strcmp(a, "--uninstall") || !strcmp(a, "/uninstall") || !strcmp(a, "-u")) g_app.do_uninstall = true;
        else if (!strcmp(a, "--enable-startup")) g_app.do_enable_startup = true;
        else if (!strcmp(a, "--disable-startup")) g_app.do_disable_startup = true;
        else if (!strcmp(a, "--status")) g_app.do_status = true;
        else if (!strcmp(a, "--console")) g_app.console_mode = true;
        else if (!strcmp(a, "--paused")) g_app.paused_initial = true;
        else if (!strcmp(a, "--no-elevate")) g_app.no_elevate = true;
        else if ((!strcmp(a, "-c") || !strcmp(a, "--config")) && (i + 1 < argc)) {
            strncpy_s(g_app.config_path, sizeof(g_app.config_path), argv[++i], _TRUNCATE);
        }
    }
    config_get_dir_path(g_app.config_path, g_app.config_dir, sizeof(g_app.config_dir));

    if (g_app.test_config_only) {
        capslayer_config_t cfg;
        if (!config_load_from_file(g_app.config_path, &cfg)) {
            printf("[CapsLayer] ERROR: Failed to parse '%s'\n", g_app.config_path);
            return 1;
        }
        printf("[CapsLayer] SUCCESS: Configuration '%s' is valid.\n", g_app.config_path);
        return 0;
    }

    if (g_app.do_status) {
        wchar_t pf[MAX_PATH], exe[MAX_PATH]; get_prog_files(pf, MAX_PATH);
        swprintf_s(exe, MAX_PATH, L"%s\\capslayer\\capslayer.exe", pf);
        printf("CapsLayer Status:\n  Installed: %s\n  Startup: %s\n",
            (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) ? "YES" : "NO",
            run_cmd_hidden(L"schtasks /Query /TN \"CapsLayer\"") ? "ENABLED" : "DISABLED");
        return 0;
    }

    if (g_app.do_install || g_app.do_uninstall || g_app.do_enable_startup || g_app.do_disable_startup) {
        if (!g_app.no_elevate && !is_admin() && relaunch_admin(argc, argv)) return 0;
        if (g_app.do_install) return setup_install() ? 0 : 1;
        if (g_app.do_uninstall) return setup_uninstall() ? 0 : 1;
        if (g_app.do_enable_startup) return setup_enable_startup() ? 0 : 1;
        if (g_app.do_disable_startup) return setup_disable_startup() ? 0 : 1;
    }

    if (g_app.console_mode) attach_console(true);

    g_app.mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_app.console_mode) printf("CapsLayer is already running.\n");
        if (g_app.mutex) CloseHandle(g_app.mutex);
        return 0;
    }

    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASSEXW wc = { .cbSize = sizeof(wc), .lpfnWndProc = MainWndProc, .hInstance = hInst, .lpszClassName = WINDOW_CLASS_NAME };
    RegisterClassExW(&wc);

    g_app.hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInst, NULL);
    if (!g_app.hwnd) return 1;

    reload_config();
    tray_init(g_app.hwnd, hInst);
    if (g_app.paused_initial) { hook_set_paused(true); tray_update_status(true, false); }
    hook_set_state_callback(on_layer_state);

    if (!hook_install(hInst)) {
        tray_cleanup(); DestroyWindow(g_app.hwnd); UnregisterClassW(WINDOW_CLASS_NAME, hInst);
        return 1;
    }

    g_app.stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    uintptr_t th = _beginthreadex(NULL, 0, config_watcher, &g_app, 0, NULL);
    if (th) g_app.watcher_thread = (HANDLE)th;

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.stop_event) {
        SetEvent(g_app.stop_event);
        if (g_app.watcher_thread) { WaitForSingleObject(g_app.watcher_thread, 1500); CloseHandle(g_app.watcher_thread); }
        CloseHandle(g_app.stop_event);
    }

    hook_uninstall();
    tray_cleanup();
    UnregisterClassW(WINDOW_CLASS_NAME, hInst);
    if (g_app.mutex) { ReleaseMutex(g_app.mutex); CloseHandle(g_app.mutex); }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;
    return main(__argc, __argv);
}

#endif /* CAPSLAYER_NO_MAIN */
