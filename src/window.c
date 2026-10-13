#include "window.h"
#include "renderer.h"
#include "storage.h"
#include "tray.h"
#include "drop_target.h"
#include "drop_source.h"
#include <math.h>

static UIState g_state = { 0 };
static IDropTarget* g_pDropTarget = NULL;

static int g_screenWidth = 1920;
static int g_screenHeight = 1080;
static RECT g_rcWorkArea = { 0, 0, 1920, 1080 };

// Animation variables
static DWORD g_animStartTime = 0;
static const DWORD ANIM_DURATION_MS = 160;

// Drag-out detection
static bool g_dragCandidate = false;
static POINT g_dragStartPoint = { 0, 0 };
static int g_dragItemIndex = -1;

// DPI helper
static int GetWindowDpi(HWND hwnd) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)GetProcAddress(hUser32, "GetDpiForWindow");
    if (pfn) return (int)pfn(hwnd);
    return 96;
}

static void UpdateScreenMetrics(HWND hwnd) {
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(hMon, &mi)) {
        g_rcWorkArea = mi.rcWork;
        g_screenWidth = mi.rcWork.right - mi.rcWork.left;
        g_screenHeight = mi.rcWork.bottom - mi.rcWork.top;
    } else {
        g_rcWorkArea.left = 0;
        g_rcWorkArea.top = 0;
        g_rcWorkArea.right = GetSystemMetrics(SM_CXSCREEN);
        g_rcWorkArea.bottom = GetSystemMetrics(SM_CYSCREEN);
        g_screenWidth = g_rcWorkArea.right;
        g_screenHeight = g_rcWorkArea.bottom;
    }
}

static void GetTargetDimensions(int* outCollapsedW, int* outCollapsedH, int* outExpandedW, int* outExpandedH) {
    float scale = (float)g_state.dpi / 96.0f;
    if (scale < 1.0f) scale = 1.0f;

    *outCollapsedW = (int)(DEFAULT_COLLAPSED_WIDTH * scale);
    *outCollapsedH = (int)(DEFAULT_COLLAPSED_HEIGHT * scale);
    *outExpandedW = (int)(DEFAULT_EXPANDED_WIDTH * scale);
    *outExpandedH = (int)(g_screenHeight * 0.82f);
}

void Window_RefreshItems(HWND hwnd) {
    if (g_state.items) {
        Storage_FreeItems(g_state.items);
        g_state.items = NULL;
        g_state.itemCount = 0;
    }
    Storage_GetItems(&g_state.items, &g_state.itemCount);
    InvalidateRect(hwnd, NULL, FALSE);
}

void Window_Expand(HWND hwnd) {
    if (g_state.windowState == WND_STATE_EXPANDED || g_state.windowState == WND_STATE_EXPANDING) {
        return;
    }

    KillTimer(hwnd, TIMER_ID_AUTO_COLLAPSE);
    KillTimer(hwnd, TIMER_ID_TRIM_WS);

    // Reload items if coming from collapsed
    if (g_state.windowState == WND_STATE_COLLAPSED) {
        Window_RefreshItems(hwnd);
    }

    g_state.windowState = WND_STATE_EXPANDING;
    g_animStartTime = GetTickCount();
    SetTimer(hwnd, TIMER_ID_ANIM, 16, NULL);
}

void Window_Collapse(HWND hwnd) {
    if (g_state.windowState == WND_STATE_COLLAPSED || g_state.windowState == WND_STATE_COLLAPSING) {
        return;
    }

    KillTimer(hwnd, TIMER_ID_AUTO_COLLAPSE);

    g_state.windowState = WND_STATE_COLLAPSING;
    g_animStartTime = GetTickCount();
    SetTimer(hwnd, TIMER_ID_ANIM, 16, NULL);
}

void Window_ToggleExpand(HWND hwnd) {
    if (g_state.windowState == WND_STATE_COLLAPSED || g_state.windowState == WND_STATE_COLLAPSING) {
        Window_Expand(hwnd);
    } else {
        Window_Collapse(hwnd);
    }
}

bool Window_IsPinned(void) {
    return g_state.isPanelPinned;
}

void Window_SetPinned(HWND hwnd, bool pinned) {
    g_state.isPanelPinned = pinned;
    if (pinned) {
        KillTimer(hwnd, TIMER_ID_AUTO_COLLAPSE);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void CopyItemToClipboard(HWND hwnd, const StashItem* item) {
    if (!item) return;

    if (item->type == ITEM_TYPE_TEXT) {
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            size_t len = wcslen(item->content);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
            if (hMem) {
                wchar_t* p = (wchar_t*)GlobalLock(hMem);
                if (p) {
                    memcpy(p, item->content, (len + 1) * sizeof(wchar_t));
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
        }
    } else if (item->type == ITEM_TYPE_FILE || item->type == ITEM_TYPE_IMAGE) {
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            size_t len = wcslen(item->file_path);
            size_t allocSize = sizeof(DROPFILES) + (len + 2) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, allocSize);
            if (hMem) {
                BYTE* p = (BYTE*)GlobalLock(hMem);
                if (p) {
                    DROPFILES* df = (DROPFILES*)p;
                    df->pFiles = sizeof(DROPFILES);
                    df->fWide = TRUE;
                    wchar_t* pStr = (wchar_t*)(p + sizeof(DROPFILES));
                    wcsncpy_s(pStr, len + 1, item->file_path, _TRUNCATE);
                    pStr[len] = L'\0';
                    pStr[len + 1] = L'\0';
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_HDROP, hMem);
                }
            }
            CloseClipboard();
        }
    }
}

static void OpenCacheFolder(void) {
    const wchar_t* cacheDir = Storage_GetCacheDir();
    ShellExecuteW(NULL, L"open", cacheDir, NULL, NULL, SW_SHOWNORMAL);
}

// Window Procedure
static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        UpdateScreenMetrics(hwnd);
        g_state.dpi = GetWindowDpi(hwnd);
        g_state.windowState = WND_STATE_COLLAPSED;
        g_state.animProgress = 0.0f;
        g_state.isPanelPinned = false;
        g_state.scrollY = 0;
        g_state.hoveredItemIndex = -1;
        g_state.hoveredAction = HIT_NONE;

        int colW, colH, expW, expH;
        GetTargetDimensions(&colW, &colH, &expW, &expH);

        int curX = g_rcWorkArea.right - colW;
        int curY = g_rcWorkArea.top + (g_screenHeight - colH) / 2;

        SetWindowPos(hwnd, HWND_TOPMOST, curX, curY, colW, colH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

        // Register Global Hotkey (Win + Alt + V)
        RegisterHotKey(hwnd, HOTKEY_TOGGLE_ID, MOD_WIN | MOD_ALT | MOD_NOREPEAT, 'V');

        // Register OLE Drop Target
        g_pDropTarget = DropTarget_Create(hwnd);
        if (g_pDropTarget) {
            RegisterDragDrop(hwnd, g_pDropTarget);
        }

        // Initialize System Tray
        HICON hAppIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));
        Tray_Init(hwnd, hAppIcon);

        return 0;
    }

    case WM_DPICHANGED: {
        g_state.dpi = LOWORD(wParam);
        UpdateScreenMetrics(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_APP_TOGGLE_EXPAND: {
        if (wParam == 1) {
            Window_Expand(hwnd);
        } else {
            Window_Collapse(hwnd);
        }
        return 0;
    }

    case WM_APP_ITEMS_UPDATED: {
        Window_RefreshItems(hwnd);
        return 0;
    }

    case WM_HOTKEY: {
        if (wParam == HOTKEY_TOGGLE_ID) {
            Window_ToggleExpand(hwnd);
        }
        return 0;
    }

    case WM_APP_TRAY_MSG: {
        if (lParam == WM_LBUTTONUP) {
            Window_ToggleExpand(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            Tray_ShowContextMenu(hwnd, g_state.isPanelPinned);
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);
        switch (cmd) {
        case IDM_TRAY_TOGGLE:
            Window_ToggleExpand(hwnd);
            break;
        case IDM_TRAY_PIN_DOCK:
            Window_SetPinned(hwnd, !g_state.isPanelPinned);
            break;
        case IDM_TRAY_OPEN_CACHE:
            OpenCacheFolder();
            break;
        case IDM_TRAY_PURGE_NOW:
            Storage_PurgeExpiredItems();
            Window_RefreshItems(hwnd);
            break;
        case IDM_TRAY_CLEAR_ALL:
            if (MessageBoxW(hwnd, L"Clear all stashed items?", APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                Storage_ClearAll(false);
                Window_RefreshItems(hwnd);
            }
            break;
        case IDM_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == TIMER_ID_ANIM) {
            DWORD elapsed = GetTickCount() - g_animStartTime;
            float t = (float)elapsed / (float)ANIM_DURATION_MS;
            bool animFinished = false;

            if (t >= 1.0f) {
                t = 1.0f;
                animFinished = true;
            }

            // Cubic ease out
            float ease = 1.0f - powf(1.0f - t, 3.0f);

            int colW, colH, expW, expH;
            GetTargetDimensions(&colW, &colH, &expW, &expH);

            int curW, curH;
            if (g_state.windowState == WND_STATE_EXPANDING) {
                curW = colW + (int)((expW - colW) * ease);
                curH = colH + (int)((expH - colH) * ease);
                g_state.animProgress = ease;
                if (animFinished) {
                    g_state.windowState = WND_STATE_EXPANDED;
                    g_state.animProgress = 1.0f;
                    KillTimer(hwnd, TIMER_ID_ANIM); // Terminate timer immediately!
                }
            } else if (g_state.windowState == WND_STATE_COLLAPSING) {
                curW = expW - (int)((expW - colW) * ease);
                curH = expH - (int)((expH - colH) * ease);
                g_state.animProgress = 1.0f - ease;
                if (animFinished) {
                    g_state.windowState = WND_STATE_COLLAPSED;
                    g_state.animProgress = 0.0f;
                    KillTimer(hwnd, TIMER_ID_ANIM); // Terminate timer immediately!

                    // Set 5-second timer for Working Set Trim (Section 6.2)
                    SetTimer(hwnd, TIMER_ID_TRIM_WS, WORKING_SET_TRIM_DELAY_MS, NULL);
                }
            } else {
                KillTimer(hwnd, TIMER_ID_ANIM);
                return 0;
            }

            int curX = g_rcWorkArea.right - curW;
            int curY = g_rcWorkArea.top + (g_screenHeight - curH) / 2;

            SetWindowPos(hwnd, HWND_TOPMOST, curX, curY, curW, curH, SWP_NOACTIVATE | SWP_NOZORDER);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (wParam == TIMER_ID_AUTO_COLLAPSE) {
            KillTimer(hwnd, TIMER_ID_AUTO_COLLAPSE);

            // Double check cursor position
            POINT pt;
            GetCursorPos(&pt);
            RECT rcWin;
            GetWindowRect(hwnd, &rcWin);

            if (!PtInRect(&rcWin, pt) && !g_state.isPanelPinned) {
                Window_Collapse(hwnd);
            }
            return 0;
        }

        if (wParam == TIMER_ID_TRIM_WS) {
            KillTimer(hwnd, TIMER_ID_TRIM_WS);

            // Only trim if still collapsed
            if (g_state.windowState == WND_STATE_COLLAPSED) {
                Renderer_FlushThumbnails();
                if (g_state.items) {
                    Storage_FreeItems(g_state.items);
                    g_state.items = NULL;
                    g_state.itemCount = 0;
                }
                // Force working set trim to release RAM back to OS
                SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
            }
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        g_state.mousePos.x = x;
        g_state.mousePos.y = y;

        // Auto-expand on hover if collapsed
        if (g_state.windowState == WND_STATE_COLLAPSED) {
            Window_Expand(hwnd);
            return 0;
        }

        // Cancel auto-collapse timer since mouse is inside
        KillTimer(hwnd, TIMER_ID_AUTO_COLLAPSE);

        // Track mouse leave
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = HOVER_DEFAULT;
        TrackMouseEvent(&tme);

        // Check Drag-Out initiation
        if ((wParam & MK_LBUTTON) && g_dragCandidate && g_dragItemIndex >= 0 && g_dragItemIndex < g_state.itemCount) {
            int dx = abs(x - g_dragStartPoint.x);
            int dy = abs(y - g_dragStartPoint.y);
            if (dx > 5 || dy > 5) {
                g_dragCandidate = false;
                const StashItem* item = &g_state.items[g_dragItemIndex];
                DropSource_StartDrag(item);
                return 0;
            }
        }

        // Hit testing for hover feedback
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HitTarget target;
        if (Renderer_HitTest(rcClient.right, rcClient.bottom, &g_state, x, y, &target)) {
            if (g_state.hoveredItemIndex != target.item_index ||
                g_state.hoveredAction != target.type ||
                g_state.hoveredItemId != target.item_id) {
                g_state.hoveredItemIndex = target.item_index;
                g_state.hoveredAction = target.type;
                g_state.hoveredItemId = target.item_id;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else {
            if (g_state.hoveredItemIndex != -1 || g_state.hoveredAction != HIT_NONE) {
                g_state.hoveredItemIndex = -1;
                g_state.hoveredAction = HIT_NONE;
                g_state.hoveredItemId = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_dragCandidate = false;
        g_state.hoveredItemIndex = -1;
        g_state.hoveredAction = HIT_NONE;
        g_state.hoveredItemId = 0;
        InvalidateRect(hwnd, NULL, FALSE);

        // Trigger auto collapse after delay if not pinned
        if ((g_state.windowState == WND_STATE_EXPANDED || g_state.windowState == WND_STATE_EXPANDING) &&
            !g_state.isPanelPinned) {
            SetTimer(hwnd, TIMER_ID_AUTO_COLLAPSE, AUTO_COLLAPSE_DELAY_MS, NULL);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HitTarget target;
        if (Renderer_HitTest(rcClient.right, rcClient.bottom, &g_state, x, y, &target)) {
            switch (target.type) {
            case HIT_HEADER_PIN_TOGGLE:
                Window_SetPinned(hwnd, !g_state.isPanelPinned);
                break;
            case HIT_HEADER_CLEAR_EXPIRED:
                Storage_PurgeExpiredItems();
                Storage_ClearAll(true);
                Window_RefreshItems(hwnd);
                break;
            case HIT_HEADER_COLLAPSE:
                Window_Collapse(hwnd);
                break;
            case HIT_CARD_DELETE:
                Storage_DeleteItem(target.item_id);
                Window_RefreshItems(hwnd);
                break;
            case HIT_CARD_PIN_TOGGLE:
                Storage_TogglePin(target.item_id);
                Window_RefreshItems(hwnd);
                break;
            case HIT_CARD_COPY:
                if (target.item_index >= 0 && target.item_index < g_state.itemCount) {
                    CopyItemToClipboard(hwnd, &g_state.items[target.item_index]);
                }
                break;
            case HIT_CARD_BODY:
            case HIT_CARD_DRAG_GRIP:
                g_dragCandidate = true;
                g_dragStartPoint.x = x;
                g_dragStartPoint.y = y;
                g_dragItemIndex = target.item_index;
                break;
            default:
                break;
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        g_dragCandidate = false;
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        int totalH = Renderer_GetTotalContentHeight(rcClient.right, &g_state);
        int maxScroll = max(0, totalH - rcClient.bottom);

        g_state.scrollY -= (delta / 2);
        if (g_state.scrollY < 0) g_state.scrollY = 0;
        if (g_state.scrollY > maxScroll) g_state.scrollY = maxScroll;

        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        int w = rcClient.right - rcClient.left;
        int h = rcClient.bottom - rcClient.top;

        if (w > 0 && h > 0) {
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

            Renderer_Render(hdcMem, w, h, &g_state);

            BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent flicker

    case WM_DESTROY: {
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE_ID);
        Tray_Remove();

        if (g_pDropTarget) {
            RevokeDragDrop(hwnd);
            g_pDropTarget->lpVtbl->Release(g_pDropTarget);
            g_pDropTarget = NULL;
        }

        if (g_state.items) {
            Storage_FreeItems(g_state.items);
            g_state.items = NULL;
            g_state.itemCount = 0;
        }

        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

HWND Window_Create(HINSTANCE hInstance) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(WNDCLASSEXW));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // Handled by double-buffer
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WINDOW_CLASS_NAME,
        APP_NAME,
        WS_POPUP,
        0, 0, DEFAULT_COLLAPSED_WIDTH, DEFAULT_COLLAPSED_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd) {
        // Set layered window alpha (245/255 for sleek dark Fluent feel)
        SetLayeredWindowAttributes(hwnd, 0, 245, LWA_ALPHA);
    }

    return hwnd;
}

void Window_Destroy(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}
