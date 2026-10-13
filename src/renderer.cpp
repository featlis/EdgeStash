#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <algorithm>
#include "renderer.h"

using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

static ULONG_PTR g_gdiplusToken = 0;
static bool g_rendererInitialized = false;

// Static Fonts
static Font* g_pFontTitle = nullptr;
static Font* g_pFontCardTitle = nullptr;
static Font* g_pFontRegular = nullptr;
static Font* g_pFontSmall = nullptr;
static Font* g_pFontBadge = nullptr;
static Font* g_pFontIcon = nullptr;

// Static Brushes
static SolidBrush* g_pBrushBg = nullptr;
static SolidBrush* g_pBrushHeaderBg = nullptr;
static SolidBrush* g_pBrushDropBarBg = nullptr;
static SolidBrush* g_pBrushCard = nullptr;
static SolidBrush* g_pBrushCardHover = nullptr;
static SolidBrush* g_pBrushTextWhite = nullptr;
static SolidBrush* g_pBrushTextMuted = nullptr;
static SolidBrush* g_pBrushTextDim = nullptr;
static SolidBrush* g_pBrushAccent = nullptr;
static SolidBrush* g_pBrushAccentHover = nullptr;
static SolidBrush* g_pBrushTtlGreen = nullptr;
static SolidBrush* g_pBrushTtlYellow = nullptr;
static SolidBrush* g_pBrushTtlRed = nullptr;
static SolidBrush* g_pBrushTtlPinned = nullptr;
static SolidBrush* g_pBrushBtnHover = nullptr;
static SolidBrush* g_pBrushFileIconBg = nullptr;

// Static Pens
static Pen* g_pPenBorder = nullptr;
static Pen* g_pPenBorderHover = nullptr;
static Pen* g_pPenAccent = nullptr;
static Pen* g_pPenDivider = nullptr;
static Pen* g_pPenDotted = nullptr;

// Thumbnail LRU Cache (Max 10 in memory)
struct CachedThumb {
    int64_t itemId;
    std::wstring path;
    Bitmap* pBmp;
    DWORD lastAccess;
};
static std::vector<CachedThumb> g_thumbCache;
static const size_t MAX_CACHED_THUMBS = 10;

// Encoder CLSID helper
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (!pImageCodecInfo) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }

    free(pImageCodecInfo);
    return -1;
}

// Helper: Add rounded rectangle to path
static void AddRoundedRect(GraphicsPath& path, RectF rect, float radius) {
    float diameter = radius * 2.0f;
    if (diameter > rect.Width) diameter = rect.Width;
    if (diameter > rect.Height) diameter = rect.Height;

    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

bool Renderer_Init(void) {
    if (g_rendererInitialized) return true;

    GdiplusStartupInput gdiplusStartupInput;
    Status status = GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
    if (status != Ok) return false;

    // Create Fonts
    FontFamily fontFamily(L"Segoe UI");
    g_pFontTitle = new Font(&fontFamily, 12.0f, FontStyleBold, UnitPixel);
    g_pFontCardTitle = new Font(&fontFamily, 11.0f, FontStyleBold, UnitPixel);
    g_pFontRegular = new Font(&fontFamily, 10.0f, FontStyleRegular, UnitPixel);
    g_pFontSmall = new Font(&fontFamily, 9.0f, FontStyleRegular, UnitPixel);
    g_pFontBadge = new Font(&fontFamily, 9.0f, FontStyleBold, UnitPixel);
    g_pFontIcon = new Font(&fontFamily, 10.0f, FontStyleBold, UnitPixel);

    // Create Brushes
    g_pBrushBg = new SolidBrush(Color(240, 24, 24, 24));
    g_pBrushHeaderBg = new SolidBrush(Color(255, 30, 30, 30));
    g_pBrushDropBarBg = new SolidBrush(Color(255, 36, 36, 36));
    g_pBrushCard = new SolidBrush(Color(255, 40, 40, 40));
    g_pBrushCardHover = new SolidBrush(Color(255, 50, 50, 50));
    g_pBrushTextWhite = new SolidBrush(Color(255, 245, 245, 245));
    g_pBrushTextMuted = new SolidBrush(Color(255, 175, 175, 175));
    g_pBrushTextDim = new SolidBrush(Color(255, 120, 120, 120));
    g_pBrushAccent = new SolidBrush(Color(255, 0, 140, 255));
    g_pBrushAccentHover = new SolidBrush(Color(255, 40, 170, 255));
    g_pBrushTtlGreen = new SolidBrush(Color(255, 46, 185, 100));
    g_pBrushTtlYellow = new SolidBrush(Color(255, 240, 180, 40));
    g_pBrushTtlRed = new SolidBrush(Color(255, 235, 75, 60));
    g_pBrushTtlPinned = new SolidBrush(Color(255, 255, 195, 0));
    g_pBrushBtnHover = new SolidBrush(Color(70, 255, 255, 255));
    g_pBrushFileIconBg = new SolidBrush(Color(255, 52, 52, 52));

    // Create Pens
    g_pPenBorder = new Pen(Color(255, 58, 58, 58), 1.0f);
    g_pPenBorderHover = new Pen(Color(255, 95, 95, 95), 1.0f);
    g_pPenAccent = new Pen(Color(255, 0, 140, 255), 1.5f);
    g_pPenDivider = new Pen(Color(255, 48, 48, 48), 1.0f);
    g_pPenDotted = new Pen(Color(255, 75, 75, 75), 1.0f);
    g_pPenDotted->SetDashStyle(DashStyleDash);

    g_rendererInitialized = true;
    return true;
}

void Renderer_FlushThumbnails(void) {
    for (auto& item : g_thumbCache) {
        if (item.pBmp) {
            delete item.pBmp;
            item.pBmp = nullptr;
        }
    }
    g_thumbCache.clear();
}

void Renderer_Shutdown(void) {
    if (!g_rendererInitialized) return;

    Renderer_FlushThumbnails();

    delete g_pFontTitle;
    delete g_pFontCardTitle;
    delete g_pFontRegular;
    delete g_pFontSmall;
    delete g_pFontBadge;
    delete g_pFontIcon;

    delete g_pBrushBg;
    delete g_pBrushHeaderBg;
    delete g_pBrushDropBarBg;
    delete g_pBrushCard;
    delete g_pBrushCardHover;
    delete g_pBrushTextWhite;
    delete g_pBrushTextMuted;
    delete g_pBrushTextDim;
    delete g_pBrushAccent;
    delete g_pBrushAccentHover;
    delete g_pBrushTtlGreen;
    delete g_pBrushTtlYellow;
    delete g_pBrushTtlRed;
    delete g_pBrushTtlPinned;
    delete g_pBrushBtnHover;
    delete g_pBrushFileIconBg;

    delete g_pPenBorder;
    delete g_pPenBorderHover;
    delete g_pPenAccent;
    delete g_pPenDivider;
    delete g_pPenDotted;

    GdiplusShutdown(g_gdiplusToken);
    g_rendererInitialized = false;
}

static Bitmap* GetOrLoadThumbnail(int64_t itemId, const wchar_t* path) {
    if (!path || path[0] == L'\0') return nullptr;

    DWORD now = GetTickCount();

    // Check in cache
    for (size_t i = 0; i < g_thumbCache.size(); ++i) {
        if (g_thumbCache[i].itemId == itemId) {
            g_thumbCache[i].lastAccess = now;
            return g_thumbCache[i].pBmp;
        }
    }

    // Load from disk
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return nullptr;

    Bitmap* pLoaded = Bitmap::FromFile(path);
    if (!pLoaded || pLoaded->GetLastStatus() != Ok) {
        if (pLoaded) delete pLoaded;
        return nullptr;
    }

    // Evict oldest if full
    if (g_thumbCache.size() >= MAX_CACHED_THUMBS) {
        size_t oldestIdx = 0;
        DWORD oldestTime = g_thumbCache[0].lastAccess;
        for (size_t i = 1; i < g_thumbCache.size(); ++i) {
            if (g_thumbCache[i].lastAccess < oldestTime) {
                oldestTime = g_thumbCache[i].lastAccess;
                oldestIdx = i;
            }
        }
        delete g_thumbCache[oldestIdx].pBmp;
        g_thumbCache.erase(g_thumbCache.begin() + oldestIdx);
    }

    CachedThumb entry;
    entry.itemId = itemId;
    entry.path = path;
    entry.pBmp = pLoaded;
    entry.lastAccess = now;
    g_thumbCache.push_back(entry);

    return pLoaded;
}

// Generate 128x128 thumbnail
bool Renderer_GenerateThumbnail(const wchar_t* srcPath, const wchar_t* dstThumbPath, int maxDim) {
    if (!srcPath || !dstThumbPath) return false;

    Bitmap* src = Bitmap::FromFile(srcPath);
    if (!src || src->GetLastStatus() != Ok) {
        if (src) delete src;
        return false;
    }

    UINT w = src->GetWidth();
    UINT h = src->GetHeight();
    if (w == 0 || h == 0) {
        delete src;
        return false;
    }

    float scale = (float)maxDim / (float)max(w, h);
    if (scale > 1.0f) scale = 1.0f; // Don't upscale tiny icons

    int targetW = max(1, (int)(w * scale));
    int targetH = max(1, (int)(h * scale));

    Bitmap* thumb = new Bitmap(targetW, targetH, PixelFormat32bppARGB);
    Graphics g(thumb);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    g.DrawImage(src, 0, 0, targetW, targetH);
    delete src;

    CLSID pngClsid;
    if (GetEncoderClsid(L"image/png", &pngClsid) < 0) {
        delete thumb;
        return false;
    }

    Status s = thumb->Save(dstThumbPath, &pngClsid, NULL);
    delete thumb;
    return (s == Ok);
}

bool Renderer_SaveBitmapToPng(HBITMAP hBitmap, const wchar_t* dstPath) {
    if (!hBitmap || !dstPath) return false;

    Bitmap bmp(hBitmap, NULL);
    if (bmp.GetLastStatus() != Ok) return false;

    CLSID pngClsid;
    if (GetEncoderClsid(L"image/png", &pngClsid) < 0) return false;

    Status s = bmp.Save(dstPath, &pngClsid, NULL);
    return (s == Ok);
}

// Format human-friendly file size
static void FormatFileSize(int64_t bytes, wchar_t* out, size_t maxLen) {
    if (bytes < 1024) {
        swprintf_s(out, maxLen, L"%lld B", bytes);
    } else if (bytes < 1024 * 1024) {
        swprintf_s(out, maxLen, L"%.1f KB", (double)bytes / 1024.0);
    } else {
        swprintf_s(out, maxLen, L"%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
}

// Format TTL time remaining
static void FormatTtl(int64_t expiresAt, int isPinned, wchar_t* out, size_t maxLen, float* outRatio) {
    if (isPinned) {
        swprintf_s(out, maxLen, L"PINNED");
        if (outRatio) *outRatio = 1.0f;
        return;
    }

    int64_t now = (int64_t)time(NULL);
    int64_t diff = expiresAt - now;

    if (diff <= 0) {
        swprintf_s(out, maxLen, L"Expired");
        if (outRatio) *outRatio = 0.0f;
        return;
    }

    float ratio = (float)diff / 86400.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (outRatio) *outRatio = ratio;

    int hours = (int)(diff / 3600);
    int mins = (int)((diff % 3600) / 60);

    if (hours > 0) {
        swprintf_s(out, maxLen, L"%dh %dm left", hours, mins);
    } else {
        swprintf_s(out, maxLen, L"%dm left", max(1, mins));
    }
}

// Layout Constants
static const float HEADER_HEIGHT = 44.0f;
static const float DROP_BANNER_HEIGHT = 30.0f;
static const float CARD_HEIGHT = 80.0f;
static const float CARD_MARGIN_Y = 8.0f;
static const float CARD_TOTAL_HEIGHT = CARD_HEIGHT + CARD_MARGIN_Y;
static const float CARD_PADDING_X = 12.0f;

int Renderer_GetTotalContentHeight(int width, const UIState* state) {
    (void)width;
    if (!state) return 0;
    int topOffset = (int)(HEADER_HEIGHT + DROP_BANNER_HEIGHT + 10.0f);
    int cardsHeight = (int)(state->itemCount * CARD_TOTAL_HEIGHT);
    return topOffset + cardsHeight + 20;
}

// Hit Testing implementation
bool Renderer_HitTest(int width, int height, const UIState* state, int x, int y, HitTarget* outTarget) {
    if (!state || !outTarget) return false;
    memset(outTarget, 0, sizeof(HitTarget));
    outTarget->type = HIT_NONE;
    outTarget->item_index = -1;

    if (state->windowState == WND_STATE_COLLAPSED) {
        return false;
    }

    // 1. Header Buttons
    // Pin Dock button: Top Right (width - 76, 10, 24, 24)
    if (x >= width - 76 && x <= width - 52 && y >= 10 && y <= 34) {
        outTarget->type = HIT_HEADER_PIN_TOGGLE;
        SetRect(&outTarget->rect, width - 76, 10, width - 52, 34);
        return true;
    }
    // Clear All button: (width - 48, 10, 24, 24)
    if (x >= width - 48 && x <= width - 24 && y >= 10 && y <= 34) {
        outTarget->type = HIT_HEADER_CLEAR_EXPIRED;
        SetRect(&outTarget->rect, width - 48, 10, width - 24, 34);
        return true;
    }
    // Collapse button: (width - 24, 10, 20, 24)
    if (x >= width - 24 && x <= width - 4 && y >= 10 && y <= 34) {
        outTarget->type = HIT_HEADER_COLLAPSE;
        SetRect(&outTarget->rect, width - 24, 10, width - 4, 34);
        return true;
    }

    // 2. Cards
    float startY = HEADER_HEIGHT + DROP_BANNER_HEIGHT + 8.0f - (float)state->scrollY;
    float cardW = (float)width - (CARD_PADDING_X * 2.0f);

    for (int i = 0; i < state->itemCount; ++i) {
        float cy = startY + (float)i * CARD_TOTAL_HEIGHT;
        if (cy + CARD_HEIGHT < HEADER_HEIGHT) continue;
        if (cy > (float)height) break;

        if (x >= (int)CARD_PADDING_X && x <= (int)(CARD_PADDING_X + cardW) &&
            y >= (int)cy && y <= (int)(cy + CARD_HEIGHT)) {

            outTarget->item_index = i;
            outTarget->item_id = state->items[i].id;

            // Check Card Buttons on right side
            int btnRight = (int)(CARD_PADDING_X + cardW - 8.0f);
            int btnY = (int)(cy + 8.0f);

            // Delete button: [btnRight - 20, btnY, 20, 20]
            if (x >= btnRight - 20 && x <= btnRight && y >= btnY && y <= btnY + 20) {
                outTarget->type = HIT_CARD_DELETE;
                SetRect(&outTarget->rect, btnRight - 20, btnY, btnRight, btnY + 20);
                return true;
            }
            // Pin toggle button: [btnRight - 44, btnY, 20, 20]
            if (x >= btnRight - 44 && x <= btnRight - 24 && y >= btnY && y <= btnY + 20) {
                outTarget->type = HIT_CARD_PIN_TOGGLE;
                SetRect(&outTarget->rect, btnRight - 44, btnY, btnRight - 24, btnY + 20);
                return true;
            }
            // Copy button: [btnRight - 68, btnY, 20, 20]
            if (x >= btnRight - 68 && x <= btnRight - 48 && y >= btnY && y <= btnY + 20) {
                outTarget->type = HIT_CARD_COPY;
                SetRect(&outTarget->rect, btnRight - 68, btnY, btnRight - 48, btnY + 20);
                return true;
            }
            // Drag grip area: Left icon / body
            if (x >= (int)CARD_PADDING_X && x <= (int)(CARD_PADDING_X + 70.0f)) {
                outTarget->type = HIT_CARD_DRAG_GRIP;
                SetRect(&outTarget->rect, (int)CARD_PADDING_X, (int)cy, (int)(CARD_PADDING_X + cardW), (int)(cy + CARD_HEIGHT));
                return true;
            }

            outTarget->type = HIT_CARD_BODY;
            SetRect(&outTarget->rect, (int)CARD_PADDING_X, (int)cy, (int)(CARD_PADDING_X + cardW), (int)(cy + CARD_HEIGHT));
            return true;
        }
    }

    return false;
}

// Draw Collapsed State Handle (Slim 6px bar on edge)
static void DrawCollapsedHandle(Graphics& g, int width, int height, const UIState* state) {
    (void)state;
    // Dark base
    g.FillRectangle(g_pBrushBg, 0, 0, width, height);

    // Accent line in center
    float accentW = 3.0f;
    float accentX = ((float)width - accentW) * 0.5f;

    // Center pill indicator
    float pillH = 48.0f;
    float pillY = ((float)height - pillH) * 0.5f;

    GraphicsPath path;
    AddRoundedRect(path, RectF(accentX, pillY, accentW, pillH), 1.5f);
    g.FillPath(g_pBrushAccent, &path);
}

// Draw Header
static void DrawHeader(Graphics& g, int width, const UIState* state) {
    // Header background
    RectF headerRect(0.0f, 0.0f, (float)width, HEADER_HEIGHT);
    g.FillRectangle(g_pBrushHeaderBg, headerRect);
    g.DrawLine(g_pPenDivider, 0.0f, HEADER_HEIGHT, (float)width, HEADER_HEIGHT);

    // App Title
    StringFormat sfLeft;
    sfLeft.SetAlignment(StringAlignmentNear);
    sfLeft.SetLineAlignment(StringAlignmentCenter);
    RectF titleRect(14.0f, 0.0f, 130.0f, HEADER_HEIGHT);
    g.DrawString(APP_NAME, -1, g_pFontTitle, titleRect, &sfLeft, g_pBrushTextWhite);

    // Item count badge
    wchar_t badgeText[32];
    swprintf_s(badgeText, 32, L"%d items", state->itemCount);
    RectF badgeRect(110.0f, 12.0f, 64.0f, 20.0f);
    GraphicsPath badgePath;
    AddRoundedRect(badgePath, badgeRect, 4.0f);
    g.FillPath(g_pBrushDropBarBg, &badgePath);
    g.DrawPath(g_pPenBorder, &badgePath);

    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(badgeText, -1, g_pFontBadge, badgeRect, &sfCenter, g_pBrushTextMuted);

    // Header Action Buttons
    // 1. Pin Dock Button
    RectF pinBtnRect((float)width - 76.0f, 10.0f, 24.0f, 24.0f);
    if (state->hoveredAction == HIT_HEADER_PIN_TOGGLE) {
        GraphicsPath btnPath;
        AddRoundedRect(btnPath, pinBtnRect, 4.0f);
        g.FillPath(g_pBrushBtnHover, &btnPath);
    }
    const wchar_t* pinIcon = state->isPanelPinned ? L"[P*]" : L"[P]";
    g.DrawString(pinIcon, -1, g_pFontSmall, pinBtnRect, &sfCenter,
                 state->isPanelPinned ? g_pBrushTtlPinned : g_pBrushTextMuted);

    // 2. Clear Expired Button
    RectF clearBtnRect((float)width - 48.0f, 10.0f, 24.0f, 24.0f);
    if (state->hoveredAction == HIT_HEADER_CLEAR_EXPIRED) {
        GraphicsPath btnPath;
        AddRoundedRect(btnPath, clearBtnRect, 4.0f);
        g.FillPath(g_pBrushBtnHover, &btnPath);
    }
    g.DrawString(L"[C]", -1, g_pFontSmall, clearBtnRect, &sfCenter, g_pBrushTextMuted);

    // 3. Collapse Button
    RectF colBtnRect((float)width - 24.0f, 10.0f, 20.0f, 24.0f);
    if (state->hoveredAction == HIT_HEADER_COLLAPSE) {
        GraphicsPath btnPath;
        AddRoundedRect(btnPath, colBtnRect, 4.0f);
        g.FillPath(g_pBrushBtnHover, &btnPath);
    }
    g.DrawString(L">", -1, g_pFontSmall, colBtnRect, &sfCenter, g_pBrushTextMuted);
}

// Draw Drop Bar Banner
static void DrawDropBanner(Graphics& g, int width) {
    float y = HEADER_HEIGHT;
    RectF bannerRect(CARD_PADDING_X, y + 4.0f, (float)width - (CARD_PADDING_X * 2.0f), DROP_BANNER_HEIGHT - 6.0f);

    GraphicsPath path;
    AddRoundedRect(path, bannerRect, 6.0f);
    g.FillPath(g_pBrushDropBarBg, &path);
    g.DrawPath(g_pPenDotted, &path);

    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"+ Drop files or text to stash (24h)", -1, g_pFontSmall, bannerRect, &sf, g_pBrushTextMuted);
}

// Draw Single Card
static void DrawCard(Graphics& g, int width, const UIState* state, int index, float curY) {
    const StashItem* item = &state->items[index];
    float cardW = (float)width - (CARD_PADDING_X * 2.0f);
    RectF cardRect(CARD_PADDING_X, curY, cardW, CARD_HEIGHT);

    bool isHovered = (state->hoveredItemIndex == index);

    // Card background
    GraphicsPath cardPath;
    AddRoundedRect(cardPath, cardRect, 8.0f);
    g.FillPath(isHovered ? g_pBrushCardHover : g_pBrushCard, &cardPath);
    g.DrawPath(isHovered ? g_pPenBorderHover : g_pPenBorder, &cardPath);

    // Left Icon/Thumbnail area (56x56)
    float iconX = CARD_PADDING_X + 10.0f;
    float iconY = curY + 12.0f;
    RectF iconBox(iconX, iconY, 56.0f, 56.0f);

    if (item->type == ITEM_TYPE_IMAGE) {
        Bitmap* pThumb = nullptr;
        if (item->thumb_path[0] != L'\0') {
            pThumb = GetOrLoadThumbnail(item->id, item->thumb_path);
        } else if (item->file_path[0] != L'\0') {
            pThumb = GetOrLoadThumbnail(item->id, item->file_path);
        }

        if (pThumb) {
            float tw = (float)pThumb->GetWidth();
            float th = (float)pThumb->GetHeight();
            float s = min(56.0f / tw, 56.0f / th);
            float dw = tw * s;
            float dh = th * s;
            float dx = iconX + (56.0f - dw) * 0.5f;
            float dy = iconY + (56.0f - dh) * 0.5f;

            GraphicsPath imgClip;
            AddRoundedRect(imgClip, RectF(dx, dy, dw, dh), 4.0f);
            g.SetClip(&imgClip);
            g.DrawImage(pThumb, dx, dy, dw, dh);
            g.ResetClip();

            g.DrawPath(g_pPenBorder, &imgClip);
        } else {
            // Placeholder image icon
            GraphicsPath phPath;
            AddRoundedRect(phPath, iconBox, 6.0f);
            g.FillPath(g_pBrushFileIconBg, &phPath);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"IMG", -1, g_pFontBadge, iconBox, &sf, g_pBrushAccent);
        }
    } else if (item->type == ITEM_TYPE_FILE) {
        GraphicsPath fbPath;
        AddRoundedRect(fbPath, iconBox, 6.0f);
        g.FillPath(g_pBrushFileIconBg, &fbPath);
        g.DrawPath(g_pPenBorder, &fbPath);

        // Get extension
        const wchar_t* ext = PathFindExtensionW(item->title);
        wchar_t extUpper[16] = L"FILE";
        if (ext && ext[0] == L'.') {
            wcsncpy_s(extUpper, 16, ext + 1, _TRUNCATE);
            _wcsupr_s(extUpper, 16);
        }

        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(extUpper, -1, g_pFontBadge, iconBox, &sf, g_pBrushAccent);
    } else {
        // Text item
        GraphicsPath tbPath;
        AddRoundedRect(tbPath, iconBox, 6.0f);
        g.FillPath(g_pBrushFileIconBg, &tbPath);
        g.DrawPath(g_pPenBorder, &tbPath);

        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"TXT", -1, g_pFontBadge, iconBox, &sf, g_pBrushTtlGreen);
    }

    // Text info area (Center)
    float textX = iconX + 66.0f;
    float textW = cardW - 66.0f - 80.0f; // Leave space for buttons

    // Title (Single line with ellipsis)
    StringFormat sfTitle;
    sfTitle.SetTrimming(StringTrimmingEllipsisCharacter);
    sfTitle.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF titleRect(textX, curY + 12.0f, textW, 18.0f);
    g.DrawString(item->title, -1, g_pFontCardTitle, titleRect, &sfTitle, g_pBrushTextWhite);

    // Detail line: File size or content snippet
    wchar_t detailStr[128];
    if (item->type == ITEM_TYPE_TEXT) {
        swprintf_s(detailStr, 128, L"%zu chars", wcslen(item->content));
    } else {
        FormatFileSize(item->file_size, detailStr, 128);
    }
    RectF detailRect(textX, curY + 32.0f, textW, 16.0f);
    g.DrawString(detailStr, -1, g_pFontSmall, detailRect, &sfTitle, g_pBrushTextDim);

    // TTL progress bar and text
    wchar_t ttlStr[64];
    float ttlRatio = 1.0f;
    FormatTtl(item->expires_at, item->is_pinned, ttlStr, 64, &ttlRatio);

    SolidBrush* pTtlColor = g_pBrushTtlGreen;
    if (item->is_pinned) {
        pTtlColor = g_pBrushTtlPinned;
    } else if (ttlRatio < 0.15f) {
        pTtlColor = g_pBrushTtlRed;
    } else if (ttlRatio < 0.5f) {
        pTtlColor = g_pBrushTtlYellow;
    }

    RectF ttlTextRect(textX, curY + 50.0f, 90.0f, 16.0f);
    g.DrawString(ttlStr, -1, g_pFontSmall, ttlTextRect, &sfTitle, pTtlColor);

    // Progress bar line (if not pinned)
    if (!item->is_pinned) {
        float barX = textX + 85.0f;
        float barY = curY + 56.0f;
        float barW = 50.0f;
        float barH = 3.0f;

        g.FillRectangle(g_pBrushFileIconBg, barX, barY, barW, barH);
        g.FillRectangle(pTtlColor, barX, barY, barW * ttlRatio, barH);
    }

    // Action Buttons (Right side)
    float btnRight = CARD_PADDING_X + cardW - 8.0f;
    float btnY = curY + 8.0f;
    StringFormat sfBtn;
    sfBtn.SetAlignment(StringAlignmentCenter);
    sfBtn.SetLineAlignment(StringAlignmentCenter);

    // 1. Delete button
    RectF delBtnRect(btnRight - 20.0f, btnY, 20.0f, 20.0f);
    if (isHovered && state->hoveredAction == HIT_CARD_DELETE && state->hoveredItemId == item->id) {
        GraphicsPath bp;
        AddRoundedRect(bp, delBtnRect, 3.0f);
        g.FillPath(g_pBrushBtnHover, &bp);
    }
    g.DrawString(L"[x]", -1, g_pFontSmall, delBtnRect, &sfBtn, g_pBrushTextMuted);

    // 2. Pin button
    RectF pinBtnRect(btnRight - 44.0f, btnY, 20.0f, 20.0f);
    if (isHovered && state->hoveredAction == HIT_CARD_PIN_TOGGLE && state->hoveredItemId == item->id) {
        GraphicsPath bp;
        AddRoundedRect(bp, pinBtnRect, 3.0f);
        g.FillPath(g_pBrushBtnHover, &bp);
    }
    const wchar_t* cardPinIcon = item->is_pinned ? L"[P*]" : L"[P]";
    g.DrawString(cardPinIcon, -1, g_pFontSmall, pinBtnRect, &sfBtn,
                 item->is_pinned ? g_pBrushTtlPinned : g_pBrushTextMuted);

    // 3. Copy button
    RectF copyBtnRect(btnRight - 68.0f, btnY, 20.0f, 20.0f);
    if (isHovered && state->hoveredAction == HIT_CARD_COPY && state->hoveredItemId == item->id) {
        GraphicsPath bp;
        AddRoundedRect(bp, copyBtnRect, 3.0f);
        g.FillPath(g_pBrushBtnHover, &bp);
    }
    g.DrawString(L"[C]", -1, g_pFontSmall, copyBtnRect, &sfBtn, g_pBrushTextMuted);

    // 4. Drag grip dots indicator at bottom right
    RectF gripRect(btnRight - 24.0f, curY + 48.0f, 24.0f, 20.0f);
    g.DrawString(L":::", -1, g_pFontBadge, gripRect, &sfBtn, g_pBrushTextDim);
}

// Draw Empty State
static void DrawEmptyState(Graphics& g, int width, int height) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);

    float midY = (float)height * 0.45f;
    RectF r1(20.0f, midY, (float)width - 40.0f, 24.0f);
    RectF r2(20.0f, midY + 24.0f, (float)width - 40.0f, 20.0f);
    RectF r3(20.0f, midY + 46.0f, (float)width - 40.0f, 20.0f);

    g.DrawString(L"Dock is empty", -1, g_pFontCardTitle, r1, &sf, g_pBrushTextMuted);
    g.DrawString(L"Drag files or copy text here", -1, g_pFontSmall, r2, &sf, g_pBrushTextDim);
    g.DrawString(L"Items auto-expire in 24 hours", -1, g_pFontSmall, r3, &sf, g_pBrushTextDim);
}

void Renderer_Render(HDC hdcDest, int width, int height, const UIState* state) {
    if (!g_rendererInitialized || width <= 0 || height <= 0 || !state) return;

    // Create double-buffer bitmap
    Bitmap backBuffer(width, height, PixelFormat32bppPARGB);
    Graphics g(&backBuffer);

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    if (state->windowState == WND_STATE_COLLAPSED) {
        DrawCollapsedHandle(g, width, height, state);
    } else {
        // Expanded or animating
        // Dark translucent window background
        g.Clear(Color(0, 0, 0, 0)); // Transparent outside
        RectF winRect(0.0f, 0.0f, (float)width, (float)height);

        GraphicsPath bgPath;
        AddRoundedRect(bgPath, winRect, 10.0f);
        g.FillPath(g_pBrushBg, &bgPath);
        g.DrawPath(g_pPenBorder, &bgPath);

        // Header
        DrawHeader(g, width, state);

        // Drop zone banner
        DrawDropBanner(g, width);

        // Clip card scrolling area
        float cardsTop = HEADER_HEIGHT + DROP_BANNER_HEIGHT + 6.0f;
        RectF cardsArea(0.0f, cardsTop, (float)width, (float)height - cardsTop - 6.0f);
        g.SetClip(cardsArea);

        if (state->itemCount == 0) {
            DrawEmptyState(g, width, height);
        } else {
            // Virtual scrolling: calculate visible range
            float startY = cardsTop + 2.0f - (float)state->scrollY;

            for (int i = 0; i < state->itemCount; ++i) {
                float cy = startY + (float)i * CARD_TOTAL_HEIGHT;
                if (cy + CARD_HEIGHT < cardsTop) continue; // Above viewport
                if (cy > (float)height) break;             // Below viewport

                DrawCard(g, width, state, i, cy);
            }
        }

        g.ResetClip();

        // Optional scrollbar indicator on right edge
        int totalH = Renderer_GetTotalContentHeight(width, state);
        if (totalH > height) {
            float viewH = (float)height - cardsTop - 10.0f;
            float thumbH = max(24.0f, (viewH / (float)totalH) * viewH);
            float thumbY = cardsTop + 5.0f + ((float)state->scrollY / (float)(totalH - height)) * (viewH - thumbH);

            RectF scrollTrack((float)width - 6.0f, thumbY, 4.0f, thumbH);
            GraphicsPath stPath;
            AddRoundedRect(stPath, scrollTrack, 2.0f);
            g.FillPath(g_pBrushTextDim, &stPath);
        }
    }

    // Blit backbuffer to target HDC
    Graphics gDest(hdcDest);
    gDest.DrawImage(&backBuffer, 0, 0, width, height);
}
