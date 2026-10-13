#ifndef EDGESTASH_RENDERER_H
#define EDGESTASH_RENDERER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    WindowState windowState;
    float animProgress;         // 0.0f (collapsed) to 1.0f (expanded)
    bool isPanelPinned;         // Window stay-open pin
    int scrollY;                // Scroll offset in pixels
    int hoveredItemIndex;       // Card index under mouse, or -1
    HitActionType hoveredAction;
    int64_t hoveredItemId;
    POINT mousePos;             // Mouse position in client coords
    int dpi;
    StashItem* items;
    int itemCount;
} UIState;

// Initialize GDI+ and static brushes, pens, fonts
bool Renderer_Init(void);
void Renderer_Shutdown(void);

// Paint entire window onto target HDC (double buffered)
void Renderer_Render(HDC hdcDest, int width, int height, const UIState* state);

// Hit testing
bool Renderer_HitTest(int width, int height, const UIState* state, int x, int y, HitTarget* outTarget);

// Get total scrollable content height
int Renderer_GetTotalContentHeight(int width, const UIState* state);

// Thumbnail & Image helpers
bool Renderer_GenerateThumbnail(const wchar_t* srcPath, const wchar_t* dstThumbPath, int maxDim);
bool Renderer_SaveBitmapToPng(HBITMAP hBitmap, const wchar_t* dstPath);

// Cache management for memory trim
void Renderer_FlushThumbnails(void);

#ifdef __cplusplus
}
#endif

#endif // EDGESTASH_RENDERER_H
