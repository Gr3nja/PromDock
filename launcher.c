/*
 * launcher.c - PromDock に組み込まれるランチャー (Launchpad風フルスクリーン)
 *
 * このファイルは promdock.c と一緒に1つの PromDock.exe にビルドされます:
 *     cl ... /Febuild\PromDock.exe promdock.c launcher.c /link /SUBSYSTEM:WINDOWS
 *
 * promdock.c から呼ぶ関数:
 *     Launcher_Init(hInst)  ... 起動時に1回 (ウィンドウ作成・F11登録・設定読み込み)
 *     Launcher_Toggle()     ... 開閉 (ドックの右クリックメニューから)
 *     Launcher_Shutdown()   ... 終了時に1回
 *
 * 機能:
 *   - F11 (グローバルホットキー) でランチャーを開閉
 *   - 画面全体を覆う半透明の第二のデスクトップ。小さめのアイコンをページ単位のグリッドで表示
 *   - 最初は空。右クリックで「新しいファイルを作成」「新しいフォルダーを作成」
 *     (exe横の LauncherItems フォルダに実際に作成し、その場でランチャーに追加。名前変更も可能)
 *     既存のものは「ショートカット/ファイル/フォルダを追加」で参照して追加
 *   - クリックで起動 (起動の遅いアプリでも固まらないよう別スレッドで起動)
 *   - 入力するだけで絞り込み検索 / Enter で先頭を起動
 *   - ←→↑↓ で選択、PageUp/PageDown・マウスホイール・横スワイプでページ切替
 *   - アイコンをドラッグして並べ替え (画面の左右端でページ送り)
 *   - 右クリック: 開く / 管理者として実行 / ファイルの場所 / 削除 / 追加 / 並べ替え
 *   - Esc・何もない所をクリック・F11・他のウィンドウをアクティブにすると閉じる
 *   - 設定は exe と同じフォルダの launcher.txt (UTF-8)
 *       @hotkey=122   ホットキーの仮想キーコード (122=F11, 118=F7 など)
 *       @cols=0 @rows=0  列数/行数 (0=自動)
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
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <ole2.h>
#include <dwmapi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

/* ---- 調整用パラメータ (96DPI基準のピクセル) ---- */
#define BASE_ICON 64.0f /* アイコンの基準サイズ (小さめ) */
#define MAX_ITEMS 1000
#define ICON_PX 128 /* 保持するアイコンの解像度 */
#define TIMER_ANIM 1
#define WM_LAUNCHFAIL (WM_APP + 1)
#define WM_ICONDONE (WM_APP + 2)
#define HOTKEY_ID 1
#define PULSE_MS 240

static const GUID MY_IID_IShellItem =
    {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
static const GUID MY_IID_IShellItemImageFactory =
    {0xbcc18b79, 0xba16, 0x442f, {0x80, 0xc4, 0x8a, 0x59, 0xc3, 0x0c, 0x46, 0x3b}};

typedef struct
{
    int id;
    wchar_t spec[MAX_PATH * 2];
    wchar_t name[128];
    uint32_t *px; /* ICON_PX x ICON_PX premultiplied BGRA (未取得は NULL) */
    int tries;
    uint8_t *lbl; /* ラベルのアルファマスク (遅延生成) */
    int lblW, lblH;
} Item;

static Item g_items[MAX_ITEMS];
static int g_count, g_nextId = 1;
static int g_view[MAX_ITEMS], g_vn; /* 検索で絞り込んだ表示順 (g_items のインデックス) */

static HWND g_hwnd;
static HDC g_memdc;
static HBITMAP g_dib, g_oldBmp;
static uint32_t *g_bits;
static int g_W, g_H, g_mx0, g_my0;
static float g_dpi = 1.0f;
static wchar_t g_cfgPath[MAX_PATH];
static int g_hotkey = VK_F11, g_cfgCols = 0, g_cfgRows = 0;

/* 表示状態 */
static BOOL g_visible, g_animOn, g_modal, g_dirty;
static float g_fade, g_fadeTarget;
static DWORD g_showTick;
static wchar_t g_search[64];
static int g_sel = -1;
static float g_pageF;
static int g_pageTarget, g_pages = 1;
static int g_launchId = -1;
static DWORD g_launchTick;

/* レイアウト */
static int g_cols, g_rows, g_per;
static float g_gridL, g_gridT, g_cellW, g_cellH, g_isz;
static float g_sbL, g_sbT, g_sbW, g_sbH;

/* マウス */
static float g_mx, g_my;
static POINT g_downPt;
static int g_pressed = -1;
static int g_mode; /* 0:なし 1:スワイプ 2:並べ替え */
static float g_swipeF0;
static int g_swipePage0;
static int g_dragIdx = -1;
static DWORD g_flipTick, g_wheelTick;

/* 検索欄のテキストキャッシュ */
static wchar_t g_sText[64];
static uint8_t *g_sMask;
static int g_sW, g_sH;

/* ------------------------------------------------------------------ */
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static BOOL EndsWithI(const wchar_t *s, const wchar_t *suffix)
{
    size_t a = wcslen(s), b = wcslen(suffix);
    return a >= b && _wcsicmp(s + a - b, suffix) == 0;
}

static BOOL ContainsI(const wchar_t *hay, const wchar_t *needle)
{
    size_t n = wcslen(needle);
    if (n == 0)
        return TRUE;
    for (; *hay; hay++)
    {
        size_t i = 0;
        while (i < n && hay[i] && towlower(hay[i]) == towlower(needle[i]))
            i++;
        if (i == n)
            return TRUE;
    }
    return FALSE;
}

static void ResolveSpec(const wchar_t *spec, wchar_t *out, size_t cch)
{
    wchar_t tmp[MAX_PATH * 2];
    ExpandEnvironmentStringsW(spec, tmp, (DWORD)(sizeof(tmp) / sizeof(tmp[0])));
    if (_wcsnicmp(tmp, L"shell:", 6) == 0)
    {
        wcsncpy(out, tmp, cch - 1);
        out[cch - 1] = 0;
    }
    else
        GetFullPathNameW(tmp, (DWORD)cch, out, NULL);
}

static void NameFromSpec(const wchar_t *spec, wchar_t *name, int cch)
{
    const wchar_t *b = wcsrchr(spec, L'\\');
    b = b ? b + 1 : spec;
    if (!*b)
        b = spec;
    wcsncpy(name, b, cch - 1);
    name[cch - 1] = 0;
    if (EndsWithI(name, L".lnk") || EndsWithI(name, L".exe"))
        name[wcslen(name) - 4] = 0;
}

/* ------------------------------------------------------------------ */
/* アイコン取得 (PromDock と同じ手順: ShellItemImageFactory -> HICON フォールバック) */
/* ------------------------------------------------------------------ */
static uint32_t *FetchViaFactory(const wchar_t *spec)
{
    wchar_t path[MAX_PATH * 2];
    ResolveSpec(spec, path, MAX_PATH * 2);

    IShellItem *si = NULL;
    if (FAILED(SHCreateItemFromParsingName(path, NULL, &MY_IID_IShellItem, (void **)&si)))
        return NULL;
    IShellItemImageFactory *fac = NULL;
    HRESULT hr = IShellItem_QueryInterface(si, &MY_IID_IShellItemImageFactory, (void **)&fac);
    IShellItem_Release(si);
    if (FAILED(hr) || !fac)
        return NULL;

    SIZE want = {256, 256};
    HBITMAP hbm = NULL;
    hr = IShellItemImageFactory_GetImage(fac, want, SIIGBF_ICONONLY, &hbm);
    IShellItemImageFactory_Release(fac);
    if (FAILED(hr) || !hbm)
        return NULL;

    BITMAP bm;
    GetObject(hbm, sizeof(bm), &bm);
    int bw = bm.bmWidth, bh = bm.bmHeight;
    uint32_t *raw = (uint32_t *)malloc((size_t)bw * bh * 4);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC hdc = GetDC(NULL);
    GetDIBits(hdc, hbm, 0, bh, raw, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);
    DeleteObject(hbm);

    BOOL anyAlpha = FALSE, needPremul = FALSE;
    for (int i = 0; i < bw * bh; i++)
    {
        uint32_t p = raw[i], a = p >> 24;
        if (a)
            anyAlpha = TRUE;
        if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a)
            needPremul = TRUE;
    }
    if (!anyAlpha)
    {
        BOOL anyRgb = FALSE;
        for (int i = 0; i < bw * bh; i++)
            if (raw[i] & 0x00FFFFFFu)
            {
                anyRgb = TRUE;
                break;
            }
        if (!anyRgb)
        { /* 完全な空白 -> 失敗扱い (あとで再取得) */
            free(raw);
            return NULL;
        }
        for (int i = 0; i < bw * bh; i++)
            raw[i] |= 0xFF000000u;
    }
    else if (needPremul)
        for (int i = 0; i < bw * bh; i++)
        {
            uint32_t p = raw[i], a = p >> 24;
            raw[i] = (a << 24) | ((((p >> 16) & 255) * a / 255) << 16) |
                     ((((p >> 8) & 255) * a / 255) << 8) | ((p & 255) * a / 255);
        }
    if (bw == 256 && bh == 256)
        return raw;

    uint32_t *out = (uint32_t *)malloc(256 * 256 * 4);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            out[y * 256 + x] = raw[(y * bh / 256) * bw + (x * bw / 256)];
    free(raw);
    return out;
}

/* HICON -> 256x256 premultiplied BGRA。黒地と白地に描いてアルファを復元する */
static uint32_t *HiconTo256(HICON hi)
{
    const int N = 256;
    uint32_t *blk = NULL, *wht = NULL;
    HDC scr = GetDC(NULL);
    HDC dc = CreateCompatibleDC(scr);
    ReleaseDC(NULL, scr);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = N;
    bi.bmiHeader.biHeight = -N;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP b1 = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&blk, NULL, 0);
    HBITMAP b2 = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&wht, NULL, 0);
    uint32_t *out = NULL;
    if (b1 && b2 && blk && wht)
    {
        HBITMAP old = (HBITMAP)SelectObject(dc, b1);
        memset(blk, 0, (size_t)N * N * 4);
        DrawIconEx(dc, 0, 0, hi, N, N, 0, NULL, DI_NORMAL);
        GdiFlush();
        SelectObject(dc, b2);
        memset(wht, 0xFF, (size_t)N * N * 4);
        DrawIconEx(dc, 0, 0, hi, N, N, 0, NULL, DI_NORMAL);
        GdiFlush();
        SelectObject(dc, old);

        out = (uint32_t *)malloc((size_t)N * N * 4);
        BOOL any = FALSE;
        for (int i = 0; i < N * N; i++)
        {
            int dg = (int)((wht[i] >> 8) & 255) - (int)((blk[i] >> 8) & 255);
            int a = 255 - dg;
            if (a < 0)
                a = 0;
            if (a > 255)
                a = 255;
            if (a == 0)
            {
                out[i] = 0;
                continue;
            }
            any = TRUE;
            uint32_t r = (blk[i] >> 16) & 255, g = (blk[i] >> 8) & 255, b = blk[i] & 255;
            if (r > (uint32_t)a)
                r = a;
            if (g > (uint32_t)a)
                g = a;
            if (b > (uint32_t)a)
                b = a;
            out[i] = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
        }
        if (!any)
        {
            free(out);
            out = NULL;
        }
    }
    if (b1)
        DeleteObject(b1);
    if (b2)
        DeleteObject(b2);
    DeleteDC(dc);
    return out;
}

static uint32_t *FetchViaHicon(const wchar_t *spec)
{
    wchar_t path[MAX_PATH * 2];
    ResolveSpec(spec, path, MAX_PATH * 2);
    HICON hi = NULL;
    uint32_t *px = NULL;
    UINT id = 0;
    if (PrivateExtractIconsW(path, 0, 256, 256, &hi, &id, 1, 0) >= 1 && hi)
    {
        px = HiconTo256(hi);
        DestroyIcon(hi);
        if (px)
            return px;
    }
    SHFILEINFOW sfi;
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
    {
        px = HiconTo256(sfi.hIcon);
        DestroyIcon(sfi.hIcon);
    }
    return px;
}

/* 256 -> ICON_PX(128) に縮小 (2x2平均)。src は解放する */
static uint32_t *Down128(uint32_t *src)
{
    uint32_t *dst = (uint32_t *)malloc((size_t)ICON_PX * ICON_PX * 4);
    for (int y = 0; y < ICON_PX; y++)
        for (int x = 0; x < ICON_PX; x++)
        {
            uint32_t p[4] = {src[(2 * y) * 256 + 2 * x], src[(2 * y) * 256 + 2 * x + 1],
                             src[(2 * y + 1) * 256 + 2 * x], src[(2 * y + 1) * 256 + 2 * x + 1]};
            uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8)
            {
                uint32_t s = ((p[0] >> sh) & 255) + ((p[1] >> sh) & 255) + ((p[2] >> sh) & 255) + ((p[3] >> sh) & 255);
                out |= ((s + 2) / 4) << sh;
            }
            dst[y * ICON_PX + x] = out;
        }
    free(src);
    return dst;
}

static uint32_t *FetchIcon(const wchar_t *spec)
{
    uint32_t *px = FetchViaFactory(spec);
    if (!px)
        px = FetchViaHicon(spec);
    return px ? Down128(px) : NULL;
}

/* ------------------------------------------------------------------ */
/* アイコンの非同期読み込み (数百個あっても UI を止めない)                  */
/* ------------------------------------------------------------------ */
typedef struct
{
    int id;
    wchar_t spec[MAX_PATH * 2];
} IconReq;
typedef struct
{
    int n;
    IconReq *r;
} IconJob;
typedef struct
{
    int id;
    uint32_t *px;
} IconRes;

static volatile LONG g_loading;
static volatile LONG g_quit;

static DWORD WINAPI IconThread(LPVOID p)
{
    IconJob *job = (IconJob *)p;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    for (int i = 0; i < job->n && !g_quit; i++)
    {
        uint32_t *px = FetchIcon(job->r[i].spec);
        if (!px)
            continue;
        IconRes *rs = (IconRes *)malloc(sizeof(IconRes));
        rs->id = job->r[i].id;
        rs->px = px;
        if (!PostMessageW(g_hwnd, WM_ICONDONE, 0, (LPARAM)rs))
        {
            free(px);
            free(rs);
        }
    }
    free(job->r);
    free(job);
    CoUninitialize();
    InterlockedExchange(&g_loading, 0);
    PostMessageW(g_hwnd, WM_ICONDONE, 0, 0);
    return 0;
}

static void StartIconLoad(void)
{
    if (g_loading)
        return;
    int n = 0;
    for (int i = 0; i < g_count; i++)
        if (!g_items[i].px && g_items[i].tries < 1)
            n++;
    if (n == 0)
        return;
    IconJob *job = (IconJob *)malloc(sizeof(IconJob));
    job->r = (IconReq *)malloc(sizeof(IconReq) * n);
    job->n = 0;
    for (int i = 0; i < g_count; i++)
        if (!g_items[i].px && g_items[i].tries < 1)
        {
            g_items[i].tries++;
            job->r[job->n].id = g_items[i].id;
            wcscpy(job->r[job->n].spec, g_items[i].spec);
            job->n++;
        }
    InterlockedExchange(&g_loading, 1);
    HANDLE th = CreateThread(NULL, 0, IconThread, job, 0, NULL);
    if (th)
        CloseHandle(th);
    else
    {
        free(job->r);
        free(job);
        InterlockedExchange(&g_loading, 0);
    }
}

/* ------------------------------------------------------------------ */
/* 項目の管理・設定ファイル                                                */
/* ------------------------------------------------------------------ */
static void RebuildView(void)
{
    g_vn = 0;
    for (int i = 0; i < g_count; i++)
        if (!g_search[0] || ContainsI(g_items[i].name, g_search))
            g_view[g_vn++] = i;
    int per = g_per > 0 ? g_per : 1;
    g_pages = g_vn > 0 ? (g_vn + per - 1) / per : 1;
    g_pageTarget = ClampI(g_pageTarget, 0, g_pages - 1);
    if (g_sel >= g_vn)
        g_sel = g_vn - 1;
}

static void FreeItemData(Item *it)
{
    free(it->px);
    free(it->lbl);
    it->px = NULL;
    it->lbl = NULL;
}

static BOOL AddItem(const wchar_t *specIn)
{
    if (g_count >= MAX_ITEMS || !specIn[0])
        return FALSE;
    wchar_t spec[MAX_PATH * 2];
    wcsncpy(spec, specIn, MAX_PATH * 2 - 1);
    spec[MAX_PATH * 2 - 1] = 0;
    for (int i = 0; i < g_count; i++)
        if (_wcsicmp(g_items[i].spec, spec) == 0)
            return FALSE;
    Item *it = &g_items[g_count];
    memset(it, 0, sizeof(*it));
    it->id = g_nextId++;
    wcscpy(it->spec, spec);
    NameFromSpec(spec, it->name, 128);
    g_count++;
    return TRUE;
}

static void RemoveItem(int idx)
{
    FreeItemData(&g_items[idx]);
    memmove(&g_items[idx], &g_items[idx + 1], (g_count - idx - 1) * sizeof(Item));
    g_count--;
}

static void MoveItem(int from, int to)
{
    if (from == to)
        return;
    Item t = g_items[from];
    if (from < to)
        memmove(&g_items[from], &g_items[from + 1], (to - from) * sizeof(Item));
    else
        memmove(&g_items[to + 1], &g_items[to], (from - to) * sizeof(Item));
    g_items[to] = t;
}

static int CmpItemName(const void *a, const void *b)
{
    return _wcsicmp(((const Item *)a)->name, ((const Item *)b)->name);
}

static void SaveConfig(void)
{
    FILE *f = _wfopen(g_cfgPath, L"wb");
    if (!f)
        return;
    fprintf(f, "@hotkey=%d\r\n@cols=%d\r\n@rows=%d\r\n", g_hotkey, g_cfgCols, g_cfgRows);
    for (int i = 0; i < g_count; i++)
    {
        char u8[MAX_PATH * 6];
        int n = WideCharToMultiByte(CP_UTF8, 0, g_items[i].spec, -1, u8, sizeof(u8), NULL, NULL);
        if (n > 1)
        {
            fwrite(u8, 1, n - 1, f);
            fwrite("\r\n", 1, 2, f);
        }
    }
    fclose(f);
}

static BOOL LoadConfig(void)
{
    FILE *f = _wfopen(g_cfgPath, L"rb");
    if (!f)
        return FALSE;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    len = (long)fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    char *p = buf;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    while (*p)
    {
        char *e = p;
        while (*e && *e != '\n' && *e != '\r')
            e++;
        char save = *e;
        *e = 0;
        if (*p == '@')
        {
            if (strncmp(p, "@hotkey=", 8) == 0)
                g_hotkey = ClampI(atoi(p + 8), 1, 255);
            else if (strncmp(p, "@cols=", 6) == 0)
                g_cfgCols = ClampI(atoi(p + 6), 0, 20);
            else if (strncmp(p, "@rows=", 6) == 0)
                g_cfgRows = ClampI(atoi(p + 6), 0, 20);
        }
        else if (*p && *p != '#')
        {
            wchar_t w[MAX_PATH * 2];
            if (MultiByteToWideChar(CP_UTF8, 0, p, -1, w, MAX_PATH * 2))
                AddItem(w);
        }
        *e = save;
        while (*e == '\n' || *e == '\r')
            e++;
        p = e;
    }
    free(buf);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 描画プリミティブ                                                       */
/* ------------------------------------------------------------------ */
static void BlendPx(int x, int y, float r, float g, float b, float a)
{
    if (x < 0 || y < 0 || x >= g_W || y >= g_H || a <= 0)
        return;
    uint32_t *dp = &g_bits[y * g_W + x];
    uint32_t d = *dp;
    float inv = 1.0f - a;
    float oa = a * 255 + ((d >> 24) & 255) * inv;
    float orr = r * a + ((d >> 16) & 255) * inv;
    float og = g * a + ((d >> 8) & 255) * inv;
    float ob = b * a + (d & 255) * inv;
    if (oa > 255)
        oa = 255;
    if (orr > 255)
        orr = 255;
    if (og > 255)
        og = 255;
    if (ob > 255)
        ob = 255;
    *dp = ((uint32_t)oa << 24) | ((uint32_t)orr << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
}

static void FillRRect(float L, float T, float R, float B, float rad, float cr, float cg, float cb, float ca)
{
    float cx = (L + R) * 0.5f, cy = (T + B) * 0.5f, hw = (R - L) * 0.5f, hh = (B - T) * 0.5f;
    int x0 = ClampI((int)L - 2, 0, g_W), x1 = ClampI((int)R + 3, 0, g_W);
    int y0 = ClampI((int)T - 2, 0, g_H), y1 = ClampI((int)B + 3, 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            float qx = fabsf(px) - hw + rad, qy = fabsf(py) - hh + rad;
            float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;
            float cov = Clamp01(0.5f - d);
            if (cov > 0)
                BlendPx(x, y, cr, cg, cb, cov * ca);
        }
}

static void DrawDot(float cx, float cy, float r, float alpha)
{
    int x0 = (int)(cx - r - 2), x1 = (int)(cx + r + 3), y0 = (int)(cy - r - 2), y1 = (int)(cy + r + 3);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = Clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
            BlendPx(x, y, 240.f, 240.f, 245.f, cov * alpha);
        }
}

static uint32_t Sample(const uint32_t *m, int ms, float u, float v)
{
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    int fx = (int)((u - x0) * 256.0f), fy = (int)((v - y0) * 256.0f);
    int x1 = ClampI(x0 + 1, 0, ms - 1), y1 = ClampI(y0 + 1, 0, ms - 1);
    x0 = ClampI(x0, 0, ms - 1);
    y0 = ClampI(y0, 0, ms - 1);
    uint32_t p00 = m[y0 * ms + x0], p10 = m[y0 * ms + x1], p01 = m[y1 * ms + x0], p11 = m[y1 * ms + x1];
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8)
    {
        uint32_t a = ((p00 >> sh) & 255) * (256 - fx) + ((p10 >> sh) & 255) * fx;
        uint32_t b = ((p01 >> sh) & 255) * (256 - fx) + ((p11 >> sh) & 255) * fx;
        out |= (((a * (256 - fy) + b * fy) >> 16) & 255) << sh;
    }
    return out;
}

static void DrawIconPx(const uint32_t *px, float cx, float cy, float size, float alpha)
{
    int dw = (int)(size + 0.5f);
    if (dw < 4)
        return;
    int dx0 = (int)floorf(cx - dw * 0.5f + 0.5f), dy0 = (int)floorf(cy - dw * 0.5f + 0.5f);
    float k = (float)ICON_PX / dw;
    uint32_t al = (uint32_t)(Clamp01(alpha) * 256.0f);
    for (int y = 0; y < dw; y++)
    {
        int py = dy0 + y;
        if (py < 0 || py >= g_H)
            continue;
        float v = (y + 0.5f) * k - 0.5f;
        for (int x = 0; x < dw; x++)
        {
            int pxx = dx0 + x;
            if (pxx < 0 || pxx >= g_W)
                continue;
            uint32_t s = Sample(px, ICON_PX, (x + 0.5f) * k - 0.5f, v);
            if (al < 256)
            {
                uint32_t t = 0;
                for (int sh = 0; sh < 32; sh += 8)
                    t |= ((((s >> sh) & 255) * al) >> 8) << sh;
                s = t;
            }
            uint32_t sa = s >> 24;
            if (!sa)
                continue;
            uint32_t *dp = &g_bits[py * g_W + pxx];
            uint32_t d = *dp, inv = 255 - sa, out = 0;
            for (int sh = 0; sh < 32; sh += 8)
            {
                uint32_t c = ((s >> sh) & 255) + (((d >> sh) & 255) * inv + 127) / 255;
                out |= (c > 255 ? 255 : c) << sh;
            }
            *dp = out;
        }
    }
}

/* GDIで白文字をマスクに描く。boxW 幅に収まらなければ「…」で省略 */
static uint8_t *MakeMask(const wchar_t *text, int fontPx, int boxW, UINT dtFlags, int *outH)
{
    HDC dc = CreateCompatibleDC(NULL);
    HFONT f = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(dc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int h = tm.tmHeight + 4;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = boxW;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP ob = (HBITMAP)SelectObject(dc, bm);
    memset(bits, 0, (size_t)boxW * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rc = {0, 0, boxW, h};
    DrawTextW(dc, text, -1, &rc, dtFlags | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER | DT_END_ELLIPSIS);
    GdiFlush();

    uint8_t *mask = (uint8_t *)malloc((size_t)boxW * h);
    for (int i = 0; i < boxW * h; i++)
        mask[i] = (uint8_t)((((uint32_t *)bits)[i] >> 8) & 255);
    *outH = h;

    SelectObject(dc, ob);
    DeleteObject(bm);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteDC(dc);
    return mask;
}

static void BlitMask(const uint8_t *mask, int mw, int mh, int x0, int y0, float r, float g, float b, float alpha)
{
    for (int y = 0; y < mh; y++)
    {
        int py = y0 + y;
        if (py < 0 || py >= g_H)
            continue;
        for (int x = 0; x < mw; x++)
        {
            uint8_t m = mask[y * mw + x];
            if (m)
                BlendPx(x0 + x, py, r, g, b, m / 255.0f * alpha);
        }
    }
}

/* ------------------------------------------------------------------ */
/* レイアウトと描画                                                       */
/* ------------------------------------------------------------------ */
static void CalcGeom(void)
{
    float d = g_dpi;
    g_cols = g_cfgCols > 0 ? g_cfgCols : 10;
    g_rows = g_cfgRows > 0 ? g_cfgRows : (g_H < (int)(720 * d) ? 5 : 6);
    g_per = g_cols * g_rows;
    float top = 100 * d, bot = 64 * d;
    float gridW = fminf(g_W * 0.84f, g_cols * 140 * d);
    g_gridL = (g_W - gridW) * 0.5f;
    g_gridT = top;
    g_cellW = gridW / g_cols;
    g_cellH = (g_H - top - bot) / g_rows;
    g_isz = fminf(fminf(g_cellW * 0.58f, g_cellH * 0.52f), BASE_ICON * 1.1f * d);
    g_sbW = fminf(420 * d, g_W * 0.6f);
    g_sbH = 38 * d;
    g_sbL = (g_W - g_sbW) * 0.5f;
    g_sbT = 36 * d;
}

/* ページ内 j 番目のセルの中心 (ox: ページ送りのオフセット) */
static void CellCenter(int j, float ox, float *cx, float *cy)
{
    int col = j % g_cols, row = j / g_cols;
    *cx = g_gridL + (col + 0.5f) * g_cellW + ox;
    *cy = g_gridT + row * g_cellH + g_cellH * 0.5f - 12 * g_dpi;
}

static int HitItem(float x, float y)
{
    int p0 = (int)floorf(g_pageF), p1 = p0 + 1;
    for (int p = p0; p <= p1; p++)
    {
        if (p < 0 || p >= g_pages)
            continue;
        float ox = (p - g_pageF) * g_W;
        for (int j = 0; j < g_per; j++)
        {
            int vi = p * g_per + j;
            if (vi >= g_vn)
                break;
            float cx, cy;
            CellCenter(j, ox, &cx, &cy);
            if (fabsf(x - cx) <= g_cellW * 0.5f && fabsf(y - (cy + 12 * g_dpi)) <= g_cellH * 0.5f)
                return vi;
        }
    }
    return -1;
}

static BOOL HitDot(float x, float y, int *page)
{
    if (g_pages <= 1)
        return FALSE;
    float sp = 22 * g_dpi, total = (g_pages - 1) * sp, cy = g_H - 40 * g_dpi;
    for (int i = 0; i < g_pages; i++)
    {
        float cx = g_W * 0.5f - total * 0.5f + i * sp;
        if (fabsf(x - cx) <= sp * 0.5f && fabsf(y - cy) <= 16 * g_dpi)
        {
            *page = i;
            return TRUE;
        }
    }
    return FALSE;
}

static void DrawItemAt(Item *it, float cx, float cy, float scale, float alpha, BOOL label)
{
    float sz = g_isz * scale;
    if (it->px)
        DrawIconPx(it->px, cx, cy, sz, alpha);
    else
        FillRRect(cx - sz * 0.5f, cy - sz * 0.5f, cx + sz * 0.5f, cy + sz * 0.5f, sz * 0.22f, 112, 116, 126, 0.55f * alpha);
    if (!label)
        return;
    int boxW = (int)(g_cellW * 0.92f);
    if (!it->lbl || it->lblW != boxW)
    {
        free(it->lbl);
        it->lblW = boxW;
        it->lbl = MakeMask(it->name, (int)(12 * g_dpi + 0.5f), boxW, DT_CENTER, &it->lblH);
    }
    int tx = (int)(cx - boxW * 0.5f), ty = (int)(cy + g_isz * 0.5f + 8 * g_dpi);
    BlitMask(it->lbl, it->lblW, it->lblH, tx, ty, 255, 255, 255, alpha);
}

static void Render(void)
{
    if (!g_bits)
        return;
    DWORD now = GetTickCount();
    float d = g_dpi;

    /* 背景: ほぼ黒の半透明 (premultiplied) */
    uint32_t bg = (204u << 24) | (14u << 16) | (14u << 8) | 19u;
    size_t total = (size_t)g_W * g_H;
    for (size_t i = 0; i < total; i++)
        g_bits[i] = bg;

    /* 検索欄 */
    FillRRect(g_sbL, g_sbT, g_sbL + g_sbW, g_sbT + g_sbH, g_sbH * 0.5f, 255, 255, 255, 0.14f);
    {
        const wchar_t *txt = g_search[0] ? g_search : L"検索";
        if (!g_sMask || wcscmp(txt, g_sText) != 0)
        {
            free(g_sMask);
            wcsncpy(g_sText, txt, 63);
            g_sText[63] = 0;
            g_sW = (int)(g_sbW - 36 * d);
            g_sMask = MakeMask(g_sText, (int)(17 * d + 0.5f), g_sW, DT_CENTER, &g_sH);
        }
        BlitMask(g_sMask, g_sW, g_sH, (int)(g_sbL + 18 * d), (int)(g_sbT + (g_sbH - g_sH) * 0.5f), 255, 255, 255,
                 g_search[0] ? 0.95f : 0.45f);
    }

    /* ページ */
    int p0 = (int)floorf(g_pageF);
    for (int p = p0; p <= p0 + 1; p++)
    {
        if (p < 0 || p >= g_pages)
            continue;
        float ox = (p - g_pageF) * g_W;
        for (int j = 0; j < g_per; j++)
        {
            int vi = p * g_per + j;
            if (vi >= g_vn)
                break;
            Item *it = &g_items[g_view[vi]];
            float cx, cy;
            CellCenter(j, ox, &cx, &cy);
            if (g_mode == 2 && vi == g_dragIdx)
                continue; /* 持ち上げ中は元の場所を空ける */
            if (vi == g_sel && g_mode != 2)
                FillRRect(cx - g_cellW * 0.46f, cy - g_isz * 0.5f - 12 * d, cx + g_cellW * 0.46f,
                          cy + g_isz * 0.5f + 34 * d, 16 * d, 255, 255, 255, 0.11f);
            float scale = 1.0f;
            if (it->id == g_launchId)
            {
                float t = (float)(now - g_launchTick) / PULSE_MS;
                if (t < 1.0f)
                    scale = 1.0f + 0.22f * sinf(t * 3.14159265f);
            }
            DrawItemAt(it, cx, cy, scale, 1.0f, TRUE);
        }
    }

    /* ページドット */
    if (g_pages > 1)
    {
        float sp = 22 * d, tot = (g_pages - 1) * sp, cy = g_H - 40 * d;
        for (int i = 0; i < g_pages; i++)
        {
            float a = 0.30f + 0.65f * Clamp01(1.0f - fabsf(i - g_pageF));
            DrawDot(g_W * 0.5f - tot * 0.5f + i * sp, cy, 4.0f * d, a);
        }
    }

    /* 並べ替え中のアイコンはカーソルに追従 */
    if (g_mode == 2 && g_dragIdx >= 0 && g_dragIdx < g_vn)
        DrawItemAt(&g_items[g_view[g_dragIdx]], g_mx, g_my, 1.12f, 0.88f, FALSE);

    POINT dst = {g_mx0, g_my0}, src = {0, 0};
    SIZE sz = {g_W, g_H};
    BYTE ca = (BYTE)(Clamp01(g_fade) * 255.0f + 0.5f);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, ca, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, NULL, &dst, &sz, g_memdc, &src, 0, &bf, ULW_ALPHA);
}

static void SetBuffers(int w, int h)
{
    if (g_bits && w == g_W && h == g_H)
        return;
    g_W = w;
    g_H = h;
    if (g_memdc)
    {
        SelectObject(g_memdc, g_oldBmp);
        DeleteObject(g_dib);
    }
    else
    {
        HDC scr = GetDC(NULL);
        g_memdc = CreateCompatibleDC(scr);
        ReleaseDC(NULL, scr);
    }
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_dib = CreateDIBSection(g_memdc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    g_oldBmp = (HBITMAP)SelectObject(g_memdc, g_dib);
    for (int i = 0; i < g_count; i++)
    { /* ラベルの幅が変わるので作り直す */
        free(g_items[i].lbl);
        g_items[i].lbl = NULL;
    }
}

static void EnsureTimer(void)
{
    if (!g_animOn)
    {
        SetTimer(g_hwnd, TIMER_ANIM, 16, NULL);
        g_animOn = TRUE;
    }
}

/* ------------------------------------------------------------------ */
/* 表示・非表示                                                          */
/* ------------------------------------------------------------------ */
static void ForceForeground(HWND h)
{
    HWND fg = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), ft = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    BOOL att = (ft && ft != me) ? AttachThreadInput(me, ft, TRUE) : FALSE;
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (att)
        AttachThreadInput(me, ft, FALSE);
}

static void SearchChanged(void)
{
    RebuildView();
    g_pageTarget = 0;
    g_sel = g_vn > 0 ? 0 : -1;
    g_dirty = TRUE;
    EnsureTimer();
}

static void ShowLauncher(void)
{
    if (g_visible)
    {
        g_fadeTarget = 1.0f;
        EnsureTimer();
        return;
    }
    POINT cp;
    GetCursorPos(&cp);
    HMONITOR hm = MonitorFromPoint(cp, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hm, &mi);
    int x = mi.rcMonitor.left, y = mi.rcMonitor.top;
    int w = mi.rcMonitor.right - x, h = mi.rcMonitor.bottom - y;

    SetBuffers(w, h);
    g_mx0 = x;
    g_my0 = y;
    CalcGeom();
    g_search[0] = 0;
    for (int i = 0; i < g_count; i++)
        if (!g_items[i].px)
            g_items[i].tries = 0; /* 開くたびに取得失敗分を再試行 */
    g_pageF = 0;
    g_pageTarget = 0;
    g_sel = -1;
    g_launchId = -1;
    g_mode = 0;
    g_dragIdx = -1;
    g_pressed = -1;
    RebuildView();
    g_fade = 0.0f;
    g_fadeTarget = 1.0f;
    g_visible = TRUE;
    g_showTick = GetTickCount();
    Render();
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ForceForeground(g_hwnd);
    SetFocus(g_hwnd);
    StartIconLoad();
    EnsureTimer();
}

static void HideLauncher(void)
{
    if (!g_visible)
        return;
    g_fadeTarget = 0.0f;
    ReleaseCapture();
    g_mode = 0;
    EnsureTimer();
}

static void ToggleLauncher(void)
{
    if (g_visible && g_fadeTarget > 0.5f)
        HideLauncher();
    else
        ShowLauncher();
}

static BOOL Step(void)
{
    BOOL busy = FALSE;
    DWORD now = GetTickCount();

    float fd = g_fadeTarget - g_fade;
    if (fabsf(fd) < 0.02f)
        g_fade = g_fadeTarget;
    else
    {
        g_fade += fd * 0.35f + (fd > 0 ? 0.02f : -0.02f);
        busy = TRUE;
    }
    if (g_fadeTarget <= 0.0f && g_fade <= 0.0f)
    { /* フェードアウト完了 -> 本当に隠す */
        g_visible = FALSE;
        g_launchId = -1;
        ShowWindow(g_hwnd, SW_HIDE);
        return FALSE;
    }

    if (g_mode != 1)
    {
        float pd = (float)g_pageTarget - g_pageF;
        if (fabsf(pd) < 0.003f)
            g_pageF = (float)g_pageTarget;
        else
        {
            g_pageF += pd * 0.22f;
            busy = TRUE;
        }
    }
    else
        busy = TRUE;

    if (g_launchId >= 0)
    {
        busy = TRUE;
        if (now - g_launchTick >= PULSE_MS)
        {
            g_launchId = -1;
            HideLauncher();
        }
    }

    if (g_mode == 2 && now - g_flipTick > 650)
    { /* 並べ替え中に左右の端へ寄せるとページ送り */
        float edge = 56 * g_dpi;
        if (g_mx < edge && g_pageTarget > 0)
        {
            g_pageTarget--;
            g_flipTick = now;
        }
        else if (g_mx > g_W - edge && g_pageTarget < g_pages - 1)
        {
            g_pageTarget++;
            g_flipTick = now;
        }
    }
    if (g_mode == 2)
        busy = TRUE;

    if (g_dirty)
    {
        g_dirty = FALSE;
        busy = TRUE;
    }
    return busy;
}

/* ------------------------------------------------------------------ */
/* 起動 (別スレッドで ShellExecuteEx。遅いアプリでも固まらない)             */
/* ------------------------------------------------------------------ */
typedef struct
{
    wchar_t spec[MAX_PATH * 2];
    wchar_t path[MAX_PATH * 2];
    wchar_t verb[16];
} LaunchReq;

static DWORD WINAPI LaunchThread(LPVOID p)
{
    LaunchReq *rq = (LaunchReq *)p;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.lpVerb = rq->verb;
    sei.lpFile = rq->path;
    sei.nShow = SW_SHOWNORMAL;
    BOOL ok = ShellExecuteExW(&sei);
    CoUninitialize();
    if (ok)
        free(rq);
    else if (!PostMessageW(g_hwnd, WM_LAUNCHFAIL, 0, (LPARAM)rq))
        free(rq);
    return 0;
}

static void LaunchSpec(const wchar_t *spec, const wchar_t *verb)
{
    LaunchReq *rq = (LaunchReq *)malloc(sizeof(LaunchReq));
    if (!rq)
        return;
    wcscpy(rq->spec, spec);
    ResolveSpec(spec, rq->path, MAX_PATH * 2);
    wcsncpy(rq->verb, verb, 15);
    rq->verb[15] = 0;
    HANDLE th = CreateThread(NULL, 0, LaunchThread, rq, 0, NULL);
    if (!th)
    {
        free(rq);
        MessageBeep(MB_ICONERROR);
        return;
    }
    CloseHandle(th);
}

static void LaunchView(int vi, const wchar_t *verb)
{
    if (vi < 0 || vi >= g_vn || g_launchId >= 0)
        return;
    Item *it = &g_items[g_view[vi]];
    LaunchSpec(it->spec, verb);
    g_launchId = it->id; /* 押した瞬間にポンと弾ませて、そのまま閉じる */
    g_launchTick = GetTickCount();
    EnsureTimer();
}

/* ------------------------------------------------------------------ */
/* 追加ダイアログ・メニュー                                                */
/* ------------------------------------------------------------------ */
static void AfterListChanged(void)
{
    SaveConfig();
    RebuildView();
    for (int i = 0; i < g_count; i++)
        if (!g_items[i].px)
            g_items[i].tries = 0;
    StartIconLoad();
    g_dirty = TRUE;
    EnsureTimer();
}

/* OFN_ALLOWMULTISELECT の結果 (単一: フルパス / 複数: フォルダ\0ファイル\0...\0\0) を追加 */
static void AddPicked(const wchar_t *buf)
{
    int added = 0;
    size_t n = wcslen(buf);
    if (buf[n + 1] == 0)
        added += AddItem(buf);
    else
        for (const wchar_t *p = buf + n + 1; *p; p += wcslen(p) + 1)
        {
            wchar_t full[MAX_PATH * 2];
            _snwprintf(full, MAX_PATH * 2, L"%s\\%s", buf, p);
            full[MAX_PATH * 2 - 1] = 0;
            added += AddItem(full);
        }
    if (added)
        AfterListChanged();
}

/* ショートカット(.lnk/.url) または 任意のファイルを選んで追加 */
static void PickFiles(HWND hwnd, BOOL shortcutsOnly)
{
    wchar_t *file = (wchar_t *)calloc(65536, sizeof(wchar_t));
    if (!file)
        return;
    wchar_t startDir[MAX_PATH];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = shortcutsOnly ? L"ショートカット\0*.lnk;*.url\0"
                                    : L"すべてのファイル\0*.*\0アプリ\0*.exe;*.bat;*.cmd\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 65536;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_NODEREFERENCELINKS |
                OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = shortcutsOnly ? L"ショートカットを追加" : L"ファイルを追加";
    if (shortcutsOnly && SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startDir)))
        ofn.lpstrInitialDir = startDir; /* スタートメニューのアプリ一覧から選びやすく */
    if (GetOpenFileNameW(&ofn))
        AddPicked(file);
    free(file);
}

static void AddFolderDialog(HWND hwnd)
{
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"ランチャーに追加するフォルダ";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl)
    {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path) && AddItem(path))
            AfterListChanged();
        CoTaskMemFree(pidl);
    }
}

/* ------------------------------------------------------------------ */
/* 新規作成 (ファイル / フォルダー) と名前の入力                            */
/* ------------------------------------------------------------------ */
#define IB_EDIT 100
typedef struct
{
    wchar_t text[MAX_PATH];
    BOOL ok;
    HWND edit;
    HFONT font;
} IBState;

static int S(int v) { return (int)(v * g_dpi + 0.5f); }

static LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    IBState *st = (IBState *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m)
    {
    case WM_CREATE:
    {
        st = (IBState *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        HINSTANCE hi = GetModuleHandleW(NULL);
        st->font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        st->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   S(12), S(12), S(356), S(26), h, (HMENU)(INT_PTR)IB_EDIT, hi, NULL);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(196), S(54),
                                  S(80), S(28), h, (HMENU)(INT_PTR)IDOK, hi, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(288),
                                  S(54), S(80), S(28), h, (HMENU)(INT_PTR)IDCANCEL, hi, NULL);
        SendMessageW(st->edit, WM_SETFONT, (WPARAM)st->font, TRUE);
        SendMessageW(ok, WM_SETFONT, (WPARAM)st->font, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)st->font, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK)
        {
            GetWindowTextW(st->edit, st->text, MAX_PATH);
            st->ok = TRUE;
            DestroyWindow(h);
        }
        else if (LOWORD(w) == IDCANCEL)
            DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* 名前入力ダイアログ (text は入出力, MAX_PATH)。OK なら TRUE */
static BOOL InputBox(HWND owner, const wchar_t *title, wchar_t *text)
{
    static BOOL reg;
    HINSTANCE hi = GetModuleHandleW(NULL);
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = InputProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"PromLauncherInput";
        RegisterClassExW(&wc);
        reg = TRUE;
    }
    IBState st;
    memset(&st, 0, sizeof(st));
    wcsncpy(st.text, text, MAX_PATH - 1);

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    RECT rc = {0, 0, S(380), S(96)};
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 3;

    HWND dlg = CreateWindowExW(ex, L"PromLauncherInput", title, style, x, y, w, h, owner, NULL, hi, &st);
    if (!dlg)
        return FALSE;
    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(st.edit);
    {
        const wchar_t *dot = wcsrchr(st.text, L'.');
        int sel = (dot && dot != st.text) ? (int)(dot - st.text) : -1; /* 拡張子は選択から外す */
        SendMessageW(st.edit, EM_SETSEL, 0, sel);
    }
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0)
        if (!IsDialogMessage(dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (st.font)
        DeleteObject(st.font);
    if (st.ok)
        wcscpy(text, st.text);
    return st.ok;
}

/* 新規作成したものの置き場所: exe と同じフォルダの LauncherItems */
static void GetItemsDir(wchar_t *out, size_t cch)
{
    wcsncpy(out, g_cfgPath, cch - 1);
    out[cch - 1] = 0;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash)
        *slash = 0;
    wcsncat(out, L"\\LauncherItems", cch - wcslen(out) - 1);
}

static BOOL IsOwnedItem(const wchar_t *spec)
{
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
    GetItemsDir(dir, MAX_PATH * 2);
    ResolveSpec(spec, path, MAX_PATH * 2);
    size_t n = wcslen(dir);
    return _wcsnicmp(path, dir, n) == 0 && path[n] == L'\\';
}

static void CreateNew(BOOL folder)
{
    wchar_t dir[MAX_PATH * 2], path[MAX_PATH * 2];
    GetItemsDir(dir, MAX_PATH * 2);
    CreateDirectoryW(dir, NULL); /* 既にあってもOK */
    const wchar_t *base = folder ? L"新しいフォルダー" : L"新しいファイル";
    const wchar_t *ext = folder ? L"" : L".txt";
    BOOL found = FALSE;
    for (int n = 1; n < 1000 && !found; n++)
    {
        if (n == 1)
            _snwprintf(path, MAX_PATH * 2, L"%s\\%s%s", dir, base, ext);
        else
            _snwprintf(path, MAX_PATH * 2, L"%s\\%s (%d)%s", dir, base, n, ext);
        path[MAX_PATH * 2 - 1] = 0;
        found = GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES;
    }
    BOOL ok = FALSE;
    if (found)
    {
        if (folder)
            ok = CreateDirectoryW(path, NULL);
        else
        {
            HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                CloseHandle(h);
                ok = TRUE;
            }
        }
    }
    if (!ok || !AddItem(path))
    {
        MessageBeep(MB_ICONERROR);
        return;
    }
    g_search[0] = 0; /* 検索中でも作ったものが見えるように */
    AfterListChanged();
    g_sel = g_vn - 1;
    g_pageTarget = g_sel / g_per;
    g_dirty = TRUE;
    EnsureTimer();
}

/* 新規作成したファイル/フォルダーの実体の名前を変更 */
static void RenameItem(HWND hwnd, int idx)
{
    wchar_t path[MAX_PATH * 2];
    ResolveSpec(g_items[idx].spec, path, MAX_PATH * 2);
    wchar_t *base = wcsrchr(path, L'\\');
    if (!base)
        return;
    base++;
    wchar_t name[MAX_PATH], clean[MAX_PATH];
    wcsncpy(name, base, MAX_PATH - 1);
    name[MAX_PATH - 1] = 0;
    if (!InputBox(hwnd, L"名前を変更", name))
        return;
    int n = 0;
    for (const wchar_t *c = name; *c && n < MAX_PATH - 1; c++)
        if (!wcschr(L"\\/:*?\"<>|", *c))
            clean[n++] = *c;
    while (n > 0 && (clean[n - 1] == L' ' || clean[n - 1] == L'.'))
        n--;
    clean[n] = 0;
    size_t dl = (size_t)(base - path);
    if (!clean[0] || _wcsicmp(clean, base) == 0 || dl + wcslen(clean) >= MAX_PATH * 2)
        return;
    wchar_t np[MAX_PATH * 2];
    wcsncpy(np, path, dl);
    np[dl] = 0;
    wcscat(np, clean);
    if (!MoveFileW(path, np))
    {
        MessageBeep(MB_ICONERROR);
        return;
    }
    wcscpy(g_items[idx].spec, np);
    NameFromSpec(np, g_items[idx].name, 128);
    FreeItemData(&g_items[idx]); /* アイコン(拡張子で変わる)とラベルを作り直す */
    g_items[idx].tries = 0;
    AfterListChanged();
}

static void ShowContextMenu(HWND hwnd, int vi)
{
    HMENU menu = CreatePopupMenu();
    int idx = (vi >= 0 && vi < g_vn) ? g_view[vi] : -1;
    if (idx >= 0)
    {
        AppendMenuW(menu, MF_STRING, 1, L"開く");
        AppendMenuW(menu, MF_STRING, 2, L"管理者として実行");
        AppendMenuW(menu, MF_STRING, 3, L"ファイルの場所を開く");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        if (IsOwnedItem(g_items[idx].spec))
            AppendMenuW(menu, MF_STRING, 5, L"名前を変更…");
        AppendMenuW(menu, MF_STRING, 4, L"ランチャーから削除");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(menu, MF_STRING, 13, L"新しいファイルを作成");
    AppendMenuW(menu, MF_STRING, 14, L"新しいフォルダーを作成");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 10, L"ショートカットを追加…");
    AppendMenuW(menu, MF_STRING, 11, L"既存のファイルを追加…");
    AppendMenuW(menu, MF_STRING, 12, L"既存のフォルダを追加…");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_search[0] ? MF_GRAYED : 0), 15, L"名前順に並べ替え");

    POINT pt;
    GetCursorPos(&pt);
    g_modal = TRUE;
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessage(hwnd, WM_NULL, 0, 0);

    switch (cmd)
    {
    case 1:
        g_modal = FALSE;
        LaunchView(vi, L"open");
        return;
    case 2:
        g_modal = FALSE;
        LaunchView(vi, L"runas");
        return;
    case 3:
        if (idx >= 0)
        {
            wchar_t path[MAX_PATH * 2], args[MAX_PATH * 2 + 16];
            ResolveSpec(g_items[idx].spec, path, MAX_PATH * 2);
            _snwprintf(args, MAX_PATH * 2 + 16, L"/select,\"%s\"", path);
            args[MAX_PATH * 2 + 15] = 0;
            ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
        }
        break;
    case 4:
        if (idx >= 0)
        {
            RemoveItem(idx);
            g_sel = -1;
            AfterListChanged();
        }
        break;
    case 5:
        if (idx >= 0)
            RenameItem(hwnd, idx);
        break;
    case 10:
        PickFiles(hwnd, TRUE);
        break;
    case 11:
        PickFiles(hwnd, FALSE);
        break;
    case 12:
        AddFolderDialog(hwnd);
        break;
    case 13:
        CreateNew(FALSE);
        break;
    case 14:
        CreateNew(TRUE);
        break;
    case 15:
        qsort(g_items, g_count, sizeof(Item), CmpItemName);
        AfterListChanged();
        break;
    default:
        break;
    }
    g_modal = FALSE;
    g_showTick = GetTickCount(); /* ダイアログ後の非アクティブ化で即閉じないように */
    if (g_visible)
    {
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
}

/* ------------------------------------------------------------------ */
/* ウィンドウプロシージャ                                                  */
/* ------------------------------------------------------------------ */
static void SelectMove(int dx, int dy)
{
    if (g_vn <= 0)
        return;
    if (g_sel < 0)
        g_sel = g_pageTarget * g_per;
    else
    {
        int n = g_sel + dx + dy * g_cols;
        if (dy != 0 && (n < 0 || n >= g_vn))
            n = g_sel; /* 上下は端で止める */
        g_sel = ClampI(n, 0, g_vn - 1);
    }
    g_pageTarget = g_sel / g_per;
    g_dirty = TRUE;
    EnsureTimer();
}

static void GoPage(int p)
{
    g_pageTarget = ClampI(p, 0, g_pages - 1);
    g_dirty = TRUE;
    EnsureTimer();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_HOTKEY:
        ToggleLauncher();
        return 0;

    case WM_ICONDONE:
    {
        IconRes *rs = (IconRes *)lp;
        if (rs)
        {
            BOOL used = FALSE;
            for (int i = 0; i < g_count; i++)
                if (g_items[i].id == rs->id)
                {
                    free(g_items[i].px);
                    g_items[i].px = rs->px;
                    used = TRUE;
                    break;
                }
            if (!used)
                free(rs->px);
            free(rs);
        }
        if (g_visible)
        {
            g_dirty = TRUE;
            EnsureTimer();
        }
        return 0;
    }

    case WM_LAUNCHFAIL:
    {
        LaunchReq *rq = (LaunchReq *)lp;
        for (int i = 0; i < g_count; i++)
            if (_wcsicmp(g_items[i].spec, rq->spec) == 0 && g_items[i].id == g_launchId)
                g_launchId = -1;
        free(rq);
        MessageBeep(MB_ICONERROR);
        if (g_visible)
            ShowLauncher(); /* 失敗したので閉じずに戻す */
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g_visible && !g_modal && GetTickCount() - g_showTick > 400)
            HideLauncher();
        return 0;

    case WM_MOUSEMOVE:
    {
        float nx = (float)(short)LOWORD(lp), ny = (float)(short)HIWORD(lp);
        BOOL moved = (nx != g_mx || ny != g_my);
        g_mx = nx;
        g_my = ny;
        if ((wp & MK_LBUTTON) && g_pressed != -2 && GetCapture() == hwnd)
        {
            if (g_mode == 0)
            {
                int dx = (int)nx - g_downPt.x, dy = (int)ny - g_downPt.y;
                float th = 8.0f * g_dpi;
                if ((float)(dx * dx + dy * dy) >= th * th)
                {
                    if (g_pressed >= 0 && !g_search[0])
                    {
                        g_mode = 2;
                        g_dragIdx = g_pressed;
                        g_flipTick = GetTickCount();
                    }
                    else
                    {
                        g_mode = 1;
                        g_swipeF0 = g_pageF;
                        g_swipePage0 = g_pageTarget;
                    }
                }
            }
            if (g_mode == 1)
            {
                g_pageF = g_swipeF0 - (nx - g_downPt.x) / (float)g_W;
                if (g_pageF < -0.35f)
                    g_pageF = -0.35f;
                if (g_pageF > g_pages - 1 + 0.35f)
                    g_pageF = g_pages - 1 + 0.35f;
            }
            else if (g_mode == 2)
            {
                int hit = HitItem(nx, ny);
                if (hit >= 0 && hit != g_dragIdx)
                {
                    MoveItem(g_view[g_dragIdx], g_view[hit]); /* 検索なし中のみ: view == items */
                    g_dragIdx = hit;
                    RebuildView();
                }
            }
        }
        else if (moved && g_mode == 0)
        {
            int hit = HitItem(nx, ny);
            if (hit != g_sel && hit >= 0)
                g_sel = hit;
            else if (hit < 0 && g_sel >= 0 && !(wp & MK_LBUTTON))
                g_sel = -1;
        }
        g_dirty = TRUE;
        EnsureTimer();
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        g_downPt.x = (short)LOWORD(lp);
        g_downPt.y = (short)HIWORD(lp);
        g_pressed = HitItem((float)g_downPt.x, (float)g_downPt.y);
        g_mode = 0;
        return 0;

    case WM_LBUTTONUP:
    {
        float ux = (float)(short)LOWORD(lp), uy = (float)(short)HIWORD(lp);
        if (GetCapture() == hwnd)
            ReleaseCapture();
        int mode = g_mode;
        g_mode = 0;
        if (mode == 1)
        {
            float delta = (g_downPt.x - ux) / (float)g_W; /* 右へ引いた分だけ次ページ */
            int target = g_swipePage0;
            if (delta > 0.10f)
                target++;
            else if (delta < -0.10f)
                target--;
            GoPage(target);
        }
        else if (mode == 2)
        {
            g_dragIdx = -1;
            SaveConfig();
            g_dirty = TRUE;
            EnsureTimer();
        }
        else if (g_pressed >= 0)
        {
            if (HitItem(ux, uy) == g_pressed)
                LaunchView(g_pressed, L"open");
        }
        else
        {
            int pg;
            BOOL inSearch = ux >= g_sbL && ux <= g_sbL + g_sbW && uy >= g_sbT && uy <= g_sbT + g_sbH;
            if (HitDot(ux, uy, &pg))
                GoPage(pg);
            else if (!inSearch)
                HideLauncher(); /* 何もない所をクリックで閉じる */
        }
        g_pressed = -1;
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_mode == 2)
            SaveConfig();
        g_mode = 0;
        g_dragIdx = -1;
        g_dirty = TRUE;
        return 0;

    case WM_RBUTTONUP:
        ShowContextMenu(hwnd, HitItem((float)(short)LOWORD(lp), (float)(short)HIWORD(lp)));
        return 0;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        DWORD now = GetTickCount();
        if (now - g_wheelTick > 120)
        {
            int dlt = (short)HIWORD(wp);
            if (dlt != 0)
            {
                int dir = (msg == WM_MOUSEWHEEL) ? (dlt < 0 ? 1 : -1) : (dlt > 0 ? 1 : -1);
                GoPage(g_pageTarget + dir);
                g_wheelTick = now;
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp)
        {
        case VK_ESCAPE:
            if (g_search[0])
            {
                g_search[0] = 0;
                SearchChanged();
            }
            else
                HideLauncher();
            return 0;
        case VK_RETURN:
            if (g_vn > 0)
                LaunchView(g_sel >= 0 ? g_sel : 0, L"open");
            return 0;
        case VK_LEFT:
            SelectMove(-1, 0);
            return 0;
        case VK_RIGHT:
            SelectMove(1, 0);
            return 0;
        case VK_UP:
            SelectMove(0, -1);
            return 0;
        case VK_DOWN:
            SelectMove(0, 1);
            return 0;
        case VK_PRIOR:
            GoPage(g_pageTarget - 1);
            return 0;
        case VK_NEXT:
            GoPage(g_pageTarget + 1);
            return 0;
        default:
            if ((int)wp == g_hotkey)
            {
                HideLauncher();
                return 0;
            }
            break;
        }
        break;

    case WM_CHAR:
    {
        wchar_t c = (wchar_t)wp;
        size_t n = wcslen(g_search);
        if (c == 8)
        {
            if (n > 0)
            {
                g_search[n - 1] = 0;
                SearchChanged();
            }
        }
        else if (c >= 32 && c != 127 && n < 40)
        {
            g_search[n] = c;
            g_search[n + 1] = 0;
            SearchChanged();
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_ANIM)
        {
            BOOL keep = Step();
            if (g_visible)
                Render();
            if (!keep)
            {
                KillTimer(hwnd, TIMER_ANIM);
                g_animOn = FALSE;
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        g_quit = 1;
        UnregisterHotKey(hwnd, HOTKEY_ID);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* promdock.c から呼ばれる入口                                            */
/* ------------------------------------------------------------------ */
void Launcher_Init(HINSTANCE hInst)
{
    HDC scr = GetDC(NULL);
    g_dpi = GetDeviceCaps(scr, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, scr);

    GetModuleFileNameW(NULL, g_cfgPath, MAX_PATH);
    wchar_t *slash = wcsrchr(g_cfgPath, L'\\');
    if (slash)
        wcscpy(slash + 1, L"launcher.txt");
    else
        wcscpy(g_cfgPath, L"launcher.txt");
    if (!LoadConfig())
        SaveConfig(); /* 初回は空の状態で始める */

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromLauncherWnd";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"PromLauncher",
                             WS_POPUP, 0, 0, 100, 100, NULL, NULL, hInst, NULL);
    RegisterHotKey(g_hwnd, HOTKEY_ID, MOD_NOREPEAT, (UINT)g_hotkey); /* 失敗しても(他アプリが使用中)メニューから開ける */

    CalcGeom();
    RebuildView();
    StartIconLoad(); /* 起動直後からアイコンを先読み */
}

void Launcher_Toggle(void)
{
    if (g_hwnd)
        ToggleLauncher();
}

void Launcher_Shutdown(void)
{
    g_quit = 1;
    if (g_hwnd)
    {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
    }
    for (int i = 0; i < g_count; i++)
        FreeItemData(&g_items[i]);
    free(g_sMask);
    g_sMask = NULL;
}