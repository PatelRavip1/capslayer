#include "tray.h"
#include "hook.h"
#include <stdio.h>
#include <stdlib.h>

static NOTIFYICONDATAW g_nid;
static bool g_tray_active = false;
static HICON g_icon_active = NULL;
static HICON g_icon_locked = NULL;
static HICON g_icon_paused = NULL;

static HICON create_status_icon(COLORREF bg_color, COLORREF fg_color, const wchar_t *letter)
{
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;

    HDC hdc_screen = GetDC(NULL);
    HDC hdc_color = CreateCompatibleDC(hdc_screen);
    HDC hdc_mask = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; /* Top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *color_bits = NULL;
    HBITMAP hbm_color = CreateDIBSection(hdc_screen, &bmi, DIB_RGB_COLORS, &color_bits, NULL, 0);
    HBITMAP hbm_mask = CreateBitmap(size, size, 1, 1, NULL);

    HBITMAP old_color = (HBITMAP)SelectObject(hdc_color, hbm_color);
    HBITMAP old_mask = (HBITMAP)SelectObject(hdc_mask, hbm_mask);

    /* Fill background */
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    RECT rc = { 0, 0, size, size };
    FillRect(hdc_color, &rc, bg_brush);
    DeleteObject(bg_brush);

    /* Draw mask (all black = opaque) */
    HBRUSH mask_brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(hdc_mask, &rc, mask_brush);

    /* Draw letter in center */
    SetBkMode(hdc_color, TRANSPARENT);
    SetTextColor(hdc_color, fg_color);

    HFONT hFont = CreateFontW(
        size - 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    HFONT old_font = (HFONT)SelectObject(hdc_color, hFont);

    DrawTextW(hdc_color, letter ? letter : L"C", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc_color, old_font);
    DeleteObject(hFont);

    SelectObject(hdc_color, old_color);
    SelectObject(hdc_mask, old_mask);
    DeleteDC(hdc_color);
    DeleteDC(hdc_mask);
    ReleaseDC(NULL, hdc_screen);

    ICONINFO ii;
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = hbm_color;
    ii.hbmMask = hbm_mask;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbm_color);
    DeleteObject(hbm_mask);

    return hIcon;
}

bool tray_init(HWND hwnd, HINSTANCE hInstance)
{
    (void)hInstance;
    if (g_tray_active) return true;

    /* Green for active, Amber/Blue for locked layer, Gray for paused */
    g_icon_active = create_status_icon(RGB(0, 200, 83), RGB(255, 255, 255), L"C");
    g_icon_locked = create_status_icon(RGB(0, 176, 255), RGB(255, 255, 255), L"L");
    g_icon_paused = create_status_icon(RGB(117, 117, 117), RGB(255, 255, 255), L"C");

    ZeroMemory(&g_nid, sizeof(NOTIFYICONDATAW));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_icon_active ? g_icon_active : LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, sizeof(g_nid.szTip) / sizeof(wchar_t), L"CapsLayer - Active");

    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        g_tray_active = true;
        return true;
    }

    return false;
}

void tray_update_status(bool paused, bool persistent)
{
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

void tray_show_menu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool paused = hook_is_paused();
    bool persistent = hook_is_persistent_layer();

    /* Status header */
    if (paused) {
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, L"CapsLayer (Paused)");
    } else if (persistent) {
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, L"CapsLayer (Layer Locked - Caps+P)");
    } else {
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED, IDM_STATUS, L"CapsLayer (Active)");
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Toggle Persistent Layer Mode */
    UINT lock_flags = MF_STRING | (persistent ? MF_CHECKED : MF_UNCHECKED);
    if (paused) lock_flags |= (MF_DISABLED | MF_GRAYED);
    AppendMenuW(hMenu, lock_flags, IDM_TOGGLE_PERSISTENT, L"&Lock Layer (Caps+P)");

    /* Toggle Enable */
    UINT enable_flags = MF_STRING | (paused ? MF_UNCHECKED : MF_CHECKED);
    AppendMenuW(hMenu, enable_flags, IDM_TOGGLE_ENABLE, L"&Enabled");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Config actions */
    AppendMenuW(hMenu, MF_STRING, IDM_RELOAD_CONFIG, L"&Reload Configuration");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_CONFIG, L"&Open config.json");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"&About CapsLayer");
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"E&xit");

    /* Required for proper menu dismiss behavior */
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

void tray_cleanup(void)
{
    if (g_tray_active) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_tray_active = false;
    }
    if (g_icon_active) {
        DestroyIcon(g_icon_active);
        g_icon_active = NULL;
    }
    if (g_icon_locked) {
        DestroyIcon(g_icon_locked);
        g_icon_locked = NULL;
    }
    if (g_icon_paused) {
        DestroyIcon(g_icon_paused);
        g_icon_paused = NULL;
    }
}
