#ifndef CAPSLAYER_HOOK_H
#define CAPSLAYER_HOOK_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Installs WH_KEYBOARD_LL hook */
bool hook_install(HINSTANCE hInstance);

/* Uninstalls hook */
void hook_uninstall(void);

/* Checks if hook is currently active and installed */
bool hook_is_installed(void);

/* Checks if layer remapping is paused */
bool hook_is_paused(void);

/* Pauses or resumes layer remapping */
void hook_set_paused(bool paused);

/* Checks if persistent (locked) layer mode is active */
bool hook_is_persistent_layer(void);

/* Sets persistent (locked) layer mode */
void hook_set_persistent_layer(bool active);

/* Toggles persistent (locked) layer mode */
bool hook_toggle_persistent_layer(void);

/* Callback notification hook for layer state changes (e.g. tray icon update) */
typedef void (*layer_state_callback_t)(bool is_paused, bool is_persistent);
void hook_set_state_callback(layer_state_callback_t cb);

/* Updates current active configuration thread-safely */
void hook_update_config(const capslayer_config_t *cfg);

/* Low-level hook procedure for testing / direct dispatch */
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif /* CAPSLAYER_HOOK_H */
