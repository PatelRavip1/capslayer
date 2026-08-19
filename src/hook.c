#include "hook.h"
#include "keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

static HHOOK g_hook_handle = NULL;
static CRITICAL_SECTION g_config_cs;
static bool g_cs_initialized = false;
static capslayer_config_t g_config;
static volatile bool g_paused = false;
static volatile bool g_persistent_layer = false;
static layer_state_callback_t g_state_callback = NULL;

/* CapsLock LED blinker thread resources */
static HANDLE g_blink_thread = NULL;
static HANDLE g_stop_blink_event = NULL;
static bool g_initial_caps_state = false;

/* State tracking invariants */
static bool g_capslock_down = false;
static bool g_capslock_layer_used = false;
static WORD g_active_injected_vk[256] = { 0 };

static void init_critical_section_once(void)
{
    if (!g_cs_initialized) {
        InitializeCriticalSection(&g_config_cs);
        g_cs_initialized = true;
    }
}

static void get_config_snapshot(capslayer_config_t *out_cfg)
{
    init_critical_section_once();
    EnterCriticalSection(&g_config_cs);
    *out_cfg = g_config;
    LeaveCriticalSection(&g_config_cs);
}

void hook_update_config(const capslayer_config_t *cfg)
{
    if (!cfg) return;
    init_critical_section_once();
    EnterCriticalSection(&g_config_cs);
    g_config = *cfg;
    LeaveCriticalSection(&g_config_cs);
}

bool hook_is_installed(void)
{
    return (g_hook_handle != NULL);
}

bool hook_is_paused(void)
{
    return g_paused;
}

static unsigned __stdcall capslock_blink_worker(void *arg)
{
    (void)arg;
    while (g_persistent_layer && !g_paused) {
        /* Turn CapsLock LED ON (1 second) */
        if (!(GetKeyState(VK_CAPITAL) & 0x0001)) {
            send_key_tap(VK_CAPITAL);
        }
        if (WaitForSingleObject(g_stop_blink_event, 1000) != WAIT_TIMEOUT) {
            break;
        }
        if (!g_persistent_layer || g_paused) break;

        /* Turn CapsLock LED OFF (1 second) */
        if (GetKeyState(VK_CAPITAL) & 0x0001) {
            send_key_tap(VK_CAPITAL);
        }
        if (WaitForSingleObject(g_stop_blink_event, 1000) != WAIT_TIMEOUT) {
            break;
        }
    }

    /* Restore CapsLock state when exiting persistent mode */
    bool current_state = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    if (current_state != g_initial_caps_state) {
        send_key_tap(VK_CAPITAL);
    }
    return 0;
}

static void start_capslock_blinker(void)
{
    if (g_blink_thread != NULL) return;

    g_initial_caps_state = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

    if (!g_stop_blink_event) {
        g_stop_blink_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    } else {
        ResetEvent(g_stop_blink_event);
    }

    uintptr_t th = _beginthreadex(NULL, 0, capslock_blink_worker, NULL, 0, NULL);
    if (th != 0) {
        g_blink_thread = (HANDLE)th;
    }
}

static void stop_capslock_blinker(void)
{
    if (g_stop_blink_event) {
        SetEvent(g_stop_blink_event);
    }
    if (g_blink_thread) {
        WaitForSingleObject(g_blink_thread, 1500);
        CloseHandle(g_blink_thread);
        g_blink_thread = NULL;
    }

    /* Restore CapsLock state */
    bool current_state = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    if (current_state != g_initial_caps_state) {
        send_key_tap(VK_CAPITAL);
    }
}

void hook_set_paused(bool paused)
{
    g_paused = paused;
    if (paused) {
        stop_capslock_blinker();

        /* Release all active injected keys when paused */
        for (int i = 0; i < 256; ++i) {
            if (g_active_injected_vk[i] != 0) {
                send_key_event(g_active_injected_vk[i], false);
                g_active_injected_vk[i] = 0;
            }
        }
        g_capslock_down = false;
        g_capslock_layer_used = false;
        g_persistent_layer = false;
    }

    if (g_state_callback) {
        g_state_callback(g_paused, g_persistent_layer);
    }
}

bool hook_is_persistent_layer(void)
{
    return g_persistent_layer;
}

void hook_set_persistent_layer(bool active)
{
    if (g_persistent_layer == active) return;

    g_persistent_layer = active;

    if (active) {
        start_capslock_blinker();
    } else {
        stop_capslock_blinker();

        /* Release active keys on deactivation */
        for (int i = 0; i < 256; ++i) {
            if (g_active_injected_vk[i] != 0) {
                send_key_event(g_active_injected_vk[i], false);
                g_active_injected_vk[i] = 0;
            }
        }
    }

    if (g_state_callback) {
        g_state_callback(g_paused, g_persistent_layer);
    }
}

bool hook_toggle_persistent_layer(void)
{
    hook_set_persistent_layer(!g_persistent_layer);
    return g_persistent_layer;
}

void hook_set_state_callback(layer_state_callback_t cb)
{
    g_state_callback = cb;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION) {
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT *)lParam;
    if (!kbd) {
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    /* 1. Recursion / Injection Guard: ignore synthetic keystrokes */
    if ((kbd->flags & LLKHF_INJECTED) || (kbd->dwExtraInfo == (ULONG_PTR)MAGIC_INJECTED_FLAG)) {
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    if (g_paused) {
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    bool is_down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool is_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    WORD vk = (WORD)(kbd->vkCode & 0xFF);

    capslayer_config_t cfg;
    get_config_snapshot(&cfg);

    /* 2. Global Shortcuts & Hotkeys Dispatch */
    if (cfg.shortcut_count > 0) {
        for (uint8_t i = 0; i < cfg.shortcut_count; ++i) {
            const global_shortcut_t *sc = &cfg.shortcuts[i];
            if (sc->trigger_vk == vk) {
                bool all_mods_down = true;
                for (uint8_t m = 0; m < sc->mod_count; ++m) {
                    if (!is_modifier_down(sc->modifiers[m])) {
                        all_mods_down = false;
                        break;
                    }
                }

                if (all_mods_down) {
                    if (vk == VK_CAPITAL) {
                        g_capslock_layer_used = true;
                    }

                    if (is_down) {
                        switch (sc->action.type) {
                            case ACTION_EXEC:
                                spawn_process_async(sc->action.data.command);
                                break;
                            case ACTION_COMBO:
                                send_key_combo(sc->action.data.combo.vks, sc->action.data.combo.count);
                                break;
                            case ACTION_KEY:
                                send_key_tap(sc->action.data.target_vk);
                                break;
                            case ACTION_TOGGLE_PERSISTENT:
                                hook_toggle_persistent_layer();
                                break;
                            default:
                                break;
                        }
                    }
                    return 1; /* Consume event */
                }
            }
        }
    }

    /* 3. Physical Escape Handling */
    if (vk == VK_ESCAPE) {
        /* If persistent layer lock is active, tapping Escape unlocks it cleanly */
        if (g_persistent_layer) {
            if (is_down) {
                hook_set_persistent_layer(false);
            }
            return 1;
        }

        if (cfg.settings.swap_esc_and_capslock || cfg.settings.esc_tap_as_capslock) {
            if (is_down) {
                send_key_tap(VK_CAPITAL);
            }
            return 1; /* Consume event */
        }
        return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
    }

    /* 4. Physical CapsLock Handling */
    if (vk == VK_CAPITAL) {
        if (is_down) {
            if (!g_capslock_down) {
                g_capslock_down = true;
                g_capslock_layer_used = false;
            }
            return 1; /* Consume event to prevent toggling CapsLock LED / state */
        } else if (is_up) {
            g_capslock_down = false;

            /* Release any held remapped synthetic keys */
            for (int i = 0; i < 256; ++i) {
                if (g_active_injected_vk[i] != 0) {
                    send_key_event(g_active_injected_vk[i], false);
                    g_active_injected_vk[i] = 0;
                }
            }

            /* If CapsLock was tapped without triggering any layer actions, emit Escape */
            if (!g_capslock_layer_used && (cfg.settings.capslock_tap_as_esc || cfg.settings.swap_esc_and_capslock)) {
                send_key_tap(VK_ESCAPE);
            }

            return 1; /* Consume event */
        }
    }

    /* 5. Layer Mapping & Key Remapping */
    bool layer_active = g_capslock_down || g_persistent_layer;

    if (layer_active) {
        const layer_action_t *action = &cfg.layer_map[vk];

        /* Fallback: 'P' (0x50) always toggles persistent mode under CapsLock */
        if (action->type == ACTION_TOGGLE_PERSISTENT || (g_capslock_down && vk == 0x50)) {
            if (g_capslock_down) {
                g_capslock_layer_used = true;
            }
            if (is_down) {
                hook_toggle_persistent_layer();
            }
            return 1;
        }

        switch (action->type) {
            case ACTION_KEY: {
                if (g_capslock_down) {
                    g_capslock_layer_used = true;
                }
                WORD target_vk = action->data.target_vk;
                if (is_down) {
                    g_active_injected_vk[vk] = target_vk;
                    send_key_event(target_vk, true);
                } else if (is_up) {
                    WORD active_vk = g_active_injected_vk[vk] ? g_active_injected_vk[vk] : target_vk;
                    g_active_injected_vk[vk] = 0;
                    send_key_event(active_vk, false);
                }
                return 1;
            }

            case ACTION_COMBO: {
                if (g_capslock_down) {
                    g_capslock_layer_used = true;
                }
                if (is_down) {
                    send_key_combo(action->data.combo.vks, action->data.combo.count);
                }
                return 1;
            }

            case ACTION_EXEC: {
                if (g_capslock_down) {
                    g_capslock_layer_used = true;
                }
                if (is_down) {
                    spawn_process_async(action->data.command);
                }
                return 1;
            }

            case ACTION_NONE:
            default: {
                if (g_active_injected_vk[vk] != 0) {
                    if (is_up) {
                        send_key_event(g_active_injected_vk[vk], false);
                        g_active_injected_vk[vk] = 0;
                        return 1;
                    }
                }
                if (cfg.settings.unmapped_passthrough) {
                    return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
                }
                return 1;
            }
        }
    } else {
        /* If layer is not active, but this key had a synthetic key injected, clean it up on KeyUp */
        if (is_up && g_active_injected_vk[vk] != 0) {
            send_key_event(g_active_injected_vk[vk], false);
            g_active_injected_vk[vk] = 0;
            return 1;
        }
    }

    return CallNextHookEx(g_hook_handle, nCode, wParam, lParam);
}

bool hook_install(HINSTANCE hInstance)
{
    if (g_hook_handle != NULL) {
        return true;
    }

    init_critical_section_once();

    g_hook_handle = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        hInstance,
        0
    );

    return (g_hook_handle != NULL);
}

void hook_uninstall(void)
{
    stop_capslock_blinker();

    if (g_hook_handle != NULL) {
        UnhookWindowsHookEx(g_hook_handle);
        g_hook_handle = NULL;
    }

    /* Release all active injected keys */
    for (int i = 0; i < 256; ++i) {
        if (g_active_injected_vk[i] != 0) {
            send_key_event(g_active_injected_vk[i], false);
            g_active_injected_vk[i] = 0;
        }
    }
    g_capslock_down = false;
    g_capslock_layer_used = false;
    g_persistent_layer = false;
}
