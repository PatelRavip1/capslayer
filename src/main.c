#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "keys.h"
#include "config.h"
#include "hook.h"
#include "tray.h"

#define CAPSLAYER_VERSION "1.0.0"
#define WINDOW_CLASS_NAME L"CapsLayer_MessageWindow_Class"
#define WINDOW_TITLE      L"CapsLayer Daemon"
#define MUTEX_NAME        L"CapsLayer_SingleInstance_Mutex"

typedef struct {
    char config_path[MAX_PATH];
    char config_dir[MAX_PATH];
    bool console_mode;
    bool test_config_only;
    bool paused_initial;
    bool no_elevate;
    bool do_install;
    bool do_uninstall;
    bool do_enable_startup;
    bool do_disable_startup;
    bool do_status;
    HWND hwnd;
    HANDLE stop_event;
    HANDLE watcher_thread;
    HANDLE single_instance_mutex;
} AppContext;

static AppContext g_app;

static void print_usage(const char *prog_name)
{
    printf("CapsLayer v%s - Fast Windows Keyboard Remapper & Layer Daemon\n\n", CAPSLAYER_VERSION);
    printf("Usage: %s [options]\n\n", prog_name ? prog_name : "capslayer.exe");
    printf("Options:\n");
    printf("  -c, --config <path>     Specify path to config.json\n");
    printf("  -t, --test-config       Validate config file syntax and bindings, then exit\n");
    printf("      --install           Install to 'C:\\Program Files (x86)\\capslayer' and enable startup\n");
    printf("      --uninstall         Uninstall from 'C:\\Program Files (x86)\\capslayer' and remove startup\n");
    printf("      --enable-startup    Register Task Scheduler startup task (elevated at logon)\n");
    printf("      --disable-startup   Remove Task Scheduler startup task\n");
    printf("      --status            Show installation and startup configuration status\n");
    printf("      --console           Attach/allocate console for live diagnostic output\n");
    printf("      --paused            Start daemon in paused (disabled) state\n");
    printf("      --no-elevate        Skip auto-elevation to administrator\n");
    printf("  -v, --version           Print version information and exit\n");
    printf("  -h, --help              Display this help message and exit\n\n");
}

static void log_msg(const char *fmt, ...)
{
    if (!g_app.console_mode) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

static bool is_running_as_admin(void)
{
    BOOL is_admin = FALSE;
    HANDLE token = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
            is_admin = (elevation.TokenIsElevated != 0);
        }
        CloseHandle(token);
    }
    return (is_admin != 0);
}

static bool relaunch_as_admin(int argc, char *argv[])
{
    wchar_t exe_path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH)) {
        return false;
    }

    wchar_t args_buf[2048] = { 0 };
    for (int i = 1; i < argc; ++i) {
        wchar_t wide_arg[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wide_arg, MAX_PATH);
        if (i > 1) {
            wcscat_s(args_buf, sizeof(args_buf) / sizeof(wchar_t), L" ");
        }
        wcscat_s(args_buf, sizeof(args_buf) / sizeof(wchar_t), wide_arg);
    }

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.lpParameters = args_buf[0] ? args_buf : NULL;
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOASYNC;

    return (ShellExecuteExW(&sei) != 0);
}

static void on_layer_state_changed(bool is_paused, bool is_persistent)
{
    (void)is_paused;
    (void)is_persistent;
    if (g_app.hwnd) {
        PostMessageW(g_app.hwnd, WM_USER_STATE_CHANGED, 0, 0);
    }
}

static bool reload_configuration(void)
{
    capslayer_config_t cfg;
    log_msg("[Config] Loading configuration from '%s'...", g_app.config_path);

    if (!config_load_from_file(g_app.config_path, &cfg)) {
        log_msg("[Config] Warning: Failed to load '%s'. Falling back to default settings.", g_app.config_path);
        config_init_defaults(&cfg);
    } else {
        log_msg("[Config] Configuration loaded and applied successfully.");
    }

    hook_update_config(&cfg);
    return true;
}

static unsigned __stdcall config_watcher_thread_proc(void *arg)
{
    AppContext *app = (AppContext *)arg;
    if (!app || !app->config_dir[0]) return 0;

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, app->config_dir, -1, NULL, 0);
    if (wide_len <= 0) return 0;

    wchar_t wide_dir[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, app->config_dir, -1, wide_dir, MAX_PATH);

    HANDLE hDir = CreateFileW(
        wide_dir,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        log_msg("[Watcher] Failed to open directory '%s' for watching (Error: %lu)", app->config_dir, GetLastError());
        return 0;
    }

    log_msg("[Watcher] Watching directory '%s' for live config changes...", app->config_dir);

    BYTE buffer[1024];
    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    HANDLE wait_handles[2] = { app->stop_event, ov.hEvent };
    DWORD last_reload_tick = 0;

    while (true) {
        ResetEvent(ov.hEvent);
        DWORD bytes_returned = 0;

        BOOL ok = ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE,
            &bytes_returned,
            &ov,
            NULL
        );

        if (!ok) {
            break;
        }

        DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0) {
            /* Stop event signaled */
            CancelIo(hDir);
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            /* Directory change detected */
            DWORD current_tick = GetTickCount();
            /* 250ms debounce */
            if (current_tick - last_reload_tick > 250) {
                last_reload_tick = current_tick;
                Sleep(50); /* Brief yield for atomic file write to finish */
                if (app->hwnd) {
                    PostMessageW(app->hwnd, WM_USER_RELOAD_CONFIG, 0, 0);
                }
            }
        } else {
            break;
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(hDir);
    return 0;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                tray_show_menu(hwnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                bool next_state = !hook_is_paused();
                hook_set_paused(next_state);
                tray_update_status(next_state, hook_is_persistent_layer());
                log_msg("[Daemon] State toggled via double click: %s", next_state ? "Paused" : "Active");
            }
            return 0;
        }

        case WM_COMMAND: {
            WORD cmd_id = LOWORD(wParam);
            switch (cmd_id) {
                case IDM_TOGGLE_PERSISTENT: {
                    bool is_locked = hook_toggle_persistent_layer();
                    tray_update_status(hook_is_paused(), is_locked);
                    log_msg("[Daemon] Layer lock toggled via menu: %s", is_locked ? "Locked" : "Unlocked");
                    break;
                }
                case IDM_TOGGLE_ENABLE: {
                    bool next_state = !hook_is_paused();
                    hook_set_paused(next_state);
                    tray_update_status(next_state, hook_is_persistent_layer());
                    log_msg("[Daemon] State toggled via menu: %s", next_state ? "Paused" : "Active");
                    break;
                }
                case IDM_RELOAD_CONFIG: {
                    reload_configuration();
                    break;
                }
                case IDM_OPEN_CONFIG: {
                    int wide_len = MultiByteToWideChar(CP_UTF8, 0, g_app.config_path, -1, NULL, 0);
                    if (wide_len > 0) {
                        wchar_t wide_path[MAX_PATH];
                        MultiByteToWideChar(CP_UTF8, 0, g_app.config_path, -1, wide_path, MAX_PATH);
                        ShellExecuteW(hwnd, L"open", wide_path, NULL, NULL, SW_SHOWNORMAL);
                    }
                    break;
                }
                case IDM_ABOUT: {
                    MessageBoxW(
                        hwnd,
                        L"CapsLayer v" TEXT(CAPSLAYER_VERSION) L"\n\n"
                        L"Fast Windows Keyboard Remapper & Layer Daemon in C\n\n"
                        L"Dual-role CapsLock / Escape, navigation layers, CapsLock+P layer lock (with LED blinker), and asynchronous application launching.",
                        L"About CapsLayer",
                        MB_OK | MB_ICONINFORMATION
                    );
                    break;
                }
                case IDM_EXIT: {
                    DestroyWindow(hwnd);
                    break;
                }
            }
            return 0;
        }

        case WM_USER_STATE_CHANGED: {
            tray_update_status(hook_is_paused(), hook_is_persistent_layer());
            return 0;
        }

        case WM_USER_RELOAD_CONFIG: {
            log_msg("[Watcher] File change detected. Hot-reloading configuration...");
            reload_configuration();
            return 0;
        }

        case WM_USER_TOGGLE_PAUSE: {
            bool next_state = !hook_is_paused();
            hook_set_paused(next_state);
            tray_update_status(next_state, hook_is_persistent_layer());
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static int run_app(HINSTANCE hInstance)
{
    /* Enforce Single Instance */
    g_app.single_instance_mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_app.console_mode) {
            printf("CapsLayer is already running.\n");
        }
        if (g_app.single_instance_mutex) {
            CloseHandle(g_app.single_instance_mutex);
            g_app.single_instance_mutex = NULL;
        }
        return 0;
    }

    /* Register Window Class for message pump */
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        log_msg("[Daemon] Error: Failed to register window class.");
        return 1;
    }

    /* Create Hidden Message Window */
    g_app.hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (!g_app.hwnd) {
        log_msg("[Daemon] Error: Failed to create message window.");
        UnregisterClassW(WINDOW_CLASS_NAME, hInstance);
        return 1;
    }

    /* Load initial configuration */
    reload_configuration();

    /* Initialize System Tray */
    if (!tray_init(g_app.hwnd, hInstance)) {
        log_msg("[Daemon] Warning: Failed to initialize system tray icon.");
    }

    if (g_app.paused_initial) {
        hook_set_paused(true);
        tray_update_status(true, false);
        log_msg("[Daemon] Started in paused state.");
    }

    /* Register hook state callback for tray icon updates */
    hook_set_state_callback(on_layer_state_changed);

    /* Install Keyboard Hook */
    if (!hook_install(hInstance)) {
        log_msg("[Daemon] Fatal Error: Failed to install low-level keyboard hook.");
        MessageBoxW(NULL, L"Failed to install keyboard hook. Please check system permissions.", L"CapsLayer Error", MB_OK | MB_ICONERROR);
        tray_cleanup();
        DestroyWindow(g_app.hwnd);
        UnregisterClassW(WINDOW_CLASS_NAME, hInstance);
        return 1;
    }

    log_msg("[Daemon] Keyboard hook installed. Daemon running.");

    /* Start Config File Watcher Thread */
    g_app.stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    uintptr_t thread_handle = _beginthreadex(NULL, 0, config_watcher_thread_proc, &g_app, 0, NULL);
    if (thread_handle != 0) {
        g_app.watcher_thread = (HANDLE)thread_handle;
    }

    /* Message Loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    log_msg("[Daemon] Shutting down daemon...");

    /* Stop watcher thread */
    if (g_app.stop_event) {
        SetEvent(g_app.stop_event);
        if (g_app.watcher_thread) {
            WaitForSingleObject(g_app.watcher_thread, 2000);
            CloseHandle(g_app.watcher_thread);
            g_app.watcher_thread = NULL;
        }
        CloseHandle(g_app.stop_event);
        g_app.stop_event = NULL;
    }

    /* Uninstall hook & clean tray */
    hook_uninstall();
    tray_cleanup();
    UnregisterClassW(WINDOW_CLASS_NAME, hInstance);

    if (g_app.single_instance_mutex) {
        ReleaseMutex(g_app.single_instance_mutex);
        CloseHandle(g_app.single_instance_mutex);
        g_app.single_instance_mutex = NULL;
    }

    log_msg("[Daemon] Clean shutdown complete.");
    return (int)msg.wParam;
}
static bool run_command_hidden(const wchar_t *cmd)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    wchar_t cmd_buf[1024];
    wcsncpy_s(cmd_buf, sizeof(cmd_buf) / sizeof(wchar_t), cmd, _TRUNCATE);

    if (!CreateProcessW(NULL, cmd_buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 10000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (exit_code == 0);
}

static void get_install_base_path(wchar_t *buf, size_t buf_len)
{
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", buf, (DWORD)buf_len) != 0 && buf[0] != L'\0') {
        return;
    }
    if (GetEnvironmentVariableW(L"ProgramFiles", buf, (DWORD)buf_len) != 0 && buf[0] != L'\0') {
        return;
    }
    wcscpy_s(buf, buf_len, L"C:\\Program Files (x86)");
}

static bool setup_enable_startup(void)
{
    wchar_t prog_files[MAX_PATH];
    get_install_base_path(prog_files, MAX_PATH);

    wchar_t target_exe[MAX_PATH];
    swprintf_s(target_exe, MAX_PATH, L"%s\\capslayer\\capslayer.exe", prog_files);

    wchar_t target_dir[MAX_PATH];
    swprintf_s(target_dir, MAX_PATH, L"%s\\capslayer", prog_files);

    /* Clean up any legacy Registry Run keys or Startup folder shortcuts to avoid duplicate unelevated launches */
    run_command_hidden(L"reg delete \"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_command_hidden(L"reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_command_hidden(L"powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        L"$p1 = [System.IO.Path]::Combine($env:APPDATA, 'Microsoft\\Windows\\Start Menu\\Programs\\Startup\\CapsLayer.lnk'); "
        L"$p2 = [System.IO.Path]::Combine($env:ProgramData, 'Microsoft\\Windows\\Start Menu\\Programs\\Startup\\CapsLayer.lnk'); "
        L"Remove-Item -Path $p1, $p2 -Force -ErrorAction SilentlyContinue\"");

    /* Register Task Scheduler task with Highest (Admin) privileges, full battery support, and no timeout */
    wchar_t sch_cmd[2048];
    swprintf_s(sch_cmd, sizeof(sch_cmd) / sizeof(wchar_t),
        L"powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        L"$action = New-ScheduledTaskAction -Execute '%ls' -WorkingDirectory '%ls'; "
        L"$trigger = New-ScheduledTaskTrigger -AtLogOn; "
        L"$principal = New-ScheduledTaskPrincipal -GroupId 'BUILTIN\\Users' -RunLevel Highest; "
        L"$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0 -Priority 4 -MultipleInstances IgnoreNew; "
        L"$task = New-ScheduledTask -Action $action -Trigger $trigger -Principal $principal -Settings $settings; "
        L"Register-ScheduledTask -TaskName 'CapsLayer' -InputObject $task -Force\"",
        target_exe, target_dir);
    if (!run_command_hidden(sch_cmd)) {
        wchar_t sch_fallback[1024];
        swprintf_s(sch_fallback, sizeof(sch_fallback) / sizeof(wchar_t),
            L"schtasks /Create /TN \"CapsLayer\" /TR \"\\\"%ls\\\"\" /SC ONLOGON /RL HIGHEST /F /DELAY 0000:00",
            target_exe);
        run_command_hidden(sch_fallback);
    }

    return true;
}

static bool setup_disable_startup(void)
{
    run_command_hidden(L"schtasks /Delete /TN \"CapsLayer\" /F");
    run_command_hidden(L"reg delete \"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_command_hidden(L"reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_command_hidden(L"powershell -NoProfile -ExecutionPolicy Bypass -Command \""
        L"$p1 = [System.IO.Path]::Combine($env:APPDATA, 'Microsoft\\Windows\\Start Menu\\Programs\\Startup\\CapsLayer.lnk'); "
        L"$p2 = [System.IO.Path]::Combine($env:ProgramData, 'Microsoft\\Windows\\Start Menu\\Programs\\Startup\\CapsLayer.lnk'); "
        L"Remove-Item -Path $p1, $p2 -Force -ErrorAction SilentlyContinue\"");
    return true;
}
static bool setup_install(void)
{
    wchar_t src_exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, src_exe, MAX_PATH)) {
        printf("[Error] Failed to determine source executable path.\n");
        return false;
    }

    wchar_t prog_files[MAX_PATH];
    get_install_base_path(prog_files, MAX_PATH);

    wchar_t target_dir[MAX_PATH];
    swprintf_s(target_dir, MAX_PATH, L"%s\\capslayer", prog_files);

    wchar_t target_exe[MAX_PATH];
    swprintf_s(target_exe, MAX_PATH, L"%s\\capslayer\\capslayer.exe", prog_files);

    wchar_t target_cfg[MAX_PATH];
    swprintf_s(target_cfg, MAX_PATH, L"%s\\capslayer\\config.json", prog_files);

    printf("===================================================\n");
    printf("      CapsLayer Automated Windows Installer\n");
    printf("===================================================\n\n");
    printf("[1/6] Target directory: %ls\n", target_dir);

    /* Stop running instances */
    printf("[2/6] Stopping any running instances...\n");
    run_command_hidden(L"taskkill /F /IM capslayer.exe");
    Sleep(500);

    /* Create target directory */
    printf("[3/6] Creating directory...\n");
    CreateDirectoryW(target_dir, NULL);

    /* Copy executable */
    printf("[4/6] Copying files...\n");
    if (!CopyFileW(src_exe, target_exe, FALSE)) {
        printf("[Error] Failed to copy capslayer.exe (Error: %lu)\n", GetLastError());
        return false;
    }
    printf("  - Copied capslayer.exe\n");

    /* Copy config.json if source exists and target does not exist */
    wchar_t src_dir[MAX_PATH];
    wcscpy_s(src_dir, MAX_PATH, src_exe);
    wchar_t *last_slash = wcsrchr(src_dir, L'\\');
    if (last_slash) *last_slash = L'\0';
    wchar_t src_cfg[MAX_PATH];
    swprintf_s(src_cfg, MAX_PATH, L"%s\\config.json", src_dir);

    if (GetFileAttributesW(target_cfg) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(src_cfg) != INVALID_FILE_ATTRIBUTES) {
        CopyFileW(src_cfg, target_cfg, FALSE);
        printf("  - Copied default config.json\n");
    } else if (GetFileAttributesW(target_cfg) != INVALID_FILE_ATTRIBUTES) {
        printf("  - Preserved existing config.json\n");
    }

    /* Set folder permissions for user config edits */
    wchar_t acl_cmd[1024];
    swprintf_s(acl_cmd, sizeof(acl_cmd) / sizeof(wchar_t),
        L"icacls \"%ls\" /grant *S-1-5-32-545:(OI)(CI)M /T /Q", target_dir);
    run_command_hidden(acl_cmd);

    /* Register Multi-layer Windows Startup */
    printf("[5/6] Registering Windows Startup (Registry, Startup Folder & Task Scheduler)...\n");
    setup_enable_startup();
    printf("  - Startup registered across Registry Run keys, Startup folder, and Task Scheduler\n");
    /* Create Start Menu Shortcut */
    printf("[6/6] Creating Start Menu shortcut & launching...\n");
    wchar_t sc_cmd[2048];
    swprintf_s(sc_cmd, sizeof(sc_cmd) / sizeof(wchar_t),
        L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ws = New-Object -ComObject WScript.Shell; $scPath = [System.IO.Path]::Combine($env:ProgramData, 'Microsoft\\Windows\\Start Menu\\Programs\\CapsLayer.lnk'); $sc = $ws.CreateShortcut($scPath); $sc.TargetPath = '%ls'; $sc.WorkingDirectory = '%ls'; $sc.Description = 'CapsLayer Keyboard Remapper'; $sc.Save()\"",
        target_exe, target_dir);
    run_command_hidden(sc_cmd);

    /* Launch installed daemon */
    ShellExecuteW(NULL, L"open", target_exe, NULL, target_dir, SW_SHOWNORMAL);

    printf("\n===================================================\n");
    printf("[SUCCESS] CapsLayer installed successfully!\n");
    printf("  - Location: %ls\n", target_dir);
    printf("  - Startup : Enabled (Task Scheduler: CapsLayer)\n");
    printf("  - Status  : Daemon started in background (see System Tray)\n");
    printf("===================================================\n");
    return true;
}

static bool setup_uninstall(void)
{
    wchar_t prog_files[MAX_PATH];
    get_install_base_path(prog_files, MAX_PATH);

    wchar_t target_dir[MAX_PATH];
    swprintf_s(target_dir, MAX_PATH, L"%s\\capslayer", prog_files);

    printf("===================================================\n");
    printf("            CapsLayer Uninstaller\n");
    printf("===================================================\n\n");

    printf("[1/4] Stopping CapsLayer processes...\n");
    run_command_hidden(L"taskkill /F /IM capslayer.exe");
    Sleep(500);

    printf("[2/4] Removing Task Scheduler startup task...\n");
    setup_disable_startup();

    printf("[3/4] Removing Start Menu shortcuts & Registry entries...\n");
    run_command_hidden(L"reg delete \"HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");
    run_command_hidden(L"reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v \"CapsLayer\" /f");

    wchar_t del_sc[1024];
    swprintf_s(del_sc, sizeof(del_sc) / sizeof(wchar_t),
        L"del /F /Q \"%s\\Microsoft\\Windows\\Start Menu\\Programs\\CapsLayer.lnk\"",
        _wgetenv(L"ProgramData") ? _wgetenv(L"ProgramData") : L"C:\\ProgramData");
    run_command_hidden(del_sc);

    printf("[4/4] Removing installation directory '%ls'...\n", target_dir);
    wchar_t rd_cmd[1024];
    swprintf_s(rd_cmd, sizeof(rd_cmd) / sizeof(wchar_t), L"rmdir /S /Q \"%ls\"", target_dir);
    run_command_hidden(rd_cmd);

    printf("\n===================================================\n");
    printf("[SUCCESS] CapsLayer has been completely uninstalled.\n");
    printf("===================================================\n");
    return true;
}

static void setup_show_status(void)
{
    wchar_t prog_files[MAX_PATH];
    get_install_base_path(prog_files, MAX_PATH);

    wchar_t target_exe[MAX_PATH];
    swprintf_s(target_exe, MAX_PATH, L"%s\\capslayer\\capslayer.exe", prog_files);

    wchar_t target_cfg[MAX_PATH];
    swprintf_s(target_cfg, MAX_PATH, L"%s\\capslayer\\config.json", prog_files);

    printf("===================================================\n");
    printf("         CapsLayer Installation Status\n");
    printf("===================================================\n");
    printf("  - Installation Directory : %ls\\capslayer\n", prog_files);
    printf("  - Executable Installed   : %s\n", (GetFileAttributesW(target_exe) != INVALID_FILE_ATTRIBUTES) ? "YES" : "NO");
    printf("  - Config File Present    : %s\n", (GetFileAttributesW(target_cfg) != INVALID_FILE_ATTRIBUTES) ? "YES" : "NO");

    bool task_exists = run_command_hidden(L"schtasks /Query /TN \"CapsLayer\"");
    printf("  - Startup Task (Logon)   : %s\n", task_exists ? "ENABLED (Task Scheduler: CapsLayer)" : "DISABLED");
    printf("===================================================\n");
}

static void attach_console_for_cli(bool force_alloc)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD file_type = (hOut != NULL && hOut != INVALID_HANDLE_VALUE) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;

    if (file_type == FILE_TYPE_PIPE || file_type == FILE_TYPE_DISK) {
        /* Standard output is already redirected to a pipe or file */
        return;
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
    } else if (force_alloc) {
        if (AllocConsole()) {
            FILE *fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
        }
    }
}

static void parse_cli_args(int argc, char *argv[])
{
    memset(&g_app, 0, sizeof(AppContext));
    config_get_default_path(g_app.config_path, sizeof(g_app.config_path));

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0 || strcmp(arg, "/?") == 0 || strcmp(arg, "/h") == 0) {
            attach_console_for_cli(false);
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0 || strcmp(arg, "/v") == 0) {
            attach_console_for_cli(false);
            printf("CapsLayer version %s\n", CAPSLAYER_VERSION);
            exit(0);
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--test-config") == 0) {
            g_app.test_config_only = true;
        } else if (strcmp(arg, "--install") == 0 || strcmp(arg, "/install") == 0 || strcmp(arg, "-i") == 0) {
            g_app.do_install = true;
        } else if (strcmp(arg, "--uninstall") == 0 || strcmp(arg, "/uninstall") == 0 || strcmp(arg, "-u") == 0) {
            g_app.do_uninstall = true;
        } else if (strcmp(arg, "--enable-startup") == 0 || strcmp(arg, "--startup") == 0) {
            g_app.do_enable_startup = true;
        } else if (strcmp(arg, "--disable-startup") == 0 || strcmp(arg, "--no-startup") == 0) {
            g_app.do_disable_startup = true;
        } else if (strcmp(arg, "--status") == 0) {
            g_app.do_status = true;
        } else if (strcmp(arg, "--console") == 0) {
            g_app.console_mode = true;
        } else if (strcmp(arg, "--paused") == 0) {
            g_app.paused_initial = true;
        } else if (strcmp(arg, "--no-elevate") == 0) {
            g_app.no_elevate = true;
        } else if ((strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) && (i + 1 < argc)) {
            strncpy(g_app.config_path, argv[++i], sizeof(g_app.config_path) - 1);
            g_app.config_path[sizeof(g_app.config_path) - 1] = '\0';
        }
    }

    config_get_dir_path(g_app.config_path, g_app.config_dir, sizeof(g_app.config_dir));
}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        attach_console_for_cli(false);
    }

    parse_cli_args(argc, argv);

    if (g_app.test_config_only) {
        printf("[CapsLayer] Testing configuration from '%s'...\n", g_app.config_path);
        capslayer_config_t cfg;
        if (!config_load_from_file(g_app.config_path, &cfg)) {
            printf("[CapsLayer] ERROR: Failed to parse configuration file '%s'.\n", g_app.config_path);
            return 1;
        }
        printf("[CapsLayer] SUCCESS: Configuration is valid.\n");
        printf("  - capslock_tap_as_esc: %s\n", cfg.settings.capslock_tap_as_esc ? "true" : "false");
        printf("  - esc_tap_as_capslock: %s\n", cfg.settings.esc_tap_as_capslock ? "true" : "false");
        printf("  - swap_esc_and_capslock: %s\n", cfg.settings.swap_esc_and_capslock ? "true" : "false");
        printf("  - unmapped_passthrough: %s\n", cfg.settings.unmapped_passthrough ? "true" : "false");

        int mapping_count = 0;
        for (int k = 0; k < 256; ++k) {
            if (cfg.layer_map[k].type != ACTION_NONE) {
                mapping_count++;
            }
        }
        printf("  - Active layer mappings: %d keys defined\n", mapping_count);
        return 0;
    }

    if (g_app.do_status) {
        setup_show_status();
        return 0;
    }

    /* Handle setup commands that require admin rights */
    if (g_app.do_install || g_app.do_uninstall || g_app.do_enable_startup || g_app.do_disable_startup) {
        if (!g_app.no_elevate && !is_running_as_admin()) {
            if (relaunch_as_admin(argc, argv)) {
                return 0;
            }
        }

        if (g_app.do_install) {
            return setup_install() ? 0 : 1;
        }
        if (g_app.do_uninstall) {
            return setup_uninstall() ? 0 : 1;
        }
        if (g_app.do_enable_startup) {
            bool ok = setup_enable_startup();
            printf("[Startup] %s.\n", ok ? "Windows Startup task enabled successfully" : "Failed to enable startup task");
            return ok ? 0 : 1;
        }
        if (g_app.do_disable_startup) {
            bool ok = setup_disable_startup();
            printf("[Startup] %s.\n", ok ? "Windows Startup task removed successfully" : "Failed to remove startup task");
            return ok ? 0 : 1;
        }
    }


    if (g_app.console_mode) {
        attach_console_for_cli(true);
    }

    return run_app(GetModuleHandle(NULL));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return main(__argc, __argv);
}
