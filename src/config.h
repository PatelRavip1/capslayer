#ifndef CAPSLAYER_CONFIG_H
#define CAPSLAYER_CONFIG_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "keys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_COMBO_KEYS 8
#define MAX_COMMAND_LEN 512
#define MAX_SHORTCUTS 32
#define MAX_SHORTCUT_MODIFIERS 4

typedef enum {
    ACTION_NONE = 0,
    ACTION_KEY,              /* Remap to another single virtual key */
    ACTION_COMBO,            /* Remap to key combination (e.g. Ctrl+C) */
    ACTION_EXEC,             /* Launch external command */
    ACTION_TOGGLE_PERSISTENT /* Toggle persistent / locked layer mode */
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
    bool swap_esc_and_capslock;
    bool capslock_tap_as_esc;
    bool esc_tap_as_capslock;
    bool unmapped_passthrough;
    bool show_tray_icon;
    bool start_minimized;
} config_settings_t;

typedef struct {
    config_settings_t settings;
    layer_action_t layer_map[256];
    global_shortcut_t shortcuts[MAX_SHORTCUTS];
    uint8_t shortcut_count;
} capslayer_config_t;

/* Initializes a configuration structure with default safe values */
void config_init_defaults(capslayer_config_t *cfg);

/* Loads and parses configuration from a JSON file */
bool config_load_from_file(const char *path, capslayer_config_t *cfg);

/* Loads and parses configuration from a JSON string */
bool config_load_from_json_string(const char *json_str, capslayer_config_t *cfg);

/* Resolves default config file path next to executable */
bool config_get_default_path(char *buffer, size_t buffer_size);

/* Resolves the directory path where config.json resides */
bool config_get_dir_path(const char *config_path, char *dir_buf, size_t dir_size);

#ifdef __cplusplus
}
#endif

#endif /* CAPSLAYER_CONFIG_H */
