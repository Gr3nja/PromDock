/*
 * photoframe.c - PromDock の「画像・GIF追加」 = デスクトップに置ける画像・GIF (promdock.c / launcher.c / setting.c / searchbar.c と同じ exe にビルド)
 *
 *     cl ... /Febuild\PromDock.exe promdock.c launcher.c setting.c searchbar.c photoframe.c /link /SUBSYSTEM:WINDOWS
 *
 * 機能:
 *   - 画像 (PNG / JPEG / BMP / TIFF / ICO) と GIF アニメーションを、大きさを決めてデスクトップに置ける
 *       大きさは「長い辺のピクセル数」で指定。縦横比は元の画像のまま (JPEG は撮影時の向きも反映)
 *   - GIF はコマごとの待ち時間どおりに再生 (ループ再生)
 *   - 検索バーと同じく、最前面ではなく最奥 (壁紙のすぐ上=デスクトップの上) に置く。他のウィンドウは写真の上に重なる
 *   - ドラッグで好きな場所に移動できる (アプリ設定でロックも可能)。位置は自動保存
 *   - 写真を右クリック: 大きさの変更 / ドラッグ移動の切り替え / 削除 / アプリ設定
 *   - 画像ファイルは読み込み時にメモリへ取り込むので、PromDock の動作中でも元ファイルを削除・移動できる
 *     (ただし、元ファイルを動かすと次回起動時には読み込めなくなる。画像のパスを保存しているため)
 *   - 設定は exe と同じフォルダの photoframe.ini に保存 (UTF-16。日本語のパスも保存できる)
 *       [PhotoFrame]
 *       count=1
 *       movable=1
 *       pf0_path=C:\Users\me\Pictures\cat.gif
 *       pf0_size=400
 *       pf0_x=800
 *       pf0_y=60
 *
 * 外部 (promdock.c / setting.c) から呼ぶ関数:
 *     PhotoFrame_Init(hInst) / PhotoFrame_Shutdown()                         ... 起動時/終了時に1回
 *     PhotoFrame_Add / _Remove / _Count / _Limit / _Name / _Info             ... 設定ウィンドウの一覧操作
 *     PhotoFrame_GetSize / _SetSize                                          ... 長い辺のピクセル数
 *     PhotoFrame_Movable / _SetMovable                                       ... ドラッグ移動の許可
 *     PhotoFrame_PickFile                                                    ... 「ファイルを開く」ダイアログ
 *   ※ Settings_Init() は PhotoFrame_Init() より先に走る (初回設定) ので、
 *      追加・削除などは初期化前に呼ばれても安全に動くようにしてある。
 */
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

/* setting.c */
void Settings_RefreshPhotoFrame(void);
void Settings_OpenPage(int page); /* 4: 画像・GIF追加のページ */

/* このファイルが公開する関数 */
void PhotoFrame_Init(HINSTANCE hInst);
void PhotoFrame_Shutdown(void);
int PhotoFrame_Count(void);
int PhotoFrame_Limit(void);
void PhotoFrame_Name(int i, wchar_t *out, int cch);
void PhotoFrame_Info(int i, wchar_t *out, int cch);
BOOL PhotoFrame_Add(const wchar_t *path, int size);
void PhotoFrame_Remove(int index);
int PhotoFrame_GetSize(int i);
void PhotoFrame_SetSize(int i, int size);
BOOL PhotoFrame_Movable(void);
void PhotoFrame_SetMovable(BOOL on);
BOOL PhotoFrame_PickFile(HWND owner, wchar_t *out, int cch);

#define PF_MAX 12
#define PF_SIZE_MIN 50
#define PF_SIZE_MAX 3000
#define PF_SIZE_DEF 400
#define PF_CACHE_MAX ((size_t)128 * 1024 * 1024) /* 描画済みコマのキャッシュ上限 (GIF 全体で) */
#define PF_MAXFILE (256u * 1024u * 1024u)        /* 読み込める画像ファイルの上限 */
#define TIMER_ANIM 1
#define TIMER_Z 2 /* 最奥の位置を保つための見張り */
#define POS_AUTO 0x7fffffff
#define MENU_MOVABLE 50
#define MENU_DELETE 51
#define MENU_SETTINGS 52
#define MENU_SIZE_BASE 100 /* 100 + 大きさの番号 */

static const int kMenuSizes[] = {150, 200, 300, 400, 600, 800, 1000};
#define N_MENU_SIZES ((int)(sizeof(kMenuSizes) / sizeof(kMenuSizes[0])))

/* ------------------------------------------------------------------ */
/* GDI+ (フラット API。C から使えるよう必要な宣言だけを自前で書く)          */
/* ------------------------------------------------------------------ */
typedef struct
{
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GpStartupInput;

typedef struct
{
    UINT id;
    ULONG length;
    WORD type;
    void *value;
} GpPropItem;

int WINAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *input, void *output);
void WINAPI GdiplusShutdown(ULONG_PTR token);
int WINAPI GdipLoadImageFromStream(IStream *stream, void **image);
int WINAPI GdipDisposeImage(void *image);
int WINAPI GdipGetImageWidth(void *image, UINT *width);
int WINAPI GdipGetImageHeight(void *image, UINT *height);
int WINAPI GdipImageGetFrameCount(void *image, const GUID *dimensionID, UINT *count);
int WINAPI GdipImageSelectActiveFrame(void *image, const GUID *dimensionID, UINT frameIndex);
int WINAPI GdipImageRotateFlip(void *image, int rotateFlipType);
int WINAPI GdipGetPropertyItemSize(void *image, UINT propId, UINT *size);
int WINAPI GdipGetPropertyItem(void *image, UINT propId, UINT propSize, GpPropItem *buffer);
int WINAPI GdipCreateBitmapFromScan0(INT width, INT height, INT stride, INT format, BYTE *scan0, void **bitmap);
int WINAPI GdipGetImageGraphicsContext(void *image, void **graphics);
int WINAPI GdipDeleteGraphics(void *graphics);
int WINAPI GdipGraphicsClear(void *graphics, UINT32 color);
int WINAPI GdipSetInterpolationMode(void *graphics, int mode);
int WINAPI GdipSetPixelOffsetMode(void *graphics, int mode);
int WINAPI GdipDrawImageRectI(void *graphics, void *image, INT x, INT y, INT w, INT h);

#define GP_PIXFMT_32PARGB 0x000E200B
#define GP_INTERP_HQ_BICUBIC 7
#define GP_PIXELOFFSET_HALF 4
#define GP_TAG_FRAMEDELAY 0x5100
#define GP_TAG_ORIENTATION 0x0112

/* FrameDimensionTime {6aedbd6d-3fb5-418a-83a6-7f45229dc872} */
static const GUID kDimTime = {0x6aedbd6d, 0x3fb5, 0x418a, {0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72}};

static ULONG_PTR g_gdipToken;
static BOOL g_gdipOn;

static BOOL GdipOn(void)
{
    if (g_gdipOn)
        return TRUE;
    GpStartupInput in;
    memset(&in, 0, sizeof(in));
    in.GdiplusVersion = 1;
    if (GdiplusStartup(&g_gdipToken, &in, NULL) != 0)
        return FALSE;
    g_gdipOn = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 状態                                                                  */
/* ------------------------------------------------------------------ */
typedef struct Frame
{
    wchar_t path[MAX_PATH];
    int size; /* 長い辺のピクセル数 */
    int x, y;
    int w0, h0; /* 元の画像の大きさ (向き補正後) */
    int w, h;   /* 表示の大きさ */
    BOOL missing;

    IStream *stm; /* 画像のデータ (GDI+ が使い続けるので保持) */
    void *img;
    UINT nframes, cur;
    int *delay; /* コマごとの待ち時間 (ms) */

    HWND hwnd;
    HDC dc;
    HBITMAP dib, old;
    uint32_t *bits;
    void *gbmp, *gfx;
    BOOL placing;

    uint32_t **cache; /* 描画済みコマ (GIF の再生を軽くする) */
    size_t cacheBytes;
} Frame;

static HINSTANCE g_hInst;
static BOOL g_ready, g_loaded, g_movable = TRUE;
static wchar_t g_ini[MAX_PATH];
static Frame *g_frames[PF_MAX];
static int g_nframes;
static size_t g_cacheUsed;

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ------------------------------------------------------------------ */
/* 保存 / 読み込み (photoframe.ini)                                       */
/* ------------------------------------------------------------------ */
static void SaveFrames(void)
{
    wchar_t k[48], v[32];
    _snwprintf(v, 32, L"%d", g_nframes);
    WritePrivateProfileStringW(L"PhotoFrame", L"count", v, g_ini);
    WritePrivateProfileStringW(L"PhotoFrame", L"movable", g_movable ? L"1" : L"0", g_ini);
    for (int i = 0; i < g_nframes; i++)
    {
        Frame *f = g_frames[i];
        _snwprintf(k, 48, L"pf%d_path", i);
        WritePrivateProfileStringW(L"PhotoFrame", k, f->path, g_ini);
        _snwprintf(k, 48, L"pf%d_size", i);
        _snwprintf(v, 32, L"%d", f->size);
        WritePrivateProfileStringW(L"PhotoFrame", k, v, g_ini);
        _snwprintf(k, 48, L"pf%d_x", i);
        _snwprintf(v, 32, L"%d", f->x);
        WritePrivateProfileStringW(L"PhotoFrame", k, v, g_ini);
        _snwprintf(k, 48, L"pf%d_y", i);
        _snwprintf(v, 32, L"%d", f->y);
        WritePrivateProfileStringW(L"PhotoFrame", k, v, g_ini);
    }
    for (int i = g_nframes; i < PF_MAX; i++)
    { /* 削除した分の古い項目を消す */
        const wchar_t *suf[] = {L"path", L"size", L"x", L"y"};
        for (int s = 0; s < 4; s++)
        {
            _snwprintf(k, 48, L"pf%d_%s", i, suf[s]);
            WritePrivateProfileStringW(L"PhotoFrame", k, NULL, g_ini);
        }
    }
}

static void EnsureLoaded(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    wchar_t *slash = wcsrchr(g_ini, L'\\');
    if (slash)
        wcscpy(slash + 1, L"photoframe.ini");
    else
        wcscpy(g_ini, L"photoframe.ini");

    /* ini が無いときは UTF-16 (BOM 付き) で作る。そうしないと日本語のパスが ANSI に化けて保存される */
    if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES)
    {
        HANDLE h = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            const WORD bom = 0xFEFF;
            DWORD wr = 0;
            WriteFile(h, &bom, sizeof(bom), &wr, NULL);
            CloseHandle(h);
        }
    }

    g_movable = GetPrivateProfileIntW(L"PhotoFrame", L"movable", 1, g_ini) != 0;
    int n = ClampI((int)GetPrivateProfileIntW(L"PhotoFrame", L"count", 0, g_ini), 0, PF_MAX);
    for (int i = 0; i < n; i++)
    {
        wchar_t k[48];
        Frame *f = (Frame *)calloc(1, sizeof(Frame));
        if (!f)
            break;
        _snwprintf(k, 48, L"pf%d_path", i);
        GetPrivateProfileStringW(L"PhotoFrame", k, L"", f->path, MAX_PATH, g_ini);
        if (!f->path[0])
        {
            free(f);
            continue;
        }
        _snwprintf(k, 48, L"pf%d_size", i);
        f->size = ClampI((int)GetPrivateProfileIntW(L"PhotoFrame", k, PF_SIZE_DEF, g_ini), PF_SIZE_MIN, PF_SIZE_MAX);
        _snwprintf(k, 48, L"pf%d_x", i);
        f->x = (int)GetPrivateProfileIntW(L"PhotoFrame", k, POS_AUTO, g_ini);
        _snwprintf(k, 48, L"pf%d_y", i);
        f->y = (int)GetPrivateProfileIntW(L"PhotoFrame", k, POS_AUTO, g_ini);
        g_frames[g_nframes++] = f;
    }
}

/* ------------------------------------------------------------------ */
/* 画像の読み込み                                                         */
/* ------------------------------------------------------------------ */
static void FrameCalcSize(Frame *f)
{
    int longSide = f->w0 > f->h0 ? f->w0 : f->h0;
    double s = (double)f->size / (double)longSide;
    f->w = (int)(f->w0 * s + 0.5);
    f->h = (int)(f->h0 * s + 0.5);
    if (f->w < 1)
        f->w = 1;
    if (f->h < 1)
        f->h = 1;
}

/* ファイルを丸ごと読み込み、GDI+ の画像にする。成功したら TRUE */
static BOOL FrameLoad(Frame *f)
{
    if (f->img)
        return TRUE;
    if (!GdipOn())
        return FALSE;

    HANDLE h = CreateFileW(f->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        f->missing = TRUE;
        return FALSE;
    }
    f->missing = FALSE;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (LONGLONG)PF_MAXFILE)
    {
        CloseHandle(h);
        return FALSE;
    }
    BYTE *buf = (BYTE *)malloc((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = buf && ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart;
    CloseHandle(h);
    if (!ok)
    {
        free(buf);
        return FALSE;
    }
    f->stm = SHCreateMemStream(buf, got);
    free(buf);
    if (!f->stm)
        return FALSE;

    void *img = NULL;
    if (GdipLoadImageFromStream(f->stm, &img) != 0 || !img)
    {
        IStream_Release(f->stm);
        f->stm = NULL;
        return FALSE;
    }
    f->img = img;

    /* コマ数と待ち時間 (GIF など) */
    UINT n = 0;
    if (GdipImageGetFrameCount(img, &kDimTime, &n) != 0 || n < 1)
        n = 1;
    f->nframes = n;
    f->cur = 0;
    if (n > 1)
    {
        f->delay = (int *)calloc(n, sizeof(int));
        if (!f->delay)
        {
            f->nframes = n = 1;
        }
        else
        {
            for (UINT i = 0; i < n; i++)
                f->delay[i] = 100;
            UINT psz = 0;
            if (GdipGetPropertyItemSize(img, GP_TAG_FRAMEDELAY, &psz) == 0 && psz >= sizeof(GpPropItem))
            {
                GpPropItem *pi = (GpPropItem *)malloc(psz);
                if (pi && GdipGetPropertyItem(img, GP_TAG_FRAMEDELAY, psz, pi) == 0 && pi->value)
                {
                    UINT cnt = pi->length / 4;
                    const LONG *d = (const LONG *)pi->value;
                    for (UINT i = 0; i < n && i < cnt; i++)
                    {
                        int ms = (int)d[i] * 10;           /* 1/100 秒単位 */
                        f->delay[i] = ms <= 10 ? 100 : ms; /* ブラウザと同じく、極端に短い値は 100ms 扱い */
                    }
                }
                free(pi);
            }
        }
    }
    else
    { /* 写真の向き (EXIF) を反映する */
        UINT psz = 0;
        if (GdipGetPropertyItemSize(img, GP_TAG_ORIENTATION, &psz) == 0 && psz >= sizeof(GpPropItem))
        {
            GpPropItem *pi = (GpPropItem *)malloc(psz);
            if (pi && GdipGetPropertyItem(img, GP_TAG_ORIENTATION, psz, pi) == 0 && pi->value && pi->length >= 2)
            {
                int o = *(const WORD *)pi->value, rf = 0;
                switch (o)
                {
                case 2:
                    rf = 4;
                    break; /* 左右反転 */
                case 3:
                    rf = 2;
                    break; /* 180度 */
                case 4:
                    rf = 6;
                    break; /* 上下反転 */
                case 5:
                    rf = 5;
                    break;
                case 6:
                    rf = 1;
                    break; /* 右に90度 */
                case 7:
                    rf = 7;
                    break;
                case 8:
                    rf = 3;
                    break; /* 左に90度 */
                }
                if (rf)
                    GdipImageRotateFlip(img, rf);
            }
            free(pi);
        }
    }

    UINT w = 0, hh = 0;
    GdipGetImageWidth(img, &w);
    GdipGetImageHeight(img, &hh);
    if (w == 0 || hh == 0 || w > 60000 || hh > 60000)
    {
        GdipDisposeImage(img);
        f->img = NULL;
        IStream_Release(f->stm);
        f->stm = NULL;
        free(f->delay);
        f->delay = NULL;
        return FALSE;
    }
    f->w0 = (int)w;
    f->h0 = (int)hh;
    FrameCalcSize(f);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 描画 (ウィンドウごとの premultiplied BGRA ビットマップへ GDI+ で描く)       */
/* ------------------------------------------------------------------ */
static void FrameFreeCache(Frame *f)
{
    if (f->cache)
    {
        for (UINT i = 0; i < f->nframes; i++)
        {
            free(f->cache[i]);
            f->cache[i] = NULL;
        }
    }
    g_cacheUsed -= f->cacheBytes;
    f->cacheBytes = 0;
}

static void FrameFreeBuffers(Frame *f)
{
    if (f->gfx)
        GdipDeleteGraphics(f->gfx);
    if (f->gbmp)
        GdipDisposeImage(f->gbmp);
    f->gfx = f->gbmp = NULL;
    if (f->dc)
    {
        if (f->dib)
        {
            SelectObject(f->dc, f->old);
            DeleteObject(f->dib);
        }
        DeleteDC(f->dc);
    }
    f->dc = NULL;
    f->dib = f->old = NULL;
    f->bits = NULL;
}

static BOOL FrameMakeBuffers(Frame *f)
{
    f->dc = CreateCompatibleDC(NULL);
    if (!f->dc)
        return FALSE;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = f->w;
    bi.bmiHeader.biHeight = -f->h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    f->dib = CreateDIBSection(f->dc, &bi, DIB_RGB_COLORS, (void **)&f->bits, NULL, 0);
    if (!f->dib || !f->bits)
    {
        FrameFreeBuffers(f);
        return FALSE;
    }
    f->old = (HBITMAP)SelectObject(f->dc, f->dib);
    /* GDI+ のビットマップを DIB のメモリそのものに被せる (premultiplied なのでそのまま UpdateLayeredWindow に渡せる) */
    if (GdipCreateBitmapFromScan0(f->w, f->h, f->w * 4, GP_PIXFMT_32PARGB, (BYTE *)f->bits, &f->gbmp) != 0 || !f->gbmp ||
        GdipGetImageGraphicsContext(f->gbmp, &f->gfx) != 0 || !f->gfx)
    {
        FrameFreeBuffers(f);
        return FALSE;
    }
    GdipSetInterpolationMode(f->gfx, GP_INTERP_HQ_BICUBIC);
    GdipSetPixelOffsetMode(f->gfx, GP_PIXELOFFSET_HALF);
    return TRUE;
}

static void FrameRender(Frame *f)
{
    if (!f->hwnd || !f->bits || !f->img)
        return;
    size_t bytes = (size_t)f->w * f->h * 4;
    UINT i = f->cur < f->nframes ? f->cur : 0;
    if (f->cache && f->cache[i])
        memcpy(f->bits, f->cache[i], bytes);
    else
    {
        if (f->nframes > 1)
            GdipImageSelectActiveFrame(f->img, &kDimTime, i);
        GdipGraphicsClear(f->gfx, 0);
        GdipDrawImageRectI(f->gfx, f->img, 0, 0, f->w, f->h);
        if (f->cache && g_cacheUsed + bytes <= PF_CACHE_MAX)
        {
            uint32_t *c = (uint32_t *)malloc(bytes);
            if (c)
            {
                memcpy(c, f->bits, bytes);
                f->cache[i] = c;
                f->cacheBytes += bytes;
                g_cacheUsed += bytes;
            }
        }
    }
    GdiFlush();
    POINT src = {0, 0};
    SIZE sz = {f->w, f->h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(f->hwnd, NULL, NULL, &sz, f->dc, &src, 0, &bf, ULW_ALPHA);
}

static void FrameSchedule(Frame *f)
{
    if (f->hwnd && f->nframes > 1 && f->delay)
        SetTimer(f->hwnd, TIMER_ANIM, (UINT)ClampI(f->delay[f->cur < f->nframes ? f->cur : 0], 20, 60000), NULL);
}

/* ------------------------------------------------------------------ */
/* 最奥への配置: デスクトップ本体 (Progman / WorkerW) の直上                 */
/* (検索バーと同じ方式。検索バーのウィンドウは重なり順の判定で飛ばす)          */
/* ------------------------------------------------------------------ */
static BOOL ClassIs(HWND w, const wchar_t *name)
{
    wchar_t c[32];
    return GetClassNameW(w, c, 32) && wcscmp(c, name) == 0;
}

static BOOL IsDesktopHost(HWND w) { return ClassIs(w, L"Progman") || ClassIs(w, L"WorkerW"); }

static BOOL IsOurs(HWND w)
{
    return ClassIs(w, L"PromPhotoFrameWnd") || ClassIs(w, L"PromSearchBarWnd") || ClassIs(w, L"PromClockWnd");
}

static BOOL AtDesktopLevel(HWND h)
{
    HWND w = GetWindow(h, GW_HWNDNEXT);
    while (w && IsOurs(w))
        w = GetWindow(w, GW_HWNDNEXT);
    return w && IsDesktopHost(w);
}

static void ZPlace(Frame *f)
{
    if (!f->hwnd || AtDesktopLevel(f->hwnd))
        return;
    HWND host = NULL;
    for (HWND w = GetTopWindow(NULL); w; w = GetWindow(w, GW_HWNDNEXT))
        if (IsDesktopHost(w))
        {
            host = w;
            break;
        }
    if (!host)
        return;
    /* 壁紙の下 (HWND_BOTTOM) だと見えなくなるので、デスクトップの1つ上に差し込む */
    HWND after = GetWindow(host, GW_HWNDPREV);
    if (!after || (GetWindowLongW(after, GWL_EXSTYLE) & WS_EX_TOPMOST))
        after = HWND_NOTOPMOST;
    f->placing = TRUE;
    SetWindowPos(f->hwnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    f->placing = FALSE;
}

/* ------------------------------------------------------------------ */
/* ウィンドウ                                                            */
/* ------------------------------------------------------------------ */
static Frame *FrameOf(HWND h) { return (Frame *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static int FrameIndex(Frame *f)
{
    for (int i = 0; i < g_nframes; i++)
        if (g_frames[i] == f)
            return i;
    return -1;
}

static void FrameDestroy(Frame *f);

static void ShowFrameMenu(Frame *f)
{
    HMENU menu = CreatePopupMenu();
    HMENU sizes = CreatePopupMenu();
    for (int i = 0; i < N_MENU_SIZES; i++)
    {
        wchar_t t[32];
        _snwprintf(t, 32, L"%d px", kMenuSizes[i]);
        t[31] = 0;
        AppendMenuW(sizes, MF_STRING | (kMenuSizes[i] == f->size ? MF_CHECKED : 0), (UINT_PTR)(MENU_SIZE_BASE + i), t);
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sizes, L"大きさ (長い辺)");
    AppendMenuW(menu, MF_STRING | (g_movable ? MF_CHECKED : 0), MENU_MOVABLE, L"ドラッグで移動できるようにする");
    AppendMenuW(menu, MF_STRING, MENU_DELETE, L"この写真を削除");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"アプリ設定…");
    POINT pt;
    GetCursorPos(&pt);
    HWND h = f->hwnd;
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
    DestroyMenu(menu); /* サブメニューも一緒に破棄される */
    PostMessageW(h, WM_NULL, 0, 0);
    if (cmd >= MENU_SIZE_BASE && cmd < MENU_SIZE_BASE + N_MENU_SIZES)
    {
        int i = FrameIndex(f);
        if (i >= 0)
        {
            PhotoFrame_SetSize(i, kMenuSizes[cmd - MENU_SIZE_BASE]);
            Settings_RefreshPhotoFrame();
        }
    }
    else if (cmd == MENU_MOVABLE)
    {
        PhotoFrame_SetMovable(!g_movable);
        Settings_RefreshPhotoFrame();
    }
    else if (cmd == MENU_DELETE)
    {
        int i = FrameIndex(f);
        if (i >= 0)
            PhotoFrame_Remove(i); /* f は破棄されるので、これ以降触らない */
    }
    else if (cmd == MENU_SETTINGS)
        Settings_OpenPage(4);
}

static LRESULT CALLBACK FrameProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE)
    {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    }
    Frame *f = FrameOf(h);
    if (!f)
        return DefWindowProcW(h, m, w, l);

    switch (m)
    {
    case WM_WINDOWPOSCHANGING:
        if (!f->placing) /* クリックで手前に出てこないよう、自分以外からの重なり順の変更は無効にする */
            ((WINDOWPOS *)l)->flags |= SWP_NOZORDER;
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NCHITTEST:
        if (g_movable)
            return HTCAPTION; /* どこを掴んでもドラッグで動かせる */
        return HTCLIENT;

    case WM_SETCURSOR:
        if (LOWORD(l) == HTCAPTION)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            return TRUE;
        }
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;

    case WM_EXITSIZEMOVE:
    { /* ドラッグ移動が終わったら位置を保存 */
        RECT rc;
        GetWindowRect(h, &rc);
        f->x = rc.left;
        f->y = rc.top;
        SaveFrames();
        return 0;
    }

    case WM_TIMER:
        if (w == TIMER_ANIM)
        {
            KillTimer(h, TIMER_ANIM);
            if (f->nframes > 0)
                f->cur = (f->cur + 1) % f->nframes;
            FrameRender(f);
            FrameSchedule(f);
        }
        else if (w == TIMER_Z)
            ZPlace(f);
        return 0;

    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP: /* 移動できる状態では全面が HTCAPTION なので、ここで右クリックを受ける */
        ShowFrameMenu(f);
        return 0;
    case WM_CONTEXTMENU:
        ShowFrameMenu(f);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCDESTROY:
        f->hwnd = NULL;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* 作成 / 破棄                                                           */
/* ------------------------------------------------------------------ */
static void ClampToWork(Frame *f)
{
    RECT r = {f->x, f->y, f->x + f->w, f->y + f->h};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    /* 写真が画面からはみ出しても構わないが、つかめる部分 (40px) は必ず画面内に残す */
    const int keep = 40;
    f->x = ClampI(f->x, mi.rcWork.left - f->w + keep, mi.rcWork.right - keep);
    f->y = ClampI(f->y, mi.rcWork.top - f->h + keep, mi.rcWork.bottom - keep);
}

static void AutoPos(Frame *f, int idx)
{
    POINT o = {0, 0};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    int cw = mi.rcWork.right - mi.rcWork.left, ch = mi.rcWork.bottom - mi.rcWork.top;
    f->x = mi.rcWork.left + (cw - f->w) / 2 + (idx % 6) * 28;
    f->y = mi.rcWork.top + (ch - f->h) / 2 + (idx % 6) * 28;
}

static void FrameCreate(Frame *f, int idx)
{
    if (f->hwnd)
        return;
    if (!FrameLoad(f))
        return;
    if (f->x == POS_AUTO || f->y == POS_AUTO)
        AutoPos(f, idx);
    ClampToWork(f);
    if (f->nframes > 1 && !f->cache)
        f->cache = (uint32_t **)calloc(f->nframes, sizeof(uint32_t *));
    f->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"PromPhotoFrameWnd", L"PromPhotoFrame", WS_POPUP, f->x,
                              f->y, f->w, f->h, NULL, NULL, g_hInst, f);
    if (!f->hwnd)
        return;
    if (!FrameMakeBuffers(f))
    {
        DestroyWindow(f->hwnd);
        return;
    }
    FrameRender(f);
    ZPlace(f);
    ShowWindow(f->hwnd, SW_SHOWNOACTIVATE);
    ZPlace(f);
    SetTimer(f->hwnd, TIMER_Z, 1000, NULL);
    FrameSchedule(f);
}

static void FrameDestroy(Frame *f)
{
    if (f->hwnd)
        DestroyWindow(f->hwnd);
    FrameFreeBuffers(f);
    FrameFreeCache(f);
    free(f->cache);
    free(f->delay);
    if (f->img)
        GdipDisposeImage(f->img);
    if (f->stm)
        IStream_Release(f->stm);
    free(f);
}

/* ------------------------------------------------------------------ */
/* 公開関数                                                              */
/* ------------------------------------------------------------------ */
int PhotoFrame_Count(void)
{
    EnsureLoaded();
    return g_nframes;
}

int PhotoFrame_Limit(void) { return PF_MAX; }

void PhotoFrame_Name(int i, wchar_t *out, int cch)
{
    EnsureLoaded();
    if (i < 0 || i >= g_nframes)
    {
        out[0] = 0;
        return;
    }
    wcsncpy(out, PathFindFileNameW(g_frames[i]->path), cch - 1);
    out[cch - 1] = 0;
}

void PhotoFrame_Info(int i, wchar_t *out, int cch)
{
    EnsureLoaded();
    if (i < 0 || i >= g_nframes)
    {
        out[0] = 0;
        return;
    }
    Frame *f = g_frames[i];
    if (f->img)
    {
        if (f->nframes > 1)
            _snwprintf(out, cch, L"GIF  %u コマ  ·  %d×%d px", f->nframes, f->w, f->h);
        else
            _snwprintf(out, cch, L"%d×%d px", f->w, f->h);
    }
    else if (f->missing)
        _snwprintf(out, cch, L"ファイルが見つかりません");
    else
        _snwprintf(out, cch, L"読み込めません");
    out[cch - 1] = 0;
}

BOOL PhotoFrame_Add(const wchar_t *path, int size)
{
    EnsureLoaded();
    if (!path || !path[0] || g_nframes >= PF_MAX)
        return FALSE;
    Frame *f = (Frame *)calloc(1, sizeof(Frame));
    if (!f)
        return FALSE;
    wcsncpy(f->path, path, MAX_PATH - 1);
    f->size = ClampI(size, PF_SIZE_MIN, PF_SIZE_MAX);
    f->x = f->y = POS_AUTO;
    if (!FrameLoad(f)) /* 画像として読めないものは追加しない */
    {
        FrameDestroy(f);
        return FALSE;
    }
    g_frames[g_nframes++] = f;
    if (g_ready)
        FrameCreate(f, g_nframes - 1);
    SaveFrames();
    return TRUE;
}

void PhotoFrame_Remove(int index)
{
    EnsureLoaded();
    if (index < 0 || index >= g_nframes)
        return;
    Frame *f = g_frames[index];
    for (int i = index; i < g_nframes - 1; i++)
        g_frames[i] = g_frames[i + 1];
    g_frames[--g_nframes] = NULL;
    FrameDestroy(f);
    SaveFrames();
    Settings_RefreshPhotoFrame();
}

int PhotoFrame_GetSize(int i)
{
    EnsureLoaded();
    return (i >= 0 && i < g_nframes) ? g_frames[i]->size : PF_SIZE_DEF;
}

void PhotoFrame_SetSize(int i, int size)
{
    EnsureLoaded();
    if (i < 0 || i >= g_nframes)
        return;
    Frame *f = g_frames[i];
    size = ClampI(size, PF_SIZE_MIN, PF_SIZE_MAX);
    if (size == f->size)
        return;
    f->size = size;
    if (f->img)
    {
        FrameFreeCache(f);
        FrameFreeBuffers(f);
        FrameCalcSize(f);
        if (f->hwnd)
        {
            SetWindowPos(f->hwnd, NULL, 0, 0, f->w, f->h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (FrameMakeBuffers(f))
                FrameRender(f);
        }
    }
    SaveFrames();
}

BOOL PhotoFrame_Movable(void)
{
    EnsureLoaded();
    return g_movable;
}

void PhotoFrame_SetMovable(BOOL on)
{
    EnsureLoaded();
    g_movable = on ? TRUE : FALSE;
    SaveFrames();
}

BOOL PhotoFrame_PickFile(HWND owner, wchar_t *out, int cch)
{
    static const wchar_t filter[] = L"画像・GIF (*.png; *.jpg; *.gif; *.bmp; *.tif; *.ico)\0"
                                    L"*.png;*.jpg;*.jpeg;*.jpe;*.jfif;*.gif;*.bmp;*.dib;*.tif;*.tiff;*.ico\0"
                                    L"すべてのファイル (*.*)\0*.*\0";
    OPENFILENAMEW o;
    memset(&o, 0, sizeof(o));
    out[0] = 0;
    o.lStructSize = sizeof(o);
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = out;
    o.nMaxFile = (DWORD)cch;
    o.lpstrTitle = L"デスクトップに置く画像・GIF を選択";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&o);
}

void PhotoFrame_Init(HINSTANCE hInst)
{
    g_hInst = hInst;
    EnsureLoaded();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FrameProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromPhotoFrameWnd";
    RegisterClassExW(&wc);

    g_ready = TRUE;
    for (int i = 0; i < g_nframes; i++)
        FrameCreate(g_frames[i], i);
    SaveFrames(); /* 自動配置した位置を記録 */
}

void PhotoFrame_Shutdown(void)
{
    g_ready = FALSE;
    for (int i = 0; i < g_nframes; i++)
    {
        FrameDestroy(g_frames[i]);
        g_frames[i] = NULL;
    }
    g_nframes = 0;
    if (g_gdipOn)
    {
        GdiplusShutdown(g_gdipToken);
        g_gdipOn = FALSE;
    }
}