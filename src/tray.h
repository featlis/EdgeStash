#ifndef EDGESTASH_TRAY_H
#define EDGESTASH_TRAY_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IDM_TRAY_TOGGLE         4001
#define IDM_TRAY_PIN_DOCK       4002
#define IDM_TRAY_OPEN_CACHE     4003
#define IDM_TRAY_PURGE_NOW      4004
#define IDM_TRAY_CLEAR_ALL      4005
#define IDM_TRAY_EXIT           4006

bool Tray_Init(HWND hwnd, HICON hIcon);
void Tray_Remove(void);
void Tray_ShowContextMenu(HWND hwnd, bool isPinned);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_TRAY_H
