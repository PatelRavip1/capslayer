#include "config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static bool is_toggle_persistent_name(const char *str)
{
    if (!str) return false;
    return (_stricmp(str, "toggle_persistent") == 0 ||
            _stricmp(str, "toggle_lock") == 0 ||
            _stricmp(str, "lock") == 0 ||
            _stricmp(str, "lock_layer") == 0 ||
            _stricmp(str, "persistent") == 0 ||
            _stricmp(str, "toggle_persistent_layer") == 0 ||
            _stricmp(str, "layer_lock") == 0 ||
            _stricmp(str, "toggle") == 0);
}

/* Helper: parse combo from JSON array of key strings or a "+" delimited string */
static bool parse_combo_array(cJSON *arr, layer_action_t *action)
{
    if (!cJSON_IsArray(arr)) return false;

    int size = cJSON_GetArraySize(arr);
    if (size <= 0 || size > MAX_COMBO_KEYS) return false;

    action->type = ACTION_COMBO;
    action->data.combo.count = 0;

    for (int i = 0; i < size; ++i) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsString(item) || !item->valuestring) {
            return false;
        }
        WORD vk = key_name_to_vk(item->valuestring);
        if (vk == 0) return false;

        action->data.combo.vks[action->data.combo.count++] = vk;
    }
    return action->data.combo.count > 0;
}

static bool parse_combo_string(const char *str, layer_action_t *action)
{
    if (!str || !*str) return false;

    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    WORD vks[MAX_COMBO_KEYS];
    uint8_t count = 0;

    char *next_token = NULL;
    char *token = strtok_s(buf, "+", &next_token);
    while (token != NULL) {
        /* Trim whitespace */
        while (*token && isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }

        if (*token) {
            if (count >= MAX_COMBO_KEYS) return false;
            WORD vk = key_name_to_vk(token);
            if (vk == 0) return false;
            vks[count++] = vk;
        }
        token = strtok_s(NULL, "+", &next_token);
    }

    if (count == 0) return false;

    if (count == 1) {
        action->type = ACTION_KEY;
        action->data.target_vk = vks[0];
    } else {
        action->type = ACTION_COMBO;
        action->data.combo.count = count;
        memcpy(action->data.combo.vks, vks, sizeof(WORD) * count);
    }
    return true;
}

/* Helper to parse an individual mapping value (string, array, or object) */
static bool parse_layer_action(cJSON *val, layer_action_t *action)
{
    if (!val || !action) return false;
    memset(action, 0, sizeof(layer_action_t));

    /* Case 1: Simple string e.g. "up", "ctrl+c", or "toggle_persistent" */
    if (cJSON_IsString(val) && val->valuestring) {
        if (is_toggle_persistent_name(val->valuestring)) {
            action->type = ACTION_TOGGLE_PERSISTENT;
            return true;
        }
        if (strchr(val->valuestring, '+') != NULL) {
            return parse_combo_string(val->valuestring, action);
        }
        WORD vk = key_name_to_vk(val->valuestring);
        if (vk != 0) {
            action->type = ACTION_KEY;
            action->data.target_vk = vk;
            return true;
        }
        /* If it looks like a path or command (has \ or / or .exe or .lnk) */
        if (strchr(val->valuestring, '\\') || strchr(val->valuestring, '/') ||
            strstr(val->valuestring, ".exe") || strstr(val->valuestring, ".lnk") || strstr(val->valuestring, ".bat")) {
            action->type = ACTION_EXEC;
            strncpy(action->data.command, val->valuestring, MAX_COMMAND_LEN - 1);
            action->data.command[MAX_COMMAND_LEN - 1] = '\0';
            return true;
        }
        return false;
    }

    /* Case 2: Array of keys e.g. ["ctrl", "c"] */
    if (cJSON_IsArray(val)) {
        return parse_combo_array(val, action);
    }

    /* Case 3: Object specification */
    if (cJSON_IsObject(val)) {
        cJSON *type_item = cJSON_GetObjectItemCaseSensitive(val, "action");
        if (!type_item || !cJSON_IsString(type_item) || !type_item->valuestring) {
            /* Try inferring from fields */
            cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(val, "command");
            if (!cmd_item) cmd_item = cJSON_GetObjectItemCaseSensitive(val, "cmd");
            if (cmd_item && cJSON_IsString(cmd_item) && cmd_item->valuestring) {
                action->type = ACTION_EXEC;
                strncpy(action->data.command, cmd_item->valuestring, MAX_COMMAND_LEN - 1);
                action->data.command[MAX_COMMAND_LEN - 1] = '\0';
                return true;
            }

            cJSON *target_item = cJSON_GetObjectItemCaseSensitive(val, "target");
            if (!target_item) target_item = cJSON_GetObjectItemCaseSensitive(val, "key");
            if (target_item && cJSON_IsString(target_item) && target_item->valuestring) {
                if (is_toggle_persistent_name(target_item->valuestring)) {
                    action->type = ACTION_TOGGLE_PERSISTENT;
                    return true;
                }
                WORD vk = key_name_to_vk(target_item->valuestring);
                if (vk != 0) {
                    action->type = ACTION_KEY;
                    action->data.target_vk = vk;
                    return true;
                }
            }

            cJSON *keys_item = cJSON_GetObjectItemCaseSensitive(val, "keys");
            if (keys_item && cJSON_IsArray(keys_item)) {
                return parse_combo_array(keys_item, action);
            }
            return false;
        }

        const char *type_str = type_item->valuestring;
        if (is_toggle_persistent_name(type_str)) {
            action->type = ACTION_TOGGLE_PERSISTENT;
            return true;
        }

        if (_stricmp(type_str, "key") == 0 || _stricmp(type_str, "remap") == 0) {
            cJSON *target = cJSON_GetObjectItemCaseSensitive(val, "target");
            if (!target) target = cJSON_GetObjectItemCaseSensitive(val, "key");
            if (target && cJSON_IsString(target) && target->valuestring) {
                if (is_toggle_persistent_name(target->valuestring)) {
                    action->type = ACTION_TOGGLE_PERSISTENT;
                    return true;
                }
                WORD vk = key_name_to_vk(target->valuestring);
                if (vk != 0) {
                    action->type = ACTION_KEY;
                    action->data.target_vk = vk;
                    return true;
                }
            }
        } else if (_stricmp(type_str, "combo") == 0) {
            cJSON *keys = cJSON_GetObjectItemCaseSensitive(val, "keys");
            if (keys && cJSON_IsArray(keys)) {
                return parse_combo_array(keys, action);
            }
            cJSON *str_combo = cJSON_GetObjectItemCaseSensitive(val, "target");
            if (str_combo && cJSON_IsString(str_combo) && str_combo->valuestring) {
                return parse_combo_string(str_combo->valuestring, action);
            }
        } else if (_stricmp(type_str, "exec") == 0 || _stricmp(type_str, "launch") == 0) {
            cJSON *cmd = cJSON_GetObjectItemCaseSensitive(val, "command");
            if (!cmd) cmd = cJSON_GetObjectItemCaseSensitive(val, "cmd");
            if (cmd && cJSON_IsString(cmd) && cmd->valuestring) {
                action->type = ACTION_EXEC;
                strncpy(action->data.command, cmd->valuestring, MAX_COMMAND_LEN - 1);
                action->data.command[MAX_COMMAND_LEN - 1] = '\0';
                return true;
            }
        } else {
            /* If type string directly names a key (e.g. "up") */
            WORD vk = key_name_to_vk(type_str);
            if (vk != 0) {
                action->type = ACTION_KEY;
                action->data.target_vk = vk;
                return true;
            }
        }
    }

    return false;
}

static bool parse_shortcut_combo_string(const char *combo_str, global_shortcut_t *shortcut)
{
    if (!combo_str || !*combo_str || !shortcut) return false;
    memset(shortcut, 0, sizeof(global_shortcut_t));

    char buf[256];
    strncpy(buf, combo_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    WORD tokens[MAX_SHORTCUT_MODIFIERS + 2];
    uint8_t count = 0;

    char *next_token = NULL;
    char *token = strtok_s(buf, "+", &next_token);
    while (token != NULL) {
        while (*token && isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }

        if (*token) {
            if (count >= (MAX_SHORTCUT_MODIFIERS + 2)) return false;
            WORD vk = key_name_to_vk(token);
            if (vk == 0) return false;
            tokens[count++] = vk;
        }
        token = strtok_s(NULL, "+", &next_token);
    }

    if (count == 0) return false;

    /* The last token or primary non-modifier token is trigger_vk */
    /* Find non-modifier tokens */
    int non_mod_idx = -1;
    for (int i = (int)count - 1; i >= 0; --i) {
        if (!is_modifier_key(tokens[i])) {
            non_mod_idx = i;
            break;
        }
    }

    if (non_mod_idx >= 0) {
        shortcut->trigger_vk = tokens[non_mod_idx];
        for (uint8_t i = 0; i < count; ++i) {
            if ((int)i != non_mod_idx) {
                if (shortcut->mod_count < MAX_SHORTCUT_MODIFIERS) {
                    shortcut->modifiers[shortcut->mod_count++] = tokens[i];
                }
            }
        }
    } else {
        /* All tokens are modifiers (e.g. Win + Alt + Shift) */
        shortcut->trigger_vk = tokens[count - 1];
        for (uint8_t i = 0; i < count - 1; ++i) {
            if (shortcut->mod_count < MAX_SHORTCUT_MODIFIERS) {
                shortcut->modifiers[shortcut->mod_count++] = tokens[i];
            }
        }
    }

    return (shortcut->trigger_vk != 0);
}

void config_init_defaults(capslayer_config_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(capslayer_config_t));

    /* Default settings */
    cfg->settings.swap_esc_and_capslock = false;
    cfg->settings.capslock_tap_as_esc = true;
    cfg->settings.esc_tap_as_capslock = true;
    cfg->settings.unmapped_passthrough = true;
    cfg->settings.show_tray_icon = true;
    cfg->settings.start_minimized = false;

    /* Default layer navigation: I J K L -> Up Left Down Right */
    WORD vk_i = key_name_to_vk("i");
    WORD vk_j = key_name_to_vk("j");
    WORD vk_k = key_name_to_vk("k");
    WORD vk_l = key_name_to_vk("l");

    if (vk_i < 256) {
        cfg->layer_map[vk_i].type = ACTION_KEY;
        cfg->layer_map[vk_i].data.target_vk = VK_UP;
    }
    if (vk_j < 256) {
        cfg->layer_map[vk_j].type = ACTION_KEY;
        cfg->layer_map[vk_j].data.target_vk = VK_LEFT;
    }
    if (vk_k < 256) {
        cfg->layer_map[vk_k].type = ACTION_KEY;
        cfg->layer_map[vk_k].data.target_vk = VK_DOWN;
    }
    if (vk_l < 256) {
        cfg->layer_map[vk_l].type = ACTION_KEY;
        cfg->layer_map[vk_l].data.target_vk = VK_RIGHT;
    }

    /* Extended navigation: H ; U D -> Home End PageUp PageDown */
    WORD vk_h = key_name_to_vk("h");
    WORD vk_semi = key_name_to_vk(";");
    WORD vk_u = key_name_to_vk("u");
    WORD vk_d = key_name_to_vk("d");

    if (vk_h < 256) {
        cfg->layer_map[vk_h].type = ACTION_KEY;
        cfg->layer_map[vk_h].data.target_vk = VK_HOME;
    }
    if (vk_semi < 256) {
        cfg->layer_map[vk_semi].type = ACTION_KEY;
        cfg->layer_map[vk_semi].data.target_vk = VK_END;
    }
    if (vk_u < 256) {
        cfg->layer_map[vk_u].type = ACTION_KEY;
        cfg->layer_map[vk_u].data.target_vk = VK_PRIOR;
    }
    if (vk_d < 256) {
        cfg->layer_map[vk_d].type = ACTION_KEY;
        cfg->layer_map[vk_d].data.target_vk = VK_NEXT;
    }

    /* Editing: Backspace / M / N -> Delete / Backspace */
    WORD vk_bs = key_name_to_vk("backspace");
    if (vk_bs < 256) {
        cfg->layer_map[vk_bs].type = ACTION_KEY;
        cfg->layer_map[vk_bs].data.target_vk = VK_DELETE;
    }

    WORD vk_m = key_name_to_vk("m");
    if (vk_m < 256) {
        cfg->layer_map[vk_m].type = ACTION_KEY;
        cfg->layer_map[vk_m].data.target_vk = VK_DELETE;
    }

    WORD vk_n = key_name_to_vk("n");
    if (vk_n < 256) {
        cfg->layer_map[vk_n].type = ACTION_KEY;
        cfg->layer_map[vk_n].data.target_vk = VK_BACK;
    }

    /* Persistent layer lock toggle: P -> Toggle Persistent Mode */
    WORD vk_p = key_name_to_vk("p");
    if (vk_p < 256) {
        cfg->layer_map[vk_p].type = ACTION_TOGGLE_PERSISTENT;
    }

    /* Close Window: W -> Alt + F4 */
    WORD vk_w = key_name_to_vk("w");
    if (vk_w < 256) {
        cfg->layer_map[vk_w].type = ACTION_COMBO;
        cfg->layer_map[vk_w].data.combo.count = 2;
        cfg->layer_map[vk_w].data.combo.vks[0] = VK_MENU;
        cfg->layer_map[vk_w].data.combo.vks[1] = VK_F4;
    }

    /* Launch Windows Terminal: Z -> "wt.exe" */
    WORD vk_z = key_name_to_vk("z");
    if (vk_z < 256) {
        cfg->layer_map[vk_z].type = ACTION_EXEC;
        strncpy(cfg->layer_map[vk_z].data.command, "wt.exe", MAX_COMMAND_LEN - 1);
    }

    /* Key Combinations: C -> Ctrl+C, V -> Ctrl+V */
    WORD vk_c = key_name_to_vk("c");
    if (vk_c < 256) {
        cfg->layer_map[vk_c].type = ACTION_COMBO;
        cfg->layer_map[vk_c].data.combo.count = 2;
        cfg->layer_map[vk_c].data.combo.vks[0] = VK_CONTROL;
        cfg->layer_map[vk_c].data.combo.vks[1] = 0x43; /* 'C' */
    }

    WORD vk_v = key_name_to_vk("v");
    if (vk_v < 256) {
        cfg->layer_map[vk_v].type = ACTION_COMBO;
        cfg->layer_map[vk_v].data.combo.count = 2;
        cfg->layer_map[vk_v].data.combo.vks[0] = VK_CONTROL;
        cfg->layer_map[vk_v].data.combo.vks[1] = 0x56; /* 'V' */
    }
}

bool config_load_from_json_string(const char *json_str, capslayer_config_t *cfg)
{
    if (!json_str || !cfg) return false;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return false;
    }

    /* Initialize defaults so unspecified fields have valid standard values */
    config_init_defaults(cfg);

    /* Parse settings */
    cJSON *settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
    if (settings && cJSON_IsObject(settings)) {
        cJSON *item;

        item = cJSON_GetObjectItemCaseSensitive(settings, "swap_esc_and_capslock");
        if (item && cJSON_IsBool(item)) cfg->settings.swap_esc_and_capslock = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(settings, "capslock_tap_as_esc");
        if (item && cJSON_IsBool(item)) cfg->settings.capslock_tap_as_esc = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(settings, "esc_tap_as_capslock");
        if (item && cJSON_IsBool(item)) cfg->settings.esc_tap_as_capslock = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(settings, "unmapped_passthrough");
        if (item && cJSON_IsBool(item)) cfg->settings.unmapped_passthrough = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(settings, "show_tray_icon");
        if (item && cJSON_IsBool(item)) cfg->settings.show_tray_icon = cJSON_IsTrue(item);

        item = cJSON_GetObjectItemCaseSensitive(settings, "start_minimized");
        if (item && cJSON_IsBool(item)) cfg->settings.start_minimized = cJSON_IsTrue(item);
    }

    /* Parse layer / bindings mapping */
    cJSON *layer = cJSON_GetObjectItemCaseSensitive(root, "layer");
    if (!layer) layer = cJSON_GetObjectItemCaseSensitive(root, "mappings");
    if (!layer) layer = cJSON_GetObjectItemCaseSensitive(root, "layer_mappings");
    if (!layer) layer = cJSON_GetObjectItemCaseSensitive(root, "bindings");

    if (layer && cJSON_IsObject(layer)) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, layer) {
            const char *src_key_name = entry->string;
            if (!src_key_name || !*src_key_name) continue;

            WORD vk = key_name_to_vk(src_key_name);
            if (vk > 0 && vk < 256) {
                layer_action_t action;
                if (parse_layer_action(entry, &action)) {
                    cfg->layer_map[vk] = action;
                }
            }
        }
    }

    /* Parse shortcuts / hotkeys / global / programs */
    cJSON *shortcuts = cJSON_GetObjectItemCaseSensitive(root, "shortcuts");
    if (!shortcuts) shortcuts = cJSON_GetObjectItemCaseSensitive(root, "hotkeys");
    if (!shortcuts) shortcuts = cJSON_GetObjectItemCaseSensitive(root, "global");
    if (!shortcuts) shortcuts = cJSON_GetObjectItemCaseSensitive(root, "programs");

    if (shortcuts) {
        cfg->shortcut_count = 0;

        if (cJSON_IsObject(shortcuts)) {
            cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, shortcuts) {
                if (cfg->shortcut_count >= MAX_SHORTCUTS) break;
                const char *combo_str = entry->string;
                if (!combo_str || !*combo_str) continue;

                global_shortcut_t sc;
                if (parse_shortcut_combo_string(combo_str, &sc)) {
                    if (parse_layer_action(entry, &sc.action)) {
                        cfg->shortcuts[cfg->shortcut_count++] = sc;
                    }
                }
            }
        } else if (cJSON_IsArray(shortcuts)) {
            int size = cJSON_GetArraySize(shortcuts);
            for (int i = 0; i < size; ++i) {
                if (cfg->shortcut_count >= MAX_SHORTCUTS) break;
                cJSON *item = cJSON_GetArrayItem(shortcuts, i);
                if (!cJSON_IsObject(item)) continue;

                global_shortcut_t sc;
                memset(&sc, 0, sizeof(global_shortcut_t));

                cJSON *keys_item = cJSON_GetObjectItemCaseSensitive(item, "keys");
                if (keys_item && cJSON_IsArray(keys_item)) {
                    int ksize = cJSON_GetArraySize(keys_item);
                    WORD tokens[MAX_SHORTCUT_MODIFIERS + 2];
                    uint8_t kcount = 0;

                    for (int k = 0; k < ksize && kcount < (MAX_SHORTCUT_MODIFIERS + 2); ++k) {
                        cJSON *kstr = cJSON_GetArrayItem(keys_item, k);
                        if (cJSON_IsString(kstr) && kstr->valuestring) {
                            WORD vk = key_name_to_vk(kstr->valuestring);
                            if (vk != 0) tokens[kcount++] = vk;
                        }
                    }

                    if (kcount > 0) {
                        int non_mod_idx = -1;
                        for (int k = (int)kcount - 1; k >= 0; --k) {
                            if (!is_modifier_key(tokens[k])) {
                                non_mod_idx = k;
                                break;
                            }
                        }

                        if (non_mod_idx >= 0) {
                            sc.trigger_vk = tokens[non_mod_idx];
                            for (uint8_t k = 0; k < kcount; ++k) {
                                if ((int)k != non_mod_idx && sc.mod_count < MAX_SHORTCUT_MODIFIERS) {
                                    sc.modifiers[sc.mod_count++] = tokens[k];
                                }
                            }
                        } else {
                            sc.trigger_vk = tokens[kcount - 1];
                            for (uint8_t k = 0; k < kcount - 1 && sc.mod_count < MAX_SHORTCUT_MODIFIERS; ++k) {
                                sc.modifiers[sc.mod_count++] = tokens[k];
                            }
                        }
                    }
                } else {
                    cJSON *combo_item = cJSON_GetObjectItemCaseSensitive(item, "combo");
                    if (!combo_item) combo_item = cJSON_GetObjectItemCaseSensitive(item, "shortcut");
                    if (combo_item && cJSON_IsString(combo_item) && combo_item->valuestring) {
                        parse_shortcut_combo_string(combo_item->valuestring, &sc);
                    }
                }

                if (sc.trigger_vk != 0) {
                    if (parse_layer_action(item, &sc.action)) {
                        cfg->shortcuts[cfg->shortcut_count++] = sc;
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

bool config_load_from_file(const char *path, capslayer_config_t *cfg)
{
    if (!path || !cfg) return false;

    FILE *f = NULL;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > (1024 * 1024)) { /* Limit config file size to 1 MB */
        fclose(f);
        return false;
    }

    char *buffer = (char *)malloc(len + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buffer, 1, len, f);
    fclose(f);
    buffer[read_bytes] = '\0';

    bool ok = config_load_from_json_string(buffer, cfg);
    free(buffer);
    return ok;
}

bool config_get_default_path(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return false;

    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        strncpy(buffer, "config.json", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return true;
    }

    char *last_slash = strrchr(exe_path, '\\');
    if (!last_slash) {
        last_slash = strrchr(exe_path, '/');
    }

    if (last_slash) {
        *(last_slash + 1) = '\0';
        snprintf(buffer, buffer_size, "%sconfig.json", exe_path);
    } else {
        strncpy(buffer, "config.json", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }

    return true;
}

bool config_get_dir_path(const char *config_path, char *dir_buf, size_t dir_size)
{
    if (!config_path || !dir_buf || dir_size == 0) return false;

    strncpy(dir_buf, config_path, dir_size - 1);
    dir_buf[dir_size - 1] = '\0';

    char *last_slash = strrchr(dir_buf, '\\');
    if (!last_slash) {
        last_slash = strrchr(dir_buf, '/');
    }

    if (last_slash) {
        *last_slash = '\0';
    } else {
        strncpy(dir_buf, ".", dir_size - 1);
        dir_buf[dir_size - 1] = '\0';
    }

    return true;
}
