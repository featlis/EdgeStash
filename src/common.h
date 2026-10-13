#ifndef EDGESTASH_COMMON_H
#define EDGESTASH_COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define APP_NAME            L"EdgeStash"
#define APP_VERSION         L"2.0.0"
#define WINDOW_CLASS_NAME   L"EdgeStash_WindowClass"
#define SINGLE_INSTANCE_MUTEX L"EdgeStash_SingleInstance_Mutex_v2"

// Custom window messages
#define WM_APP_ITEMS_UPDATED    (WM_APP + 101)
#define WM_APP_TRAY_MSG         (WM_APP + 102)
#define WM_APP_FLASH_EDGE       (WM_APP + 103)
#define WM_APP_TOGGLE_EXPAND    (WM_APP + 104)

// Timer IDs
#define TIMER_ID_ANIM           2001
#define TIMER_ID_AUTO_COLLAPSE  2002
#define TIMER_ID_TRIM_WS        2003

// Hotkey ID
#define HOTKEY_TOGGLE_ID        3001

// Dimensions (Base 96 DPI)
#define DEFAULT_EXPANDED_WIDTH  340
#define DEFAULT_COLLAPSED_WIDTH 6
#define DEFAULT_COLLAPSED_HEIGHT 150
#define AUTO_COLLAPSE_DELAY_MS  400
#define WORKING_SET_TRIM_DELAY_MS 5000

// Item Types
typedef enum {
    ITEM_TYPE_UNKNOWN = 0,
    ITEM_TYPE_FILE    = 1,
    ITEM_TYPE_TEXT    = 2,
    ITEM_TYPE_IMAGE   = 3
} ItemType;

// Stash Item
typedef struct {
    int64_t id;
    ItemType type;
    wchar_t title[260];
    wchar_t content[2048];
    wchar_t file_path[MAX_PATH];
    wchar_t thumb_path[MAX_PATH];
    int64_t file_size;
    int64_t created_at;     // Unix timestamp (sec)
    int64_t expires_at;     // Unix timestamp (sec)
    int is_pinned;          // 1: pinned (never expires), 0: 24h auto-expire
} StashItem;

// Window UI States
typedef enum {
    WND_STATE_COLLAPSED = 0,
    WND_STATE_EXPANDING = 1,
    WND_STATE_EXPANDED  = 2,
    WND_STATE_COLLAPSING = 3
} WindowState;

// Hit test action types for clicks / drags
typedef enum {
    HIT_NONE = 0,
    HIT_HEADER_PIN_TOGGLE,
    HIT_HEADER_CLEAR_EXPIRED,
    HIT_HEADER_COLLAPSE,
    HIT_CARD_BODY,
    HIT_CARD_COPY,
    HIT_CARD_PIN_TOGGLE,
    HIT_CARD_DELETE,
    HIT_CARD_DRAG_GRIP
} HitActionType;

typedef struct {
    HitActionType type;
    int64_t item_id;
    int item_index;
    RECT rect;
} HitTarget;

#endif // EDGESTASH_COMMON_H
