#include "ttl_daemon.h"
#include "storage.h"

static HANDLE g_hThread = NULL;
static HANDLE g_hStopEvent = NULL;
static HANDLE g_hTriggerEvent = NULL;
static HWND g_hwndNotify = NULL;
static volatile bool g_running = false;

static DWORD WINAPI TTLWorkerThread(LPVOID lpParam) {
    (void)lpParam;
    HANDLE waitHandles[2] = { g_hStopEvent, g_hTriggerEvent };

    while (g_running) {
        // Wait 60 seconds, or stop event, or manual trigger event
        DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, 60000);
        if (waitRes == WAIT_OBJECT_0) {
            // Stop event signaled
            break;
        }

        // Purge expired items
        int purged = Storage_PurgeExpiredItems();
        if (purged > 0 && g_hwndNotify && IsWindow(g_hwndNotify)) {
            PostMessage(g_hwndNotify, WM_APP_ITEMS_UPDATED, 0, 0);
        }

        if (waitRes == WAIT_OBJECT_0 + 1) {
            // Reset manual trigger event
            ResetEvent(g_hTriggerEvent);
        }
    }

    return 0;
}

bool TTLDaemon_Start(HWND hwndNotify) {
    if (g_running) return true;

    g_hwndNotify = hwndNotify;
    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hTriggerEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_hStopEvent || !g_hTriggerEvent) {
        if (g_hStopEvent) CloseHandle(g_hStopEvent);
        if (g_hTriggerEvent) CloseHandle(g_hTriggerEvent);
        return false;
    }

    g_running = true;
    g_hThread = CreateThread(NULL, 0, TTLWorkerThread, NULL, 0, NULL);
    if (!g_hThread) {
        g_running = false;
        CloseHandle(g_hStopEvent);
        CloseHandle(g_hTriggerEvent);
        g_hStopEvent = NULL;
        g_hTriggerEvent = NULL;
        return false;
    }

    return true;
}

void TTLDaemon_TriggerImmediatePurge(void) {
    if (g_hTriggerEvent) {
        SetEvent(g_hTriggerEvent);
    }
}

void TTLDaemon_Stop(void) {
    if (!g_running) return;

    g_running = false;
    if (g_hStopEvent) {
        SetEvent(g_hStopEvent);
    }

    if (g_hThread) {
        WaitForSingleObject(g_hThread, 3000);
        CloseHandle(g_hThread);
        g_hThread = NULL;
    }

    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }

    if (g_hTriggerEvent) {
        CloseHandle(g_hTriggerEvent);
        g_hTriggerEvent = NULL;
    }
}
