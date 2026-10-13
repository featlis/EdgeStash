#include "tray.h"

static NOTIFYICONDATAW g_nid = { 0 };
static bool g_trayAdded = false;

bool Tray_Init(HWND hwnd, HICON hIcon) {
    if (g_trayAdded) return true;

    memset(&g_nid, 0, sizeof(NOTIFYICONDATAW));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY_MSG;
    g_nid.hIcon = hIcon ? hIcon : LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, 128, L"EdgeStash - 24h Auto-Expiring Scrap Dock");

    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        g_trayAdded = true;
        return true;
    }
    return false;
}

void Tray_Remove(void) {
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = false;
    }
}

void Tray_ShowContextMenu(HWND hwnd, bool isPinned) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TOGGLE, L"Toggle Dock\tWin+Alt+V");
    AppendMenuW(hMenu, MF_STRING | (isPinned ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_PIN_DOCK, L"Pin Dock on Screen");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN_CACHE, L"Open Cache Folder");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_PURGE_NOW, L"Purge Expired Now");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_CLEAR_ALL, L"Clear All Items");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit EdgeStash");

    SetMenuDefaultItem(hMenu, IDM_TRAY_TOGGLE, FALSE);

    // Required for tray popup menu to close properly when clicking away
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}
