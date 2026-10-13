#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <stdio.h>
#include <vector>

using namespace Gdiplus;

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

#pragma pack(push, 1)
struct ICONDIR {
    WORD idReserved;
    WORD idType;
    WORD idCount;
};

struct ICONDIRENTRY {
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    DWORD dwImageOffset;
};
#pragma pack(pop)

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* p = (ImageCodecInfo*)malloc(size);
    if (!p) return -1;
    GetImageEncoders(num, size, p);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(p[j].MimeType, format) == 0) {
            *pClsid = p[j].Clsid;
            free(p);
            return j;
        }
    }
    free(p);
    return -1;
}

static void AddRoundedRect(GraphicsPath& path, RectF rect, float radius) {
    float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

static std::vector<BYTE> RenderIconPng(int size, CLSID pngClsid) {
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    float s = (float)size;

    // Dark sleek container
    GraphicsPath box;
    AddRoundedRect(box, RectF(s * 0.08f, s * 0.08f, s * 0.84f, s * 0.84f), s * 0.22f);
    SolidBrush bgBrush(Color(255, 24, 24, 28));
    g.FillPath(&bgBrush, &box);

    Pen borderPen(Color(255, 50, 50, 60), s * 0.03f);
    g.DrawPath(&borderPen, &box);

    // Cyan glowing edge stripe on right
    GraphicsPath edgeStripe;
    AddRoundedRect(edgeStripe, RectF(s * 0.72f, s * 0.22f, s * 0.12f, s * 0.56f), s * 0.05f);
    SolidBrush accentBrush(Color(255, 0, 160, 255));
    g.FillPath(&accentBrush, &edgeStripe);

    // Stash card stack inside
    GraphicsPath card1;
    AddRoundedRect(card1, RectF(s * 0.20f, s * 0.24f, s * 0.44f, s * 0.30f), s * 0.06f);
    SolidBrush card1Brush(Color(255, 45, 48, 56));
    g.FillPath(&card1Brush, &card1);
    Pen cardPen(Color(255, 70, 75, 88), s * 0.02f);
    g.DrawPath(&cardPen, &card1);

    GraphicsPath card2;
    AddRoundedRect(card2, RectF(s * 0.20f, s * 0.46f, s * 0.44f, s * 0.30f), s * 0.06f);
    SolidBrush card2Brush(Color(255, 55, 60, 72));
    g.FillPath(&card2Brush, &card2);
    g.DrawPath(&cardPen, &card2);

    // Mini lightning / arrow spark
    SolidBrush sparkBrush(Color(255, 255, 200, 50));
    PointF sparkPts[3] = {
        PointF(s * 0.32f, s * 0.56f),
        PointF(s * 0.42f, s * 0.61f),
        PointF(s * 0.32f, s * 0.66f)
    };
    g.FillPolygon(&sparkBrush, sparkPts, 3);

    IStream* pStream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    bmp.Save(pStream, &pngClsid, NULL);

    STATSTG stat;
    pStream->Stat(&stat, STATFLAG_NONAME);
    ULONG len = (ULONG)stat.cbSize.QuadPart;

    std::vector<BYTE> data(len);
    LARGE_INTEGER zero = { 0 };
    pStream->Seek(zero, STREAM_SEEK_SET, NULL);
    ULONG read = 0;
    pStream->Read(data.data(), len, &read);
    pStream->Release();

    return data;
}

int main() {
    CoInitialize(NULL);
    GdiplusStartupInput input;
    ULONG_PTR token;
    GdiplusStartup(&token, &input, NULL);

    CLSID pngClsid;
    GetEncoderClsid(L"image/png", &pngClsid);

    int sizes[] = { 256, 48, 32, 16 };
    int count = 4;
    std::vector<std::vector<BYTE>> pngs;
    for (int i = 0; i < count; ++i) {
        pngs.push_back(RenderIconPng(sizes[i], pngClsid));
    }

    FILE* f = NULL;
    _wfopen_s(&f, L"src\\app.ico", L"wb");
    if (!f) return 1;

    ICONDIR header;
    header.idReserved = 0;
    header.idType = 1;
    header.idCount = (WORD)count;
    fwrite(&header, sizeof(header), 1, f);

    DWORD offset = (DWORD)(sizeof(ICONDIR) + count * sizeof(ICONDIRENTRY));
    for (int i = 0; i < count; ++i) {
        ICONDIRENTRY entry;
        entry.bWidth = (sizes[i] >= 256) ? 0 : (BYTE)sizes[i];
        entry.bHeight = (sizes[i] >= 256) ? 0 : (BYTE)sizes[i];
        entry.bColorCount = 0;
        entry.bReserved = 0;
        entry.wPlanes = 1;
        entry.wBitCount = 32;
        entry.dwBytesInRes = (DWORD)pngs[i].size();
        entry.dwImageOffset = offset;
        fwrite(&entry, sizeof(entry), 1, f);
        offset += (DWORD)pngs[i].size();
    }

    for (int i = 0; i < count; ++i) {
        fwrite(pngs[i].data(), 1, pngs[i].size(), f);
    }

    fclose(f);
    printf("Created src\\app.ico successfully!\n");

    GdiplusShutdown(token);
    CoUninitialize();
    return 0;
}
