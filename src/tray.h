#ifndef CAPSLAYER_TRAY_H
#define CAPSLAYER_TRAY_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM_TRAYICON            (WM_USER + 1)
#define WM_USER_RELOAD_CONFIG  (WM_USER + 2)
#define WM_USER_TOGGLE_PAUSE   (WM_USER + 3)
#define WM_USER_STATE_CHANGED  (WM_USER + 4)

#define IDM_STATUS             1001
#define IDM_TOGGLE_ENABLE      1002
#define IDM_RELOAD_CONFIG      1003
#define IDM_OPEN_CONFIG        1004
#define IDM_ABOUT              1005
#define IDM_EXIT               1006
#define IDM_TOGGLE_PERSISTENT  1007

/* Initializes the system tray icon */
bool tray_init(HWND hwnd, HINSTANCE hInstance);

/* Updates tray icon tooltip and status indication (normal, paused, or layer locked) */
void tray_update_status(bool paused, bool persistent);

/* Displays the tray popup context menu */
void tray_show_menu(HWND hwnd);

/* Removes tray icon and cleans up GDI resources */
void tray_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* CAPSLAYER_TRAY_H */
