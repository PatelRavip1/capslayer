#ifndef CAPSLAYER_KEYS_H
#define CAPSLAYER_KEYS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic extra info tag to mark self-injected keystrokes and prevent recursion */
#define MAGIC_INJECTED_FLAG ((uintptr_t)0xC4951A9E)

/* Resolves a case-insensitive key name string to a Windows Virtual-Key code (VK_*) */
WORD key_name_to_vk(const char *name);

/* Checks if a given Virtual-Key code requires KEYEVENTF_EXTENDEDKEY */
bool is_extended_key(WORD vk);

/* Checks if a given Virtual-Key code is a modifier key (Shift, Ctrl, Alt, Win) */
bool is_modifier_key(WORD vk);

/* Checks if a modifier key is currently pressed down in hardware */
bool is_modifier_down(WORD vk);

/* Synthesizes a single key press or release event tagged with MAGIC_INJECTED_FLAG */
void send_key_event(WORD vk, bool is_down);

/* Synthesizes a tap (down followed by up) of a single key */
void send_key_tap(WORD vk);

/* Synthesizes a key combination (e.g. Ctrl + C or Alt + F4) */
void send_key_combo(const WORD *vks, size_t count);

/* Asynchronously spawns an external process/command without blocking the hook */
void spawn_process_async(const char *command_utf8);

#ifdef __cplusplus
}
#endif

#endif /* CAPSLAYER_KEYS_H */
