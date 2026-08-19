#ifndef CAPSLAYER_H
#define CAPSLAYER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPSLAYER_VERSION "1.0.0"
#define MAGIC_INJECTED_FLAG ((uintptr_t)0xC4951A9E)
#define MAX_COMBO_KEYS 8
#define MAX_COMMAND_LEN 512
#define MAX_SHORTCUTS 32
#define MAX_SHORTCUT_MODIFIERS 4

#define WM_TRAYICON            (WM_USER + 1)
#define WM_USER_RELOAD_CONFIG  (WM_USER + 2)
#define WM_USER_STATE_CHANGED  (WM_USER + 3)

#define IDM_STATUS             1001
#define IDM_TOGGLE_ENABLE      1002
#define IDM_RELOAD_CONFIG      1003
#define IDM_OPEN_CONFIG        1004
#define IDM_ABOUT              1005
#define IDM_EXIT               1006
#define IDM_TOGGLE_PERSISTENT  1007

typedef enum {
    ACTION_NONE = 0,
    ACTION_KEY,
    ACTION_COMBO,
    ACTION_EXEC,
    ACTION_TOGGLE_PERSISTENT
} action_type_t;

typedef struct {
    action_type_t type;
    union {
        WORD target_vk;
        struct {
            WORD vks[MAX_COMBO_KEYS];
            uint8_t count;
        } combo;
        char command[MAX_COMMAND_LEN];
    } data;
} layer_action_t;

typedef struct {
    WORD modifiers[MAX_SHORTCUT_MODIFIERS];
    uint8_t mod_count;
    WORD trigger_vk;
    layer_action_t action;
} global_shortcut_t;

typedef struct {
    WORD modifier_vk;
    bool swap_esc_and_capslock;
    bool capslock_tap_as_esc;
    bool esc_tap_as_capslock;
    bool modifier_tap_as_esc;
    bool unmapped_passthrough;
    bool show_tray_icon;
    bool start_minimized;
} config_settings_t;

typedef struct {
    config_settings_t settings;
    WORD remap_map[256];
    layer_action_t layer_map[256];
    global_shortcut_t shortcuts[MAX_SHORTCUTS];
    uint8_t shortcut_count;
} capslayer_config_t;

/* Key & Input Handling */
WORD key_name_to_vk(const char *name);
bool is_extended_key(WORD vk);
bool is_modifier_key(WORD vk);
bool is_modifier_down(WORD vk);
void send_key_event(WORD vk, bool is_down);
void send_key_tap(WORD vk);
void send_key_combo(const WORD *vks, size_t count);
void spawn_process_async(const char *command_utf8);

/* Config Management */
void config_init_defaults(capslayer_config_t *cfg);
bool config_load_from_json_string(const char *json_str, capslayer_config_t *cfg);
bool config_load_from_file(const char *path, capslayer_config_t *cfg);
bool config_get_default_path(char *buffer, size_t buffer_size);
bool config_get_dir_path(const char *config_path, char *dir_buf, size_t dir_size);

/* Hook & State Management */
typedef void (*layer_state_callback_t)(bool is_paused, bool is_persistent);
bool hook_install(HINSTANCE hInstance);
void hook_uninstall(void);
bool hook_is_installed(void);
bool hook_is_paused(void);
void hook_set_paused(bool paused);
bool hook_is_persistent_layer(void);
void hook_set_persistent_layer(bool active);
bool hook_toggle_persistent_layer(void);
void hook_set_state_callback(layer_state_callback_t cb);
void hook_update_config(const capslayer_config_t *cfg);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

/* System Tray */
bool tray_init(HWND hwnd, HINSTANCE hInstance);
void tray_update_status(bool paused, bool persistent);
void tray_show_menu(HWND hwnd);
void tray_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* CAPSLAYER_H */
