#include "drop_source.h"

// -------------------------------------------------------------
// CDropSource Implementation
// -------------------------------------------------------------
typedef struct {
    IDropSourceVtbl* lpVtbl;
    LONG refCount;
} CDropSource;

static HRESULT STDMETHODCALLTYPE DropSource_QueryInterface(IDropSource* This, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropSource)) {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DropSource_AddRef(IDropSource* This) {
    CDropSource* pThis = (CDropSource*)This;
    return InterlockedIncrement(&pThis->refCount);
}

static ULONG STDMETHODCALLTYPE DropSource_Release(IDropSource* This) {
    CDropSource* pThis = (CDropSource*)This;
    LONG count = InterlockedDecrement(&pThis->refCount);
    if (count <= 0) {
        free(pThis);
        return 0;
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE DropSource_QueryContinueDrag(IDropSource* This, BOOL fEscapePressed, DWORD grfKeyState) {
    (void)This;
    if (fEscapePressed) {
        return DRAGDROP_S_CANCEL;
    }
    if (!(grfKeyState & MK_LBUTTON)) {
        return DRAGDROP_S_DROP;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DropSource_GiveFeedback(IDropSource* This, DWORD dwEffect) {
    (void)This;
    (void)dwEffect;
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

static IDropSourceVtbl g_dropSourceVtbl = {
    DropSource_QueryInterface,
    DropSource_AddRef,
    DropSource_Release,
    DropSource_QueryContinueDrag,
    DropSource_GiveFeedback
};

static IDropSource* CreateDropSource(void) {
    CDropSource* pThis = (CDropSource*)calloc(1, sizeof(CDropSource));
    if (!pThis) return NULL;
    pThis->lpVtbl = &g_dropSourceVtbl;
    pThis->refCount = 1;
    return (IDropSource*)pThis;
}

// -------------------------------------------------------------
// CEnumFORMATETC Implementation
// -------------------------------------------------------------
typedef struct {
    IEnumFORMATETCVtbl* lpVtbl;
    LONG refCount;
    FORMATETC* formats;
    ULONG count;
    ULONG index;
} CEnumFORMATETC;

static HRESULT STDMETHODCALLTYPE EnumFormatEtc_QueryInterface(IEnumFORMATETC* This, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumFORMATETC)) {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE EnumFormatEtc_AddRef(IEnumFORMATETC* This) {
    CEnumFORMATETC* pThis = (CEnumFORMATETC*)This;
    return InterlockedIncrement(&pThis->refCount);
}

static ULONG STDMETHODCALLTYPE EnumFormatEtc_Release(IEnumFORMATETC* This) {
    CEnumFORMATETC* pThis = (CEnumFORMATETC*)This;
    LONG count = InterlockedDecrement(&pThis->refCount);
    if (count <= 0) {
        if (pThis->formats) free(pThis->formats);
        free(pThis);
        return 0;
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE EnumFormatEtc_Next(IEnumFORMATETC* This, ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) {
    CEnumFORMATETC* pThis = (CEnumFORMATETC*)This;
    if (!rgelt) return E_POINTER;

    ULONG fetched = 0;
    while (pThis->index < pThis->count && fetched < celt) {
        rgelt[fetched] = pThis->formats[pThis->index];
        rgelt[fetched].ptd = NULL;
        pThis->index++;
        fetched++;
    }

    if (pceltFetched) *pceltFetched = fetched;
    return (fetched == celt) ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE EnumFormatEtc_Skip(IEnumFORMATETC* This, ULONG celt) {
    CEnumFORMATETC* pThis = (CEnumFORMATETC*)This;
    pThis->index += celt;
    if (pThis->index > pThis->count) {
        pThis->index = pThis->count;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE EnumFormatEtc_Reset(IEnumFORMATETC* This) {
    CEnumFORMATETC* pThis = (CEnumFORMATETC*)This;
    pThis->index = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE EnumFormatEtc_Clone(IEnumFORMATETC* This, IEnumFORMATETC** ppenum) {
    (void)This;
    (void)ppenum;
    return E_NOTIMPL;
}

static IEnumFORMATETCVtbl g_enumFormatEtcVtbl = {
    EnumFormatEtc_QueryInterface,
    EnumFormatEtc_AddRef,
    EnumFormatEtc_Release,
    EnumFormatEtc_Next,
    EnumFormatEtc_Skip,
    EnumFormatEtc_Reset,
    EnumFormatEtc_Clone
};

static IEnumFORMATETC* CreateEnumFormatEtc(const FORMATETC* pFormats, ULONG count) {
    CEnumFORMATETC* pThis = (CEnumFORMATETC*)calloc(1, sizeof(CEnumFORMATETC));
    if (!pThis) return NULL;
    pThis->lpVtbl = &g_enumFormatEtcVtbl;
    pThis->refCount = 1;
    pThis->count = count;
    pThis->index = 0;
    pThis->formats = (FORMATETC*)calloc(count, sizeof(FORMATETC));
    if (pThis->formats && count > 0) {
        memcpy(pThis->formats, pFormats, count * sizeof(FORMATETC));
    }
    return (IEnumFORMATETC*)pThis;
}

// -------------------------------------------------------------
// CDataObject Implementation
// -------------------------------------------------------------
typedef struct {
    IDataObjectVtbl* lpVtbl;
    LONG refCount;
    FORMATETC* formats;
    STGMEDIUM* mediums;
    ULONG formatCount;
} CDataObject;

static HRESULT STDMETHODCALLTYPE DataObject_QueryInterface(IDataObject* This, REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDataObject)) {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE DataObject_AddRef(IDataObject* This) {
    CDataObject* pThis = (CDataObject*)This;
    return InterlockedIncrement(&pThis->refCount);
}

static ULONG STDMETHODCALLTYPE DataObject_Release(IDataObject* This) {
    CDataObject* pThis = (CDataObject*)This;
    LONG count = InterlockedDecrement(&pThis->refCount);
    if (count <= 0) {
        for (ULONG i = 0; i < pThis->formatCount; ++i) {
            ReleaseStgMedium(&pThis->mediums[i]);
        }
        if (pThis->formats) free(pThis->formats);
        if (pThis->mediums) free(pThis->mediums);
        free(pThis);
        return 0;
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE DataObject_GetData(IDataObject* This, FORMATETC* pformatetcIn, STGMEDIUM* pmedium) {
    CDataObject* pThis = (CDataObject*)This;
    if (!pformatetcIn || !pmedium) return E_INVALIDARG;

    for (ULONG i = 0; i < pThis->formatCount; ++i) {
        if (pformatetcIn->cfFormat == pThis->formats[i].cfFormat &&
            (pformatetcIn->tymed & pThis->formats[i].tymed) &&
            pformatetcIn->dwAspect == pThis->formats[i].dwAspect) {

            pmedium->tymed = pThis->mediums[i].tymed;
            pmedium->pUnkForRelease = NULL;

            // Duplicate HGLOBAL
            SIZE_T size = GlobalSize(pThis->mediums[i].hGlobal);
            HGLOBAL hCopy = GlobalAlloc(GHND, size);
            if (!hCopy) return E_OUTOFMEMORY;

            void* src = GlobalLock(pThis->mediums[i].hGlobal);
            void* dst = GlobalLock(hCopy);
            if (src && dst) {
                memcpy(dst, src, size);
            }
            if (src) GlobalUnlock(pThis->mediums[i].hGlobal);
            if (dst) GlobalUnlock(hCopy);

            pmedium->hGlobal = hCopy;
            return S_OK;
        }
    }

    return DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE DataObject_GetDataHere(IDataObject* This, FORMATETC* pformatetc, STGMEDIUM* pmedium) {
    (void)This;
    (void)pformatetc;
    (void)pmedium;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataObject_QueryGetData(IDataObject* This, FORMATETC* pformatetc) {
    CDataObject* pThis = (CDataObject*)This;
    if (!pformatetc) return E_INVALIDARG;

    for (ULONG i = 0; i < pThis->formatCount; ++i) {
        if (pformatetc->cfFormat == pThis->formats[i].cfFormat &&
            (pformatetc->tymed & pThis->formats[i].tymed) &&
            pformatetc->dwAspect == pThis->formats[i].dwAspect) {
            return S_OK;
        }
    }
    return DV_E_FORMATETC;
}

static HRESULT STDMETHODCALLTYPE DataObject_GetCanonicalFormatEtc(IDataObject* This, FORMATETC* pformatectIn, FORMATETC* pformatetcOut) {
    (void)This;
    (void)pformatectIn;
    if (!pformatetcOut) return E_INVALIDARG;
    pformatetcOut->ptd = NULL;
    return DATA_S_SAMEFORMATETC;
}

static HRESULT STDMETHODCALLTYPE DataObject_SetData(IDataObject* This, FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) {
    (void)This;
    (void)pformatetc;
    (void)pmedium;
    (void)fRelease;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataObject_EnumFormatEtc(IDataObject* This, DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) {
    CDataObject* pThis = (CDataObject*)This;
    if (!ppenumFormatEtc) return E_INVALIDARG;

    if (dwDirection == DATADIR_GET) {
        *ppenumFormatEtc = CreateEnumFormatEtc(pThis->formats, pThis->formatCount);
        return (*ppenumFormatEtc) ? S_OK : E_OUTOFMEMORY;
    }
    *ppenumFormatEtc = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE DataObject_DAdvise(IDataObject* This, FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) {
    (void)This;
    (void)pformatetc;
    (void)advf;
    (void)pAdvSink;
    (void)pdwConnection;
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DataObject_DUnadvise(IDataObject* This, DWORD dwConnection) {
    (void)This;
    (void)dwConnection;
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE DataObject_EnumDAdvise(IDataObject* This, IEnumSTATDATA** ppenumAdvise) {
    (void)This;
    (void)ppenumAdvise;
    return OLE_E_ADVISENOTSUPPORTED;
}

static IDataObjectVtbl g_dataObjectVtbl = {
    DataObject_QueryInterface,
    DataObject_AddRef,
    DataObject_Release,
    DataObject_GetData,
    DataObject_GetDataHere,
    DataObject_QueryGetData,
    DataObject_GetCanonicalFormatEtc,
    DataObject_SetData,
    DataObject_EnumFormatEtc,
    DataObject_DAdvise,
    DataObject_DUnadvise,
    DataObject_EnumDAdvise
};

static IDataObject* CreateDataObjectForFile(const wchar_t* filePath) {
    if (!filePath || filePath[0] == L'\0') return NULL;

    CDataObject* pThis = (CDataObject*)calloc(1, sizeof(CDataObject));
    if (!pThis) return NULL;

    pThis->lpVtbl = &g_dataObjectVtbl;
    pThis->refCount = 1;
    pThis->formatCount = 1;
    pThis->formats = (FORMATETC*)calloc(1, sizeof(FORMATETC));
    pThis->mediums = (STGMEDIUM*)calloc(1, sizeof(STGMEDIUM));

    pThis->formats[0].cfFormat = CF_HDROP;
    pThis->formats[0].dwAspect = DVASPECT_CONTENT;
    pThis->formats[0].lindex = -1;
    pThis->formats[0].tymed = TYMED_HGLOBAL;

    // Allocate DROPFILES
    size_t pathLen = wcslen(filePath);
    size_t allocSize = sizeof(DROPFILES) + (pathLen + 2) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND, allocSize);
    if (!hGlobal) {
        free(pThis->formats);
        free(pThis->mediums);
        free(pThis);
        return NULL;
    }

    BYTE* pBytes = (BYTE*)GlobalLock(hGlobal);
    if (pBytes) {
        DROPFILES* df = (DROPFILES*)pBytes;
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        wchar_t* pDestStr = (wchar_t*)(pBytes + sizeof(DROPFILES));
        wcsncpy_s(pDestStr, pathLen + 1, filePath, _TRUNCATE);
        pDestStr[pathLen] = L'\0';
        pDestStr[pathLen + 1] = L'\0'; // Double null terminate
        GlobalUnlock(hGlobal);
    }

    pThis->mediums[0].tymed = TYMED_HGLOBAL;
    pThis->mediums[0].hGlobal = hGlobal;
    pThis->mediums[0].pUnkForRelease = NULL;

    return (IDataObject*)pThis;
}

static IDataObject* CreateDataObjectForText(const wchar_t* text) {
    if (!text || text[0] == L'\0') return NULL;

    CDataObject* pThis = (CDataObject*)calloc(1, sizeof(CDataObject));
    if (!pThis) return NULL;

    pThis->lpVtbl = &g_dataObjectVtbl;
    pThis->refCount = 1;
    pThis->formatCount = 1;
    pThis->formats = (FORMATETC*)calloc(1, sizeof(FORMATETC));
    pThis->mediums = (STGMEDIUM*)calloc(1, sizeof(STGMEDIUM));

    pThis->formats[0].cfFormat = CF_UNICODETEXT;
    pThis->formats[0].dwAspect = DVASPECT_CONTENT;
    pThis->formats[0].lindex = -1;
    pThis->formats[0].tymed = TYMED_HGLOBAL;

    size_t len = wcslen(text);
    size_t allocSize = (len + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND, allocSize);
    if (!hGlobal) {
        free(pThis->formats);
        free(pThis->mediums);
        free(pThis);
        return NULL;
    }

    wchar_t* pDest = (wchar_t*)GlobalLock(hGlobal);
    if (pDest) {
        memcpy(pDest, text, allocSize);
        GlobalUnlock(hGlobal);
    }

    pThis->mediums[0].tymed = TYMED_HGLOBAL;
    pThis->mediums[0].hGlobal = hGlobal;
    pThis->mediums[0].pUnkForRelease = NULL;

    return (IDataObject*)pThis;
}

DWORD DropSource_StartDrag(const StashItem* item) {
    if (!item) return DROPEFFECT_NONE;

    IDataObject* pDataObject = NULL;
    if (item->type == ITEM_TYPE_FILE || item->type == ITEM_TYPE_IMAGE) {
        pDataObject = CreateDataObjectForFile(item->file_path);
    } else if (item->type == ITEM_TYPE_TEXT) {
        pDataObject = CreateDataObjectForText(item->content);
    }

    if (!pDataObject) return DROPEFFECT_NONE;

    IDropSource* pDropSource = CreateDropSource();
    if (!pDropSource) {
        pDataObject->lpVtbl->Release(pDataObject);
        return DROPEFFECT_NONE;
    }

    DWORD dwEffect = DROPEFFECT_NONE;
    HRESULT hr = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY | DROPEFFECT_LINK, &dwEffect);
    (void)hr;

    pDropSource->lpVtbl->Release(pDropSource);
    pDataObject->lpVtbl->Release(pDataObject);

    return dwEffect;
}
