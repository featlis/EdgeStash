#include "drop_target.h"
#include "storage.h"
#include "renderer.h"

typedef struct {
    IDropTargetVtbl* lpVtbl;
    LONG refCount;
    HWND hwnd;
    bool canAccept;
} CDropTarget;

static bool IsImageFile(const wchar_t* path) {
    const wchar_t* ext = PathFindExtensionW(path);
    if (!ext) return false;
    if (_wcsicmp(ext, L".png") == 0 ||
        _wcsicmp(ext, L".jpg") == 0 ||
        _wcsicmp(ext, L".jpeg") == 0 ||
        _wcsicmp(ext, L".bmp") == 0 ||
        _wcsicmp(ext, L".gif") == 0 ||
        _wcsicmp(ext, L".ico") == 0 ||
        _wcsicmp(ext, L".webp") == 0) {
        return true;
    }
    return false;
}

static int64_t GetPathFileSize(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER size;
        size.HighPart = fad.nFileSizeHigh;
        size.LowPart = fad.nFileSizeLow;
        return size.QuadPart;
    }
    return 0;
}

// Process dropped HDROP (files)
static void ProcessDropFiles(HWND hwnd, HDROP hDrop) {
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    const wchar_t* cacheDir = Storage_GetCacheDir();
    const wchar_t* thumbsDir = Storage_GetThumbsDir();

    for (UINT i = 0; i < fileCount; ++i) {
        wchar_t srcPath[MAX_PATH];
        if (DragQueryFileW(hDrop, i, srcPath, MAX_PATH) == 0) continue;

        const wchar_t* fileName = PathFindFileNameW(srcPath);
        int64_t fileSize = GetPathFileSize(srcPath);

        int64_t now = (int64_t)time(NULL);
        unsigned int rnd = (unsigned int)(rand() & 0xFFFF);

        wchar_t destPath[MAX_PATH];
        swprintf_s(destPath, MAX_PATH, L"%s\\%lld_%04x_%s", cacheDir, now, rnd, fileName);

        // Copy file into cache
        if (!CopyFileW(srcPath, destPath, FALSE)) {
            // Fallback: use source directly if copy failed
            wcsncpy_s(destPath, MAX_PATH, srcPath, _TRUNCATE);
        }

        ItemType type = ITEM_TYPE_FILE;
        wchar_t thumbPath[MAX_PATH] = { 0 };

        if (IsImageFile(srcPath)) {
            type = ITEM_TYPE_IMAGE;
            swprintf_s(thumbPath, MAX_PATH, L"%s\\%lld_%04x_thumb.png", thumbsDir, now, rnd);
            if (!Renderer_GenerateThumbnail(destPath, thumbPath, 128)) {
                thumbPath[0] = L'\0';
            }
        }

        Storage_AddItem(type, fileName, L"", destPath, thumbPath, fileSize, 0);
    }

    PostMessage(hwnd, WM_APP_ITEMS_UPDATED, 0, 0);
}

// Process dropped Unicode Text
static void ProcessDropText(HWND hwnd, const wchar_t* text) {
    if (!text || text[0] == L'\0') return;

    wchar_t title[260] = { 0 };
    // Title is first line of text
    size_t i = 0;
    while (text[i] != L'\0' && text[i] != L'\r' && text[i] != L'\n' && i < 60) {
        title[i] = text[i];
        i++;
    }
    title[i] = L'\0';

    if (wcslen(title) == 0) {
        wcscpy_s(title, 260, L"Text Snippet");
    }

    size_t charCount = wcslen(text);
    Storage_AddItem(ITEM_TYPE_TEXT, title, text, L"", L"", (int64_t)charCount, 0);
    PostMessage(hwnd, WM_APP_ITEMS_UPDATED, 0, 0);
}

// Process dropped Bitmap / DIB
static void ProcessDropDib(HWND hwnd, HGLOBAL hDib) {
    if (!hDib) return;

    void* pDib = GlobalLock(hDib);
    if (!pDib) return;

    BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)pDib;
    int w = bih->biWidth;
    int h = abs(bih->biHeight);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    SetDIBits(hdcMem, hBmp, 0, h,
              (const BYTE*)pDib + bih->biSize,
              (BITMAPINFO*)bih, DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    GlobalUnlock(hDib);

    int64_t now = (int64_t)time(NULL);
    unsigned int rnd = (unsigned int)(rand() & 0xFFFF);
    const wchar_t* cacheDir = Storage_GetCacheDir();
    const wchar_t* thumbsDir = Storage_GetThumbsDir();

    wchar_t destPath[MAX_PATH];
    swprintf_s(destPath, MAX_PATH, L"%s\\%lld_%04x_image.png", cacheDir, now, rnd);

    if (Renderer_SaveBitmapToPng(hBmp, destPath)) {
        wchar_t thumbPath[MAX_PATH];
        swprintf_s(thumbPath, MAX_PATH, L"%s\\%lld_%04x_thumb.png", thumbsDir, now, rnd);
        if (!Renderer_GenerateThumbnail(destPath, thumbPath, 128)) {
            thumbPath[0] = L'\0';
        }

        int64_t fSize = GetPathFileSize(destPath);
        Storage_AddItem(ITEM_TYPE_IMAGE, L"Pasted Image", L"", destPath, thumbPath, fSize, 0);
        PostMessage(hwnd, WM_APP_ITEMS_UPDATED, 0, 0);
    }

    DeleteObject(hBmp);
}

// IDropTarget COM Methods
static HRESULT STDMETHODCALLTYPE CDropTarget_QueryInterface(IDropTarget* This, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget)) {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE CDropTarget_AddRef(IDropTarget* This) {
    CDropTarget* pThis = (CDropTarget*)This;
    return InterlockedIncrement(&pThis->refCount);
}

static ULONG STDMETHODCALLTYPE CDropTarget_Release(IDropTarget* This) {
    CDropTarget* pThis = (CDropTarget*)This;
    LONG count = InterlockedDecrement(&pThis->refCount);
    if (count <= 0) {
        free(pThis);
        return 0;
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE CDropTarget_DragEnter(IDropTarget* This, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    (void)pt;
    CDropTarget* pThis = (CDropTarget*)This;
    pThis->canAccept = false;

    if (!pDataObj || !pdwEffect) return E_INVALIDARG;

    FORMATETC fmtHdrop = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    FORMATETC fmtText = { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    FORMATETC fmtDib = { CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

    if (pDataObj->lpVtbl->QueryGetData(pDataObj, &fmtHdrop) == S_OK ||
        pDataObj->lpVtbl->QueryGetData(pDataObj, &fmtText) == S_OK ||
        pDataObj->lpVtbl->QueryGetData(pDataObj, &fmtDib) == S_OK) {
        pThis->canAccept = true;
        *pdwEffect = DROPEFFECT_COPY;

        // Auto-expand dock window when drag enters!
        SendMessage(pThis->hwnd, WM_APP_TOGGLE_EXPAND, 1, 0);
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE CDropTarget_DragOver(IDropTarget* This, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    (void)pt;
    CDropTarget* pThis = (CDropTarget*)This;
    if (!pdwEffect) return E_INVALIDARG;

    if (pThis->canAccept) {
        *pdwEffect = DROPEFFECT_COPY;
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE CDropTarget_DragLeave(IDropTarget* This) {
    CDropTarget* pThis = (CDropTarget*)This;
    pThis->canAccept = false;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE CDropTarget_Drop(IDropTarget* This, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    (void)pt;
    CDropTarget* pThis = (CDropTarget*)This;

    if (!pDataObj || !pdwEffect) return E_INVALIDARG;
    if (!pThis->canAccept) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    *pdwEffect = DROPEFFECT_COPY;

    // Check HDROP
    FORMATETC fmtHdrop = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM medium;
    if (pDataObj->lpVtbl->GetData(pDataObj, &fmtHdrop, &medium) == S_OK) {
        ProcessDropFiles(pThis->hwnd, (HDROP)medium.hGlobal);
        ReleaseStgMedium(&medium);
        return S_OK;
    }

    // Check UNICODETEXT
    FORMATETC fmtText = { CF_UNICODETEXT, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pDataObj->lpVtbl->GetData(pDataObj, &fmtText, &medium) == S_OK) {
        const wchar_t* pText = (const wchar_t*)GlobalLock(medium.hGlobal);
        if (pText) {
            ProcessDropText(pThis->hwnd, pText);
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        return S_OK;
    }

    // Check DIB
    FORMATETC fmtDib = { CF_DIB, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pDataObj->lpVtbl->GetData(pDataObj, &fmtDib, &medium) == S_OK) {
        ProcessDropDib(pThis->hwnd, medium.hGlobal);
        ReleaseStgMedium(&medium);
        return S_OK;
    }

    return S_OK;
}

static IDropTargetVtbl g_dropTargetVtbl = {
    CDropTarget_QueryInterface,
    CDropTarget_AddRef,
    CDropTarget_Release,
    CDropTarget_DragEnter,
    CDropTarget_DragOver,
    CDropTarget_DragLeave,
    CDropTarget_Drop
};

IDropTarget* DropTarget_Create(HWND hwnd) {
    CDropTarget* pThis = (CDropTarget*)calloc(1, sizeof(CDropTarget));
    if (!pThis) return NULL;

    pThis->lpVtbl = &g_dropTargetVtbl;
    pThis->refCount = 1;
    pThis->hwnd = hwnd;
    pThis->canAccept = false;

    return (IDropTarget*)pThis;
}
