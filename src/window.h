#ifndef EDGESTASH_WINDOW_H
#define EDGESTASH_WINDOW_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates and registers main window
HWND Window_Create(HINSTANCE hInstance);
void Window_Destroy(HWND hwnd);

// State queries and controls
void Window_ToggleExpand(HWND hwnd);
void Window_Expand(HWND hwnd);
void Window_Collapse(HWND hwnd);
void Window_RefreshItems(HWND hwnd);
bool Window_IsPinned(void);
void Window_SetPinned(HWND hwnd, bool pinned);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_WINDOW_H
