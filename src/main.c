#include "common.h"
#include "window.h"
#include "storage.h"
#include "renderer.h"
#include "ttl_daemon.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // 1. Single Instance Check
    HANDLE hMutex = CreateMutexW(NULL, TRUE, SINGLE_INSTANCE_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // App already running; bring existing instance to front
        HWND existingWnd = FindWindowW(WINDOW_CLASS_NAME, NULL);
        if (existingWnd) {
            PostMessage(existingWnd, WM_APP_TOGGLE_EXPAND, 1, 0);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // 2. Initialize OLE / COM for Drag & Drop
    HRESULT hrOle = OleInitialize(NULL);
    if (FAILED(hrOle)) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 3. Initialize GDI+ Rendering Engine
    if (!Renderer_Init()) {
        OleUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 4. Initialize Local AppData Storage (SQLite & Cache directories)
    if (!Storage_Init(NULL)) {
        Renderer_Shutdown();
        OleUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 5. Purge any items expired while EdgeStash was not running
    Storage_PurgeExpiredItems();

    // 6. Create Main Edge Dock Window
    HWND hwnd = Window_Create(hInstance);
    if (!hwnd) {
        Storage_Shutdown();
        Renderer_Shutdown();
        OleUninitialize();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    // 7. Start 24H TTL Background Daemon
    TTLDaemon_Start(hwnd);

    // 8. Standard Win32 Message Loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 9. Graceful Shutdown & Resource Cleanup
    TTLDaemon_Stop();
    Storage_Shutdown();
    Renderer_Shutdown();
    OleUninitialize();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return (int)msg.wParam;
}
