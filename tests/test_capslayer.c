#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "capslayer.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(expr, msg) do { \
    g_tests_run++; \
    if (expr) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
        fflush(stdout); \
    } else { \
        printf("  [FAIL] %s (Line %d: %s)\n", msg, __LINE__, #expr); \
        fflush(stdout); \
    } \
} while (0)
static void test_key_name_to_vk(void)
{
    printf("\n=== Testing key_name_to_vk ===\n");

    /* Alphanumeric */
    TEST_ASSERT(key_name_to_vk("a") == 0x41, "Resolve 'a'");
    TEST_ASSERT(key_name_to_vk("A") == 0x41, "Resolve 'A'");
    TEST_ASSERT(key_name_to_vk("w") == 0x57, "Resolve 'w'");
    TEST_ASSERT(key_name_to_vk("z") == 0x5A, "Resolve 'z'");
    TEST_ASSERT(key_name_to_vk("0") == 0x30, "Resolve '0'");
    TEST_ASSERT(key_name_to_vk("9") == 0x39, "Resolve '9'");

    /* Navigation & Editing */
    TEST_ASSERT(key_name_to_vk("up") == VK_UP, "Resolve 'up'");
    TEST_ASSERT(key_name_to_vk("UP") == VK_UP, "Resolve 'UP' (case-insensitive)");
    TEST_ASSERT(key_name_to_vk("down") == VK_DOWN, "Resolve 'down'");
    TEST_ASSERT(key_name_to_vk("left") == VK_LEFT, "Resolve 'left'");
    TEST_ASSERT(key_name_to_vk("right") == VK_RIGHT, "Resolve 'right'");
    TEST_ASSERT(key_name_to_vk("home") == VK_HOME, "Resolve 'home'");
    TEST_ASSERT(key_name_to_vk("end") == VK_END, "Resolve 'end'");
    TEST_ASSERT(key_name_to_vk("pageup") == VK_PRIOR, "Resolve 'pageup'");
    TEST_ASSERT(key_name_to_vk("pgup") == VK_PRIOR, "Resolve 'pgup' alias");
    TEST_ASSERT(key_name_to_vk("pagedown") == VK_NEXT, "Resolve 'pagedown'");
    TEST_ASSERT(key_name_to_vk("pgdn") == VK_NEXT, "Resolve 'pgdn' alias");
    TEST_ASSERT(key_name_to_vk("delete") == VK_DELETE, "Resolve 'delete'");
    TEST_ASSERT(key_name_to_vk("del") == VK_DELETE, "Resolve 'del' alias");
    TEST_ASSERT(key_name_to_vk("backspace") == VK_BACK, "Resolve 'backspace'");
    TEST_ASSERT(key_name_to_vk("bksp") == VK_BACK, "Resolve 'bksp' alias");
    TEST_ASSERT(key_name_to_vk("tab") == VK_TAB, "Resolve 'tab'");
    TEST_ASSERT(key_name_to_vk("enter") == VK_RETURN, "Resolve 'enter'");
    TEST_ASSERT(key_name_to_vk("return") == VK_RETURN, "Resolve 'return'");
    TEST_ASSERT(key_name_to_vk("space") == VK_SPACE, "Resolve 'space'");
    TEST_ASSERT(key_name_to_vk("escape") == VK_ESCAPE, "Resolve 'escape'");
    TEST_ASSERT(key_name_to_vk("esc") == VK_ESCAPE, "Resolve 'esc' alias");
    TEST_ASSERT(key_name_to_vk("capslock") == VK_CAPITAL, "Resolve 'capslock'");
    TEST_ASSERT(key_name_to_vk("caps") == VK_CAPITAL, "Resolve 'caps' alias");

    /* Function Keys */
    TEST_ASSERT(key_name_to_vk("f1") == VK_F1, "Resolve 'f1'");
    TEST_ASSERT(key_name_to_vk("f4") == VK_F4, "Resolve 'f4'");
    TEST_ASSERT(key_name_to_vk("F5") == VK_F5, "Resolve 'F5'");
    TEST_ASSERT(key_name_to_vk("f12") == VK_F12, "Resolve 'f12'");
    TEST_ASSERT(key_name_to_vk("f24") == VK_F24, "Resolve 'f24'");

    /* Modifiers */
    TEST_ASSERT(key_name_to_vk("ctrl") == VK_CONTROL, "Resolve 'ctrl'");
    TEST_ASSERT(key_name_to_vk("alt") == VK_MENU, "Resolve 'alt'");
    TEST_ASSERT(key_name_to_vk("shift") == VK_SHIFT, "Resolve 'shift'");
    TEST_ASSERT(key_name_to_vk("win") == VK_LWIN, "Resolve 'win'");
    TEST_ASSERT(key_name_to_vk("ralt") == VK_RMENU, "Resolve 'ralt'");
    TEST_ASSERT(key_name_to_vk("right alt") == VK_RMENU, "Resolve 'right alt'");
    TEST_ASSERT(key_name_to_vk("right_alt") == VK_RMENU, "Resolve 'right_alt'");
    TEST_ASSERT(key_name_to_vk("rightalt") == VK_RMENU, "Resolve 'rightalt'");
    TEST_ASSERT(key_name_to_vk("lalt") == VK_LMENU, "Resolve 'lalt'");
    TEST_ASSERT(key_name_to_vk("left alt") == VK_LMENU, "Resolve 'left alt'");
    TEST_ASSERT(key_name_to_vk("rctrl") == VK_RCONTROL, "Resolve 'rctrl'");
    TEST_ASSERT(key_name_to_vk("lctrl") == VK_LCONTROL, "Resolve 'lctrl'");
    TEST_ASSERT(key_name_to_vk("rshift") == VK_RSHIFT, "Resolve 'rshift'");
    TEST_ASSERT(key_name_to_vk("lshift") == VK_LSHIFT, "Resolve 'lshift'");
    TEST_ASSERT(key_name_to_vk("rwin") == VK_RWIN, "Resolve 'rwin'");
    TEST_ASSERT(key_name_to_vk("lwin") == VK_LWIN, "Resolve 'lwin'");
    TEST_ASSERT(key_name_to_vk("caps_lock") == VK_CAPITAL, "Resolve 'caps_lock'");
    /* OEM & Punctuation */
    TEST_ASSERT(key_name_to_vk(";") == VK_OEM_1, "Resolve ';'");
    TEST_ASSERT(key_name_to_vk("semicolon") == VK_OEM_1, "Resolve 'semicolon'");
    TEST_ASSERT(key_name_to_vk(",") == VK_OEM_COMMA, "Resolve ','");
    TEST_ASSERT(key_name_to_vk(".") == VK_OEM_PERIOD, "Resolve '.'");
    TEST_ASSERT(key_name_to_vk("/") == VK_OEM_2, "Resolve '/'");
    TEST_ASSERT(key_name_to_vk("[") == VK_OEM_4, "Resolve '['");
    TEST_ASSERT(key_name_to_vk("]") == VK_OEM_6, "Resolve ']'");
    TEST_ASSERT(key_name_to_vk("\\") == VK_OEM_5, "Resolve '\\'");
    TEST_ASSERT(key_name_to_vk("-") == VK_OEM_MINUS, "Resolve '-'");
    TEST_ASSERT(key_name_to_vk("=") == VK_OEM_PLUS, "Resolve '='");

    /* Invalid string handling */
    TEST_ASSERT(key_name_to_vk("") == 0, "Empty string returns 0");
    TEST_ASSERT(key_name_to_vk(NULL) == 0, "NULL string returns 0");
    TEST_ASSERT(key_name_to_vk("unknown_invalid_key_name_xyz") == 0, "Unknown key returns 0");
    TEST_ASSERT(key_name_to_vk("modifier_key") == 0, "'modifier_key' string returns 0");
}

static void test_is_extended_key(void)
{
    printf("\n=== Testing is_extended_key & is_modifier_key ===\n");

    TEST_ASSERT(is_extended_key(VK_UP) == true, "VK_UP is extended");
    TEST_ASSERT(is_extended_key(VK_DOWN) == true, "VK_DOWN is extended");
    TEST_ASSERT(is_extended_key(VK_LEFT) == true, "VK_LEFT is extended");
    TEST_ASSERT(is_extended_key(VK_RIGHT) == true, "VK_RIGHT is extended");
    TEST_ASSERT(is_extended_key(VK_HOME) == true, "VK_HOME is extended");
    TEST_ASSERT(is_extended_key(VK_END) == true, "VK_END is extended");
    TEST_ASSERT(is_extended_key(VK_PRIOR) == true, "VK_PRIOR is extended");
    TEST_ASSERT(is_extended_key(VK_NEXT) == true, "VK_NEXT is extended");
    TEST_ASSERT(is_extended_key(VK_DELETE) == true, "VK_DELETE is extended");
    TEST_ASSERT(is_extended_key(VK_INSERT) == true, "VK_INSERT is extended");
    TEST_ASSERT(is_extended_key(VK_LWIN) == true, "VK_LWIN is extended");
    TEST_ASSERT(is_extended_key(VK_RWIN) == true, "VK_RWIN is extended");
    TEST_ASSERT(is_extended_key(VK_RCONTROL) == true, "VK_RCONTROL is extended");
    TEST_ASSERT(is_extended_key(VK_RMENU) == true, "VK_RMENU is extended");

    TEST_ASSERT(is_extended_key(0x41) == false, "'A' is not extended");
    TEST_ASSERT(is_extended_key(VK_SPACE) == false, "VK_SPACE is not extended");
    TEST_ASSERT(is_extended_key(VK_RETURN) == false, "VK_RETURN is not extended");

    /* Modifier key checks */
    TEST_ASSERT(is_modifier_key(VK_LWIN) == true, "VK_LWIN is modifier");
    TEST_ASSERT(is_modifier_key(VK_LMENU) == true, "VK_LMENU is modifier");
    TEST_ASSERT(is_modifier_key(VK_LSHIFT) == true, "VK_LSHIFT is modifier");
    TEST_ASSERT(is_modifier_key(VK_LCONTROL) == true, "VK_LCONTROL is modifier");
    TEST_ASSERT(is_modifier_key(0x41) == false, "'A' is not modifier");
    TEST_ASSERT(is_modifier_key(VK_CAPITAL) == false, "CapsLock is not modifier");
}

static void test_config_defaults(void)
{
    printf("\n=== Testing config_init_defaults ===\n");

    capslayer_config_t cfg;
    config_init_defaults(&cfg);

    TEST_ASSERT(cfg.settings.modifier_vk == VK_CAPITAL, "Default modifier_vk is VK_CAPITAL");
    TEST_ASSERT(cfg.settings.capslock_tap_as_esc == true, "Default capslock_tap_as_esc is true");
    TEST_ASSERT(cfg.settings.esc_tap_as_capslock == true, "Default esc_tap_as_capslock is true");
    TEST_ASSERT(cfg.settings.swap_esc_and_capslock == false, "Default swap_esc_and_capslock is false");
    TEST_ASSERT(cfg.settings.unmapped_passthrough == true, "Default unmapped_passthrough is true");
    TEST_ASSERT(cfg.settings.show_tray_icon == true, "Default show_tray_icon is true");
    /* Check navigation layer defaults */
    WORD vk_i = key_name_to_vk("i");
    WORD vk_j = key_name_to_vk("j");
    WORD vk_k = key_name_to_vk("k");
    WORD vk_l = key_name_to_vk("l");

    TEST_ASSERT(cfg.layer_map[vk_i].type == ACTION_KEY && cfg.layer_map[vk_i].data.target_vk == VK_UP, "Default 'i' -> UP");
    TEST_ASSERT(cfg.layer_map[vk_j].type == ACTION_KEY && cfg.layer_map[vk_j].data.target_vk == VK_LEFT, "Default 'j' -> LEFT");
    TEST_ASSERT(cfg.layer_map[vk_k].type == ACTION_KEY && cfg.layer_map[vk_k].data.target_vk == VK_DOWN, "Default 'k' -> DOWN");
    TEST_ASSERT(cfg.layer_map[vk_l].type == ACTION_KEY && cfg.layer_map[vk_l].data.target_vk == VK_RIGHT, "Default 'l' -> RIGHT");

    /* Check editing default */
    WORD vk_bs = key_name_to_vk("backspace");
    TEST_ASSERT(cfg.layer_map[vk_bs].type == ACTION_KEY && cfg.layer_map[vk_bs].data.target_vk == VK_DELETE, "Default 'backspace' -> DELETE");

    /* Check persistent lock default */
    WORD vk_p = key_name_to_vk("p");
    TEST_ASSERT(cfg.layer_map[vk_p].type == ACTION_TOGGLE_PERSISTENT, "Default 'p' -> ACTION_TOGGLE_PERSISTENT");

    /* Check w -> Alt+F4 default */
    WORD vk_w = key_name_to_vk("w");
    TEST_ASSERT(cfg.layer_map[vk_w].type == ACTION_COMBO && cfg.layer_map[vk_w].data.combo.count == 2, "Default 'w' -> combo");
    TEST_ASSERT(cfg.layer_map[vk_w].data.combo.vks[0] == VK_MENU && cfg.layer_map[vk_w].data.combo.vks[1] == VK_F4, "Default 'w' -> Alt + F4");

    /* Check exec default */
    WORD vk_z = key_name_to_vk("z");
    TEST_ASSERT(cfg.layer_map[vk_z].type == ACTION_EXEC && strcmp(cfg.layer_map[vk_z].data.command, "wt.exe") == 0, "Default 'z' -> exec 'wt.exe'");

    /* Check combos */
    WORD vk_c = key_name_to_vk("c");
    TEST_ASSERT(cfg.layer_map[vk_c].type == ACTION_COMBO && cfg.layer_map[vk_c].data.combo.count == 2, "Default 'c' -> combo (2 keys)");
    TEST_ASSERT(cfg.layer_map[vk_c].data.combo.vks[0] == VK_CONTROL && cfg.layer_map[vk_c].data.combo.vks[1] == 0x43, "Default 'c' -> Ctrl + C");
}

static void test_config_json_parsing(void)
{
    printf("\n=== Testing JSON config parsing ===\n");

    const char *test_json = 
        "{\n"
        "  \"settings\": {\n"
        "    \"modifier_key\": \"right alt\",\n"
        "    \"capslock_tap_as_esc\": false,\n"
        "    \"esc_tap_as_capslock\": true,\n"
        "    \"swap_esc_and_capslock\": true,\n"
        "    \"unmapped_passthrough\": false\n"
        "  },\n"
        "  \"layer\": {\n"
        "    \"i\": \"up\",\n"
        "    \"j\": \"left\",\n"
        "    \"k\": \"down\",\n"
        "    \"l\": \"right\",\n"
        "    \"w\": [\"alt\", \"f4\"],\n"
        "    \"p\": \"toggle_persistent\",\n"
        "    \"z\": {\n"
        "      \"action\": \"exec\",\n"
        "      \"command\": \"notepad.exe\"\n"
        "    }\n"
        "  },\n"
        "  \"shortcuts\": {\n"
        "    \"win+alt+capslock\": {\n"
        "      \"action\": \"exec\",\n"
        "      \"command\": \"C:\\\\WINDOWS\\\\system32\\\\shutdown.exe\"\n"
        "    },\n"
        "    \"win+shift+i\": {\n"
        "      \"action\": \"exec\",\n"
        "      \"command\": \"C:\\\\titus.lnk\"\n"
        "    }\n"
        "  }\n"
        "}";

    capslayer_config_t cfg;
    bool ok = config_load_from_json_string(test_json, &cfg);
    TEST_ASSERT(ok == true, "Parse valid JSON string");
    TEST_ASSERT(cfg.settings.modifier_vk == VK_RMENU, "Parsed modifier_key 'right alt' as VK_RMENU");
    TEST_ASSERT(cfg.settings.capslock_tap_as_esc == false, "Parsed capslock_tap_as_esc is false");
    TEST_ASSERT(cfg.settings.swap_esc_and_capslock == true, "Parsed swap_esc_and_capslock is true");
    TEST_ASSERT(cfg.settings.unmapped_passthrough == false, "Parsed unmapped_passthrough is false");
    WORD vk_i = key_name_to_vk("i");
    TEST_ASSERT(cfg.layer_map[vk_i].type == ACTION_KEY && cfg.layer_map[vk_i].data.target_vk == VK_UP, "Parsed 'i' -> UP");

    WORD vk_w = key_name_to_vk("w");
    TEST_ASSERT(cfg.layer_map[vk_w].type == ACTION_COMBO && cfg.layer_map[vk_w].data.combo.vks[0] == VK_MENU && cfg.layer_map[vk_w].data.combo.vks[1] == VK_F4, "Parsed 'w' -> Alt+F4");

    WORD vk_p = key_name_to_vk("p");
    TEST_ASSERT(cfg.layer_map[vk_p].type == ACTION_TOGGLE_PERSISTENT, "Parsed 'p' -> ACTION_TOGGLE_PERSISTENT");

    /* Check shortcuts parsing */
    TEST_ASSERT(cfg.shortcut_count == 2, "Parsed 2 global shortcuts");

    /* Check shortcut 1: win+alt+capslock */
    TEST_ASSERT(cfg.shortcuts[0].trigger_vk == VK_CAPITAL, "Shortcut 0 trigger is VK_CAPITAL");
    TEST_ASSERT(cfg.shortcuts[0].mod_count == 2, "Shortcut 0 has 2 modifiers (win, alt)");
    TEST_ASSERT(cfg.shortcuts[0].action.type == ACTION_EXEC, "Shortcut 0 is ACTION_EXEC");
    TEST_ASSERT(strstr(cfg.shortcuts[0].action.data.command, "shutdown.exe") != NULL, "Shortcut 0 command contains shutdown.exe");

    /* Check shortcut 2: win+shift+i */
    TEST_ASSERT(cfg.shortcuts[1].trigger_vk == 0x49, "Shortcut 1 trigger is 'I'");
    TEST_ASSERT(cfg.shortcuts[1].mod_count == 2, "Shortcut 1 has 2 modifiers (win, shift)");
    TEST_ASSERT(cfg.shortcuts[1].action.type == ACTION_EXEC, "Shortcut 1 is ACTION_EXEC");
    TEST_ASSERT(strstr(cfg.shortcuts[1].action.data.command, "titus.lnk") != NULL, "Shortcut 1 command contains titus.lnk");

    /* Invalid JSON handling */
    TEST_ASSERT(config_load_from_json_string("{ invalid json syntax ...", &cfg) == false, "Malformed JSON returns false");
    TEST_ASSERT(config_load_from_json_string(NULL, &cfg) == false, "NULL JSON string returns false");
    TEST_ASSERT(config_load_from_json_string("", &cfg) == false, "Empty JSON string returns false");
}

static void test_config_file_loading(void)
{
    printf("\n=== Testing config_load_from_file ===\n");

    capslayer_config_t cfg;
    bool ok = config_load_from_file("config.json", &cfg);
    TEST_ASSERT(ok == true, "Load default config.json from disk");

    TEST_ASSERT(cfg.settings.modifier_vk == VK_RMENU, "config.json modifier_vk is VK_RMENU");
    TEST_ASSERT(cfg.settings.capslock_tap_as_esc == true, "config.json capslock_tap_as_esc");
    TEST_ASSERT(cfg.settings.esc_tap_as_capslock == true, "config.json esc_tap_as_capslock");
    WORD vk_i = key_name_to_vk("i");
    TEST_ASSERT(cfg.layer_map[vk_i].type == ACTION_KEY && cfg.layer_map[vk_i].data.target_vk == VK_UP, "config.json 'i' -> UP");

    WORD vk_w = key_name_to_vk("w");
    TEST_ASSERT(cfg.layer_map[vk_w].type == ACTION_COMBO, "config.json 'w' -> combo");

    WORD vk_p = key_name_to_vk("p");
    TEST_ASSERT(cfg.layer_map[vk_p].type == ACTION_TOGGLE_PERSISTENT, "config.json 'p' -> ACTION_TOGGLE_PERSISTENT");

    /* Check shortcuts */
    TEST_ASSERT(cfg.shortcut_count >= 2, "config.json has 2 global shortcuts");

    /* Verify 'm' is not erroneously remapped to Alt */
    TEST_ASSERT(cfg.remap_map['M'] == 0, "config.json 'm' is not remapped to Alt");

    /* Nonexistent file */
    TEST_ASSERT(config_load_from_file("nonexistent_file_12345.json", &cfg) == false, "Nonexistent file returns false");
}

static void test_path_helpers(void)
{
    printf("\n=== Testing Path Helpers ===\n");

    char path_buf[MAX_PATH];
    bool ok = config_get_default_path(path_buf, sizeof(path_buf));
    TEST_ASSERT(ok == true, "config_get_default_path succeeded");
    TEST_ASSERT(strstr(path_buf, "config.json") != NULL, "Path ends with config.json");

    char dir_buf[MAX_PATH];
    ok = config_get_dir_path("C:\\Program Files\\CapsLayer\\config.json", dir_buf, sizeof(dir_buf));
    TEST_ASSERT(ok == true, "config_get_dir_path succeeded");
    TEST_ASSERT(strcmp(dir_buf, "C:\\Program Files\\CapsLayer") == 0, "Directory extracted properly");

    ok = config_get_dir_path("config.json", dir_buf, sizeof(dir_buf));
    TEST_ASSERT(ok == true && strcmp(dir_buf, ".") == 0, "Relative filename extracts '.' directory");
}

static int g_cb_calls = 0;
static void test_state_callback(bool is_paused, bool is_persistent)
{
    (void)is_paused;
    (void)is_persistent;
    g_cb_calls++;
}

static void test_hook_state_management(void)
{
    printf("\n=== Testing Hook State Management ===\n");

    capslayer_config_t cfg;
    config_init_defaults(&cfg);
    hook_update_config(&cfg);

    TEST_ASSERT(hook_is_paused() == false, "Initial hook state is not paused");
    hook_set_paused(true);
    TEST_ASSERT(hook_is_paused() == true, "Hook set to paused");
    hook_set_paused(false);
    TEST_ASSERT(hook_is_paused() == false, "Hook resumed");

    /* Persistent Layer Mode Tests */
    hook_set_state_callback(test_state_callback);
    int initial_calls = g_cb_calls;

    TEST_ASSERT(hook_is_persistent_layer() == false, "Initial persistent layer state is false");
    hook_set_persistent_layer(true);
    TEST_ASSERT(hook_is_persistent_layer() == true, "Persistent layer mode enabled");
    TEST_ASSERT(g_cb_calls > initial_calls, "State callback invoked on persistent change");

    /* Test key interception while persistent layer is active (without CapsLock physically held) */
    KBDLLHOOKSTRUCT key_event;
    ZeroMemory(&key_event, sizeof(key_event));
    key_event.vkCode = 0x49; /* 'I' */

    /* Should consume event (return 1) and remap 'I' -> UP */
    LRESULT res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&key_event);
    TEST_ASSERT(res == 1, "LowLevelKeyboardProc intercepted 'I' under persistent layer mode");

    /* Test toggle persistent layer */
    hook_toggle_persistent_layer();
    TEST_ASSERT(hook_is_persistent_layer() == false, "Persistent layer mode toggled off");

    /* Test LowLevelKeyboardProc injection filter */
    KBDLLHOOKSTRUCT injected_event;
    ZeroMemory(&injected_event, sizeof(injected_event));
    injected_event.vkCode = 0x41; /* 'A' */
    injected_event.flags = LLKHF_INJECTED;

    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&injected_event);
    /* Injected event should bypass processing */
    (void)res;
    TEST_ASSERT(true, "LowLevelKeyboardProc handled injected event without error");

    /* Test with MAGIC_INJECTED_FLAG */
    injected_event.flags = 0;
    injected_event.dwExtraInfo = (ULONG_PTR)MAGIC_INJECTED_FLAG;
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&injected_event);
    (void)res;
    TEST_ASSERT(true, "LowLevelKeyboardProc handled MAGIC_INJECTED_FLAG event without recursion");

    /* Test Custom Modifier Key (Right Alt / VK_RMENU) */
    cfg.settings.modifier_vk = VK_RMENU;
    hook_update_config(&cfg);

    KBDLLHOOKSTRUCT ralt_down;
    ZeroMemory(&ralt_down, sizeof(ralt_down));
    ralt_down.vkCode = VK_RMENU;
    ralt_down.flags = LLKHF_EXTENDED;

    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&ralt_down);
    TEST_ASSERT(res == 1, "LowLevelKeyboardProc consumed Right Alt KeyDown");

    /* While Right Alt is down, 'I' should be remapped to UP and consumed */
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&key_event);
    TEST_ASSERT(res == 1, "LowLevelKeyboardProc remapped 'I' while Right Alt is held");

    /* Releasing Right Alt */
    KBDLLHOOKSTRUCT ralt_up;
    ZeroMemory(&ralt_up, sizeof(ralt_up));
    ralt_up.vkCode = VK_RMENU;
    ralt_up.flags = LLKHF_EXTENDED;
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&ralt_up);
    TEST_ASSERT(res == 1, "LowLevelKeyboardProc consumed Right Alt KeyUp");

    /* Physical CapsLock when unmapped should pass through */
    cfg.settings.capslock_tap_as_esc = false;
    cfg.settings.swap_esc_and_capslock = false;
    cfg.remap_map[VK_CAPITAL] = 0;
    hook_update_config(&cfg);

    KBDLLHOOKSTRUCT caps_event;
    ZeroMemory(&caps_event, sizeof(caps_event));
    caps_event.vkCode = VK_CAPITAL;
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&caps_event);
    TEST_ASSERT(res == 0, "LowLevelKeyboardProc passed through CapsLock when unmapped");
}

static void test_capslock_esc_remapping(void)
{
    printf("\n=== Testing CapsLock <-> Esc Remapping ===\n");

    /* 1. Test "remap": { "capslock": "esc", "esc": "capslock" } */
    const char *remap_json = 
        "{\n"
        "  \"settings\": { \"modifier_key\": \"right alt\" },\n"
        "  \"remap\": {\n"
        "    \"capslock\": \"esc\",\n"
        "    \"esc\": \"capslock\"\n"
        "  }\n"
        "}";
    capslayer_config_t cfg1;
    bool ok = config_load_from_json_string(remap_json, &cfg1);
    TEST_ASSERT(ok == true, "Parse remap JSON block");
    TEST_ASSERT(cfg1.remap_map[VK_CAPITAL] == VK_ESCAPE, "remap block maps capslock -> esc");
    TEST_ASSERT(cfg1.remap_map[VK_ESCAPE] == VK_CAPITAL, "remap block maps esc -> capslock");

    /* 2. Test "settings": { "capslock": "esc", "esc": "capslock" } */
    const char *settings_remap_json = 
        "{\n"
        "  \"settings\": {\n"
        "    \"modifier_key\": \"right alt\",\n"
        "    \"capslock\": \"esc\",\n"
        "    \"esc\": \"capslock\"\n"
        "  }\n"
        "}";
    capslayer_config_t cfg2;
    ok = config_load_from_json_string(settings_remap_json, &cfg2);
    TEST_ASSERT(ok == true, "Parse settings key remap pairs");
    TEST_ASSERT(cfg2.remap_map[VK_CAPITAL] == VK_ESCAPE, "settings pair maps capslock -> esc");
    TEST_ASSERT(cfg2.remap_map[VK_ESCAPE] == VK_CAPITAL, "settings pair maps esc -> capslock");

    /* 3. Test swap_esc_and_capslock: true */
    const char *swap_json = 
        "{\n"
        "  \"settings\": {\n"
        "    \"modifier_key\": \"right alt\",\n"
        "    \"swap_esc_and_capslock\": true\n"
        "  }\n"
        "}";
    capslayer_config_t cfg3;
    ok = config_load_from_json_string(swap_json, &cfg3);
    TEST_ASSERT(ok == true, "Parse swap_esc_and_capslock JSON");
    TEST_ASSERT(cfg3.remap_map[VK_CAPITAL] == VK_ESCAPE, "swap_esc_and_capslock maps capslock -> esc");
    TEST_ASSERT(cfg3.remap_map[VK_ESCAPE] == VK_CAPITAL, "swap_esc_and_capslock maps esc -> capslock");

    /* 4. Test capslock_tap_as_esc & esc_tap_as_capslock when modifier is right alt */
    const char *tap_cfg_json = 
        "{\n"
        "  \"settings\": {\n"
        "    \"modifier_key\": \"right alt\",\n"
        "    \"capslock_tap_as_esc\": true,\n"
        "    \"esc_tap_as_capslock\": true\n"
        "  }\n"
        "}";
    capslayer_config_t cfg4;
    ok = config_load_from_json_string(tap_cfg_json, &cfg4);
    TEST_ASSERT(ok == true, "Parse tap flags JSON");
    TEST_ASSERT(cfg4.remap_map[VK_CAPITAL] == VK_ESCAPE, "capslock_tap_as_esc maps capslock -> esc");
    TEST_ASSERT(cfg4.remap_map[VK_ESCAPE] == VK_CAPITAL, "esc_tap_as_capslock maps esc -> capslock");

    /* 5. Hook execution test with CapsLock <-> Esc remapping */
    hook_update_config(&cfg1);

    KBDLLHOOKSTRUCT caps_down = { .vkCode = VK_CAPITAL };
    LRESULT res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&caps_down);
    TEST_ASSERT(res == 1, "CapsLock KeyDown is intercepted and remapped");

    KBDLLHOOKSTRUCT caps_up = { .vkCode = VK_CAPITAL };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&caps_up);
    TEST_ASSERT(res == 1, "CapsLock KeyUp is intercepted and released");

    KBDLLHOOKSTRUCT esc_down = { .vkCode = VK_ESCAPE };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&esc_down);
    TEST_ASSERT(res == 1, "Esc KeyDown is intercepted and remapped");

    KBDLLHOOKSTRUCT esc_up = { .vkCode = VK_ESCAPE };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&esc_up);
    TEST_ASSERT(res == 1, "Esc KeyUp is intercepted and released");
    /* 6. Test layer-nested "capslock": "esc" and "esc": "capslock" */
    const char *layer_nested_json =
        "{\n"
        "  \"settings\": { \"modifier_key\": \"right alt\" },\n"
        "  \"layer\": {\n"
        "    \"capslock\": \"esc\",\n"
        "    \"esc\": \"capslock\",\n"
        "    \"i\": \"up\"\n"
        "  }\n"
        "}";
    capslayer_config_t cfg5;
    ok = config_load_from_json_string(layer_nested_json, &cfg5);
    TEST_ASSERT(ok == true, "Parse layer-nested capslock/esc JSON");
    TEST_ASSERT(cfg5.remap_map[VK_CAPITAL] == VK_ESCAPE, "layer-nested capslock -> esc populates remap_map");
    TEST_ASSERT(cfg5.remap_map[VK_ESCAPE] == VK_CAPITAL, "layer-nested esc -> capslock populates remap_map");

    /* 7. Test top-level "capslock": "esc" and "esc": "capslock" */
    const char *toplevel_json =
        "{\n"
        "  \"modifier_key\": \"right alt\",\n"
        "  \"capslock\": \"esc\",\n"
        "  \"esc\": \"capslock\"\n"
        "}";
    capslayer_config_t cfg6;
    ok = config_load_from_json_string(toplevel_json, &cfg6);
    TEST_ASSERT(ok == true, "Parse top-level capslock/esc JSON");
    TEST_ASSERT(cfg6.remap_map[VK_CAPITAL] == VK_ESCAPE, "top-level capslock -> esc populates remap_map");
    TEST_ASSERT(cfg6.remap_map[VK_ESCAPE] == VK_CAPITAL, "top-level esc -> capslock populates remap_map");
}

static void test_modifier_tap_vs_hold(void)
{
    printf("\n=== Testing Modifier Tap vs Hold Behavior ===\n");

    capslayer_config_t cfg;
    config_init_defaults(&cfg);
    cfg.settings.modifier_vk = VK_RMENU;
    cfg.settings.capslock_tap_as_esc = true;
    cfg.settings.unmapped_passthrough = true;
    hook_update_config(&cfg);

    /* Test 1: Right Alt tapped in isolation (Key Down then Key Up) */
    KBDLLHOOKSTRUCT ralt_down = { .vkCode = VK_RMENU, .flags = LLKHF_EXTENDED };
    LRESULT res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&ralt_down);
    TEST_ASSERT(res == 1, "Isolated Right Alt KeyDown consumed for tap detection");

    KBDLLHOOKSTRUCT ralt_up = { .vkCode = VK_RMENU, .flags = LLKHF_EXTENDED };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&ralt_up);
    TEST_ASSERT(res == 1, "Isolated Right Alt KeyUp triggers modifier tap and is consumed");

    /* Test 2: Right Alt held + Layer key ('I' -> UP) */
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&ralt_down);
    TEST_ASSERT(res == 1, "Right Alt KeyDown consumed");

    KBDLLHOOKSTRUCT key_i_down = { .vkCode = 0x49 }; /* 'I' */
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&key_i_down);
    TEST_ASSERT(res == 1, "'I' KeyDown while holding Right Alt is remapped to UP");

    KBDLLHOOKSTRUCT key_i_up = { .vkCode = 0x49 };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&key_i_up);
    TEST_ASSERT(res == 1, "'I' KeyUp while holding Right Alt is handled");

    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&ralt_up);
    TEST_ASSERT(res == 1, "Right Alt KeyUp after layer key does not fire isolated tap");

    /* Test 3: Right Alt held + Unmapped key (Tab) */
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&ralt_down);
    TEST_ASSERT(res == 1, "Right Alt KeyDown consumed");

    KBDLLHOOKSTRUCT tab_down = { .vkCode = VK_TAB };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYDOWN, (LPARAM)&tab_down);
    TEST_ASSERT(res == 0, "Unmapped Tab KeyDown passes through to OS with modifier");

    KBDLLHOOKSTRUCT tab_up = { .vkCode = VK_TAB };
    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&tab_up);
    TEST_ASSERT(res == 0, "Unmapped Tab KeyUp passes through to OS");

    res = LowLevelKeyboardProc(HC_ACTION, WM_KEYUP, (LPARAM)&ralt_up);
    TEST_ASSERT(res == 1, "Right Alt KeyUp after unmapped key releases passthrough modifier");
}

int main(void)
{
    printf("===================================================\n");
    printf("     CapsLayer Test Suite & Verification\n");
    printf("===================================================\n");

    test_key_name_to_vk();
    test_is_extended_key();
    test_config_defaults();
    test_config_json_parsing();
    test_config_file_loading();
    test_path_helpers();
    test_hook_state_management();
    test_capslock_esc_remapping();
    test_modifier_tap_vs_hold();
    printf("\n===================================================\n");
    printf("Tests Run: %d | Passed: %d | Failed: %d\n", g_tests_run, g_tests_passed, g_tests_run - g_tests_passed);
    printf("===================================================\n");

    if (g_tests_passed == g_tests_run && g_tests_run > 0) {
        printf("ALL TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("SOME TESTS FAILED!\n");
        return 1;
    }
}
