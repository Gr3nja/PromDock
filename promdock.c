#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
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

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

/* ---- 調整用パラメータ (96DPI基準のピクセル) ---- */
#define BASE_SIZE 48.0f
#define MAX_SCALE 1.5f /* 拡大の最大倍率 (控えめ) */
#define ICON_GAP 8.0f
#define BAR_PAD_X 14.0f
#define BAR_PAD_Y 12.0f
#define EDGE_MARGIN 6.0f
#define MAG_SIGMA 60.0f /* 拡大が及ぶ範囲 (大きいほど周辺まで広がる) */
#define BOUNCE_HEIGHT 28.0f
#define BOUNCE_MS 900
#define LABEL_H 32.0f       /* ラベル用に確保する高さ */
#define LABEL_W_VERT 260.0f /* 縦置き時にラベル用に確保する幅 */
#define POLL_MS 700
#define MAX_ITEMS 32
#define MAX_WINS 8
#define MIP_LEVELS 3
#define TIMER_ANIM 1
#define TIMER_POLL 2

static const GUID MY_IID_IShellItem =
    {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
static const GUID MY_IID_IShellItemImageFactory =
    {0xbcc18b79, 0xba16, 0x442f, {0x80, 0xc4, 0x8a, 0x59, 0xc3, 0x0c, 0x46, 0x3b}};

typedef struct
{
    wchar_t spec[MAX_PATH * 2]; /* promdock.txt に書く文字列 (パス) */
    wchar_t name[128];          /* ラベル */
    wchar_t exe[MAX_PATH];      /* 実行中判定用のexeパス (不明なら空) */
    wchar_t proc[64];           /* 追加: --processStart で起動される実プロセス名 (例: Discord.exe) */
    BOOL pinned;
    DWORD launchTick;
    BOOL iconOk;               /* FALSE: アイコン取得失敗(仮アイコン)。後で再取得する */
    uint32_t *mip[MIP_LEVELS]; /* premultiplied BGRA 256/128/64 */
    float scale, cx, sz;
    DWORD bounceStart;
    HWND wins[MAX_WINS];
    int nwin;
    BOOL fg;
} Item;

typedef struct
{
    int fr, fg, fb;
    float fa;
    int br, bg, bb;
    float ba;
} Style;
static const Style g_barStyle = {36, 36, 40, 0.78f, 0, 0, 0, 0.0f}; /* ダークグレー半透明(濃いめ)・枠線なし */
static const Style g_labelStyle = {26, 26, 30, 0.92f, 0, 0, 0, 0.0f};

static const int g_mipSize[MIP_LEVELS] = {256, 128, 64};

static Item g_items[MAX_ITEMS];
static int g_count = 0;
static HWND g_hwnd;
static HDC g_memdc;
static HBITMAP g_dib, g_oldBmp;
static uint32_t *g_bits;
static int g_W, g_H, g_posX, g_posY0;
static float g_hideDelta, g_slide = 0.0f;
static int g_LW, g_LH;      /* 論理寸法: 軸方向の長さ / 画面端からの厚み */
static int g_edge = 0;      /* 0:下 1:上 2:左 3:右 */
static float g_frac = 0.5f; /* 端に沿った位置 (0..1, ドック中心) */
static int g_hideMs = 600;  /* 自動的に隠すまでの時間(ms) */
static BOOL g_moving;       /* Ctrl+ドラッグで移動中 */
static float g_dpi = 1.0f;
static float g_base, g_gap, g_padx, g_pady, g_margin, g_bounce;
static float g_baseline, g_barV0, g_barV1, g_barL, g_barR, g_hoverV; /* v: 画面端からの距離 */
static BOOL g_hover, g_tracking, g_animOn, g_menuOpen, g_dragged;
static BOOL g_autohide = FALSE;

/* ドラッグ&ドロップ状態 */
static int g_dragOut = -1;          /* ドックの外へ引き出し中の項目 (-1: なし) */
static BOOL g_dropActive, g_dropOk; /* 外部からのファイルドラッグ中 */
static int g_dropIdx;               /* 挿入位置 (ピン留め内の何番目の前か) */
static float g_dropOpen;            /* 挿入位置の隙間 0..1 */
static HWND g_ghost;                /* ドラッグ中アイコンのゴースト窓 */
static HDC g_ghostDC;
static HBITMAP g_ghostBmp, g_ghostOld;
static uint32_t *g_ghostBits;
static int g_ghostAX, g_ghostAY;
static float g_mouseU, g_mouseV; /* マウス位置 (論理座標: u=軸方向, v=端からの距離) */
static int g_pressed = -1;
static POINT g_downPt; /* 押下位置 (クライアント座標) */
static BOOL g_armed;   /* 一定距離動いたらドラッグ開始 */
static DWORD g_leaveTick;
static unsigned g_sig;
static wchar_t g_cfgPath[MAX_PATH];

/* ラベル用キャッシュ */
static wchar_t g_lblText[128];
static uint8_t *g_lblMask;
static int g_lblW, g_lblH;

/* ------------------------------------------------------------------ */
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* 論理座標 (u: 軸方向, v: 画面端からの距離) <-> ウィンドウ座標。
 * 内部の計算は「下端に置いた横向きドック」で行い、ここで4方向に写す。 */
static BOOL IsVert(void) { return g_edge >= 2; }
static void L2W(float u, float v, float *x, float *y)
{
    switch (g_edge)
    {
    case 0:
        *x = u;
        *y = g_H - v;
        break;
    case 1:
        *x = u;
        *y = v;
        break;
    case 2:
        *x = v;
        *y = u;
        break;
    default:
        *x = g_W - v;
        *y = u;
        break;
    }
}
static void W2L(float x, float y, float *u, float *v)
{
    switch (g_edge)
    {
    case 0:
        *u = x;
        *v = g_H - y;
        break;
    case 1:
        *u = x;
        *v = y;
        break;
    case 2:
        *u = y;
        *v = x;
        break;
    default:
        *u = y;
        *v = g_W - x;
        break;
    }
}
static void LRect(float u0, float v0, float u1, float v1, float *L, float *T, float *R, float *B)
{
    float xa, ya, xb, yb;
    L2W(u0, v0, &xa, &ya);
    L2W(u1, v1, &xb, &yb);
    *L = fminf(xa, xb);
    *R = fmaxf(xa, xb);
    *T = fminf(ya, yb);
    *B = fmaxf(ya, yb);
}

static int NPinned(void)
{
    int n = 0;
    for (int i = 0; i < g_count; i++)
        if (g_items[i].pinned)
            n++;
    return n;
}
static float SepW(void)
{
    int np = NPinned();
    return (np > 0 && np < g_count) ? g_gap * 2.5f : 0.0f;
}

/* ------------------------------------------------------------------ */
/* パス解決・アイコン取得                                                */
/* ------------------------------------------------------------------ */
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

static BOOL EndsWithI(const wchar_t *s, const wchar_t *suffix)
{
    size_t a = wcslen(s), b = wcslen(suffix);
    return a >= b && _wcsicmp(s + a - b, suffix) == 0;
}

/* .exe ならそのパス、.lnk ならリンク先を exe に入れる */
static void ResolveExe(const wchar_t *spec, wchar_t *exe, size_t cch, wchar_t *proc)
{
    wchar_t path[MAX_PATH * 2];
    exe[0] = 0;
    proc[0] = 0;
    ResolveSpec(spec, path, MAX_PATH * 2);
    if (EndsWithI(path, L".exe"))
    {
        wcsncpy(exe, path, cch - 1);
        exe[cch - 1] = 0;
        return;
    }
    if (EndsWithI(path, L".lnk"))
    {
        IShellLinkW *psl = NULL;
        IPersistFile *ppf = NULL;
        if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_IShellLinkW, (void **)&psl)))
        {
            if (SUCCEEDED(IShellLinkW_QueryInterface(psl, &IID_IPersistFile, (void **)&ppf)))
            {
                if (SUCCEEDED(IPersistFile_Load(ppf, path, STGM_READ)))
                {
                    wchar_t t[MAX_PATH];
                    if (SUCCEEDED(IShellLinkW_GetPath(psl, t, MAX_PATH, NULL, 0)) && t[0])
                    {
                        ExpandEnvironmentStringsW(t, exe, (DWORD)cch);
                    }
                    /* Discord等: Update.exe --processStart Discord.exe の実プロセス名を取る */
                    wchar_t args[1024];
                    if (SUCCEEDED(IShellLinkW_GetArguments(psl, args, 1024)))
                    {
                        const wchar_t *k = wcsstr(args, L"--processStart");
                        if (k)
                        {
                            k += 14;
                            while (*k == L' ' || *k == L'=' || *k == L'"')
                                k++;
                            size_t n = 0;
                            while (k[n] && k[n] != L' ' && k[n] != L'"' && n < 63)
                                n++;
                            wcsncpy(proc, k, n);
                            proc[n] = 0;
                        }
                    }
                }
                IPersistFile_Release(ppf);
            }
            IShellLinkW_Release(psl);
        }
    }
}

static void BuildMips(Item *it, uint32_t *base256)
{
    it->mip[0] = base256;
    for (int l = 1; l < MIP_LEVELS; l++)
    {
        int n = g_mipSize[l], pn = g_mipSize[l - 1];
        uint32_t *dst = (uint32_t *)malloc((size_t)n * n * 4);
        const uint32_t *src = it->mip[l - 1];
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++)
            {
                uint32_t p[4] = {src[(2 * y) * pn + 2 * x], src[(2 * y) * pn + 2 * x + 1],
                                 src[(2 * y + 1) * pn + 2 * x], src[(2 * y + 1) * pn + 2 * x + 1]};
                uint32_t out = 0;
                for (int sh = 0; sh < 32; sh += 8)
                {
                    uint32_t s = ((p[0] >> sh) & 255) + ((p[1] >> sh) & 255) +
                                 ((p[2] >> sh) & 255) + ((p[3] >> sh) & 255);
                    out |= ((s + 2) / 4) << sh;
                }
                dst[y * n + x] = out;
            }
        it->mip[l] = dst;
    }
}

static uint32_t *MakePlaceholder(void)
{
    uint32_t *buf = (uint32_t *)malloc(256 * 256 * 4);
    for (int i = 0; i < 256 * 256; i++)
        buf[i] = 0xFF70747E;
    return buf;
}

/* IShellItemImageFactory 経由。256x256 premultiplied BGRA を返す。表示名も取得。失敗/空白なら NULL */
static uint32_t *FetchViaFactory(const wchar_t *spec, wchar_t *name, int nameCch)
{
    wchar_t path[MAX_PATH * 2];
    ResolveSpec(spec, path, MAX_PATH * 2);

    IShellItem *si = NULL;
    if (FAILED(SHCreateItemFromParsingName(path, NULL, &MY_IID_IShellItem, (void **)&si)))
        return NULL;

    LPWSTR dn = NULL;
    if (SUCCEEDED(IShellItem_GetDisplayName(si, SIGDN_NORMALDISPLAY, &dn)) && dn)
    {
        wcsncpy(name, dn, nameCch - 1);
        name[nameCch - 1] = 0;
        CoTaskMemFree(dn);
    }
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
        { /* 完全な空白 (起動直後などシェルが未準備) -> 失敗扱い */
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
            int dg = (int)((wht[i] >> 8) & 255) - (int)((blk[i] >> 8) & 255); /* = 255 - alpha */
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
            uint32_t r = (blk[i] >> 16) & 255, g = (blk[i] >> 8) & 255, b = blk[i] & 255; /* 黒地の値 = 色*alpha (premultiplied) */
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

/* フォールバック: exe/dll/ico は PrivateExtractIcons、それ以外は SHGetFileInfo */
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

/* 256x256 premultiplied BGRA を返す。表示名も取得。取得できなければ NULL */
static uint32_t *FetchIcon256(const wchar_t *spec, wchar_t *name, int nameCch)
{
    uint32_t *px = FetchViaFactory(spec, name, nameCch);
    if (!px)
        px = FetchViaHicon(spec);
    return px;
}

static void CleanName(wchar_t *name, const wchar_t *spec)
{
    if (!name[0])
    {
        const wchar_t *sl = wcsrchr(spec, L'\\');
        wcsncpy(name, sl ? sl + 1 : spec, 127);
        name[127] = 0;
    }
    if (EndsWithI(name, L".exe") || EndsWithI(name, L".lnk"))
        name[wcslen(name) - 4] = 0;
}

static void FreeItem(Item *it)
{
    for (int l = 0; l < MIP_LEVELS; l++)
    {
        free(it->mip[l]);
        it->mip[l] = NULL;
    }
}

static BOOL AddItemAt(const wchar_t *spec, BOOL pinned, int at)
{
    if (g_count >= MAX_ITEMS)
        return FALSE;
    if (pinned)
        for (int i = 0; i < g_count; i++)
            if (g_items[i].pinned && _wcsicmp(g_items[i].spec, spec) == 0)
                return FALSE;

    Item it;
    memset(&it, 0, sizeof(it));
    wcsncpy(it.spec, spec, (sizeof(it.spec) / sizeof(it.spec[0])) - 1);
    it.pinned = pinned;
    it.scale = 1.0f;

    uint32_t *px = FetchIcon256(spec, it.name, 128);
    it.iconOk = px != NULL;
    CleanName(it.name, spec);

    BuildMips(&it, px ? px : MakePlaceholder());
    ResolveExe(spec, it.exe, MAX_PATH, it.proc);

    int pos = pinned ? ((at >= 0 && at <= NPinned()) ? at : NPinned()) : g_count; /* ピン留めは先頭グループ */
    memmove(&g_items[pos + 1], &g_items[pos], (g_count - pos) * sizeof(Item));
    g_items[pos] = it;
    g_count++;
    return TRUE;
}

static BOOL AddItem(const wchar_t *spec, BOOL pinned) { return AddItemAt(spec, pinned, -1); }

/* 起動直後はシェルが未準備でアイコン取得に失敗することがある。成功するまで再取得する */
static BOOL RetryMissingIcons(void)
{
    BOOL changed = FALSE;
    for (int i = 0; i < g_count; i++)
    {
        Item *it = &g_items[i];
        if (it->iconOk)
            continue;
        wchar_t nm[128];
        nm[0] = 0;
        uint32_t *px = FetchIcon256(it->spec, nm, 128);
        if (!px)
            continue;
        for (int l = 0; l < MIP_LEVELS; l++)
        {
            free(it->mip[l]);
            it->mip[l] = NULL;
        }
        BuildMips(it, px);
        it->iconOk = TRUE;
        if (nm[0])
        {
            CleanName(nm, it->spec);
            wcscpy(it->name, nm);
        }
        changed = TRUE;
    }
    return changed;
}

static BOOL HasMissingIcons(void)
{
    for (int i = 0; i < g_count; i++)
        if (!g_items[i].iconOk)
            return TRUE;
    return FALSE;
}

static void SwapItems(int a, int b)
{
    Item t = g_items[a];
    g_items[a] = g_items[b];
    g_items[b] = t;
    float s = g_items[a].scale;
    g_items[a].scale = g_items[b].scale;
    g_items[b].scale = s; /* 拡大率は位置に残す */
}

static void PinItem(int idx)
{
    int np = NPinned();
    Item t = g_items[idx];
    t.pinned = TRUE;
    memmove(&g_items[np + 1], &g_items[np], (idx - np) * sizeof(Item));
    g_items[np] = t;
}

/* ------------------------------------------------------------------ */
/* 設定ファイル                                                         */
/* ------------------------------------------------------------------ */
static void SaveConfig(void)
{
    FILE *f = _wfopen(g_cfgPath, L"wb");
    if (!f)
        return;
    fprintf(f, "@autohide=%d\r\n@edge=%d\r\n@frac=%d\r\n@hidems=%d\r\n", g_autohide ? 1 : 0, g_edge, (int)(g_frac * 1000.0f + 0.5f), g_hideMs);
    for (int i = 0; i < g_count; i++)
    {
        if (!g_items[i].pinned)
            continue;
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

static void LoadConfig(void)
{
    FILE *f = _wfopen(g_cfgPath, L"rb");
    if (!f)
    {
        SaveConfig();
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    len = (long)fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    char *p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
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
            if (strncmp(p, "@autohide=", 10) == 0)
                g_autohide = atoi(p + 10) != 0;
            else if (strncmp(p, "@edge=", 6) == 0)
                g_edge = ClampI(atoi(p + 6), 0, 3);
            else if (strncmp(p, "@frac=", 6) == 0)
                g_frac = ClampI(atoi(p + 6), 0, 1000) / 1000.0f;
            else if (strncmp(p, "@hidems=", 8) == 0)
                g_hideMs = ClampI(atoi(p + 8), 0, 60000);
        }
        else if (*p && *p != '#')
        {
            wchar_t w[MAX_PATH * 2];
            if (MultiByteToWideChar(CP_UTF8, 0, p, -1, w, MAX_PATH * 2))
                AddItem(w, TRUE);
        }
        *e = save;
        while (*e == '\n' || *e == '\r')
            e++;
        p = e;
    }
    free(buf);
}

/* ------------------------------------------------------------------ */
/* 実行中ウィンドウの検出                                                */
/* ------------------------------------------------------------------ */
typedef struct
{
    HWND h;
    wchar_t exe[MAX_PATH];
} WinRec;
static WinRec g_win[128];
static int g_nw;

static BOOL IsTaskbarWindow(HWND h)
{
    if (h == g_hwnd || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
        return FALSE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return FALSE;
    if (GetWindowTextLengthW(h) == 0)
        return FALSE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return FALSE;
    wchar_t cls[32];
    if (GetClassNameW(h, cls, 32) && _wcsicmp(cls, L"Progman") == 0)
        return FALSE;
    return TRUE;
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM lp)
{
    (void)lp;
    if (g_nw >= 128)
        return FALSE;
    if (!IsTaskbarWindow(h))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!ph)
        return TRUE;
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(ph, 0, buf, &n);
    CloseHandle(ph);
    if (!ok)
        return TRUE;
    const wchar_t *b = wcsrchr(buf, L'\\');
    b = b ? b + 1 : buf;
    if (_wcsicmp(b, L"ApplicationFrameHost.exe") == 0)
        return TRUE; /* UWPのラッパ */
    g_win[g_nw].h = h;
    wcscpy(g_win[g_nw].exe, buf);
    g_nw++;
    return TRUE;
}

static unsigned CalcSig(void)
{
    unsigned s = (unsigned)g_count;
    for (int i = 0; i < g_count; i++)
        s = s * 31u + (unsigned)g_items[i].nwin * 7u + (g_items[i].fg ? 3u : 0u) +
            (unsigned)(UINT_PTR)(g_items[i].nwin ? g_items[i].wins[0] : NULL);
    return s;
}

/* 戻り値: 0=変化なし 1=見た目のみ 2=項目数/並びが変わった */
static int RefreshRunning(void)
{
    BOOL structural = FALSE;
    g_nw = 0;
    EnumWindows(EnumProc, 0);
    HWND fgw = GetForegroundWindow();
    for (int i = 0; i < g_count; i++)
    {
        g_items[i].nwin = 0;
        g_items[i].fg = FALSE;
    }

    for (int k = 0; k < g_nw; k++)
    {
        int idx = -1;
        const wchar_t *bn = wcsrchr(g_win[k].exe, L'\\');
        bn = bn ? bn + 1 : g_win[k].exe;
        for (int i = 0; i < g_count; i++)
            if ((g_items[i].exe[0] && _wcsicmp(g_items[i].exe, g_win[k].exe) == 0) ||
                (g_items[i].proc[0] && _wcsicmp(g_items[i].proc, bn) == 0))
            {
                idx = i;
                break;
            }
        if (idx >= 0)
        {
            /* ここから下は元のまま */
            Item *it = &g_items[idx];
            if (it->nwin < MAX_WINS)
                it->wins[it->nwin++] = g_win[k].h;
            if (g_win[k].h == fgw)
                it->fg = TRUE;
        }
    }
    for (int i = g_count - 1; i >= 0; i--) /* 終了した未固定アプリを外す */
        if (!g_items[i].pinned && g_items[i].nwin == 0)
        {
            FreeItem(&g_items[i]);
            memmove(&g_items[i], &g_items[i + 1], (g_count - i - 1) * sizeof(Item));
            g_count--;
            structural = TRUE;
        }

    unsigned s = CalcSig();
    int r = structural ? 2 : (s != g_sig ? 1 : 0);
    g_sig = s;
    return r;
}

/* 自プロセスが非アクティブでも確実に前面へ出す */
static void ForceForeground(HWND h)
{
    if (IsIconic(h))
        ShowWindow(h, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    DWORD me = GetCurrentThreadId(), ft = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    BOOL att = (ft && ft != me) ? AttachThreadInput(me, ft, TRUE) : FALSE;
    BringWindowToTop(h);
    SetForegroundWindow(h);
    if (att)
        AttachThreadInput(me, ft, FALSE);
}

static void ActivateItem(Item *it)
{
    if (it->nwin == 0)
        return;
    HWND fgw = GetForegroundWindow();
    int cur = -1;
    for (int i = 0; i < it->nwin; i++)
        if (it->wins[i] == fgw)
            cur = i;
    ForceForeground(it->wins[(cur + 1) % it->nwin]); /* 複数ウィンドウは巡回 */
}

/* ------------------------------------------------------------------ */
/* ウィンドウ寸法・DIB                                                  */
/* ------------------------------------------------------------------ */
static void ResizeDock(void)
{
    float d = g_dpi;
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    BOOL vert = IsVert();

    g_gap = ICON_GAP * d;
    g_padx = BAR_PAD_X * d;
    g_pady = BAR_PAD_Y * d;
    g_margin = EDGE_MARGIN * d;
    g_bounce = BOUNCE_HEIGHT * d;

    int n = g_count > 0 ? g_count : 1;
    float len = (float)(vert ? (wa.bottom - wa.top) : (wa.right - wa.left));
    float limit = len * 0.92f;
    float sep = SepW();
    g_base = BASE_SIZE * d;
    for (;;)
    { /* 画面に収まるまでアイコンを縮める */
        float restW = n * g_base + (n - 1) * g_gap + 2 * g_padx + sep;
        float extra = (n < 6 ? n : 6) * g_base * (MAX_SCALE - 1.0f);
        g_LW = (int)(restW + extra + (g_base + g_gap) + 4 * g_margin);
        if (g_LW <= limit || g_base <= 20 * d)
            break;
        g_base -= 1.0f;
    }
    float labelExt = vert ? LABEL_W_VERT * d : LABEL_H * d;
    g_LH = (int)(g_margin + g_pady + MAX_SCALE * g_base + g_bounce + labelExt + 4);
    g_W = vert ? g_LH : g_LW;
    g_H = vert ? g_LW : g_LH;

    g_barV0 = g_margin;
    g_barV1 = g_margin + g_base + 2 * g_pady;
    g_baseline = g_margin + g_pady;
    g_hoverV = g_baseline + MAX_SCALE * g_base + 6 * d;
    g_hideDelta = g_barV1 - 4 * d; /* 隠れた時はバーの端4pxだけ見せる */

    float start = (float)(vert ? wa.top : wa.left);
    int along = (int)(start + g_frac * len - g_LW * 0.5f);
    along = ClampI(along, (int)start, (int)(start + len) - g_LW);
    switch (g_edge)
    {
    case 0:
        g_posX = along;
        g_posY0 = wa.bottom - g_H;
        break;
    case 1:
        g_posX = along;
        g_posY0 = wa.top;
        break;
    case 2:
        g_posX = wa.left;
        g_posY0 = along;
        break;
    default:
        g_posX = wa.right - g_W;
        g_posY0 = along;
        break;
    }

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
    bi.bmiHeader.biWidth = g_W;
    bi.bmiHeader.biHeight = -g_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_dib = CreateDIBSection(g_memdc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    g_oldBmp = (HBITMAP)SelectObject(g_memdc, g_dib);
    SetWindowPos(g_hwnd, HWND_TOPMOST, g_posX, g_posY0, g_W, g_H, SWP_NOACTIVATE);
}

/* ------------------------------------------------------------------ */
/* レイアウト・アニメーション                                            */
/* ------------------------------------------------------------------ */
/* 各項目の中心X(cx)と幅(szo)を左端0基準で求め、全体幅を返す。
 *   rest=1    : 拡大なしの静止配置 (拡大率の計算・挿入位置の判定用)
 *   withOpen=1: ドロップ位置の隙間を含める */
static float LayoutPass(float *cx, float *szo, int rest, int withOpen)
{
    int np = NPinned();
    float sepW = SepW();
    float open = withOpen ? g_dropOpen * (g_base + g_gap) : 0.0f;
    float x = 0.0f;
    if (g_dropIdx == 0)
        x += open;
    for (int i = 0; i < g_count; i++)
    {
        float s = rest ? ((i == g_dragOut) ? 0.0f : g_base) : g_base * g_items[i].scale;
        float gp = (i == g_dragOut) ? (rest ? 0.0f : Clamp01(g_items[i].scale) * g_gap) : g_gap;
        cx[i] = x + s * 0.5f;
        szo[i] = s;
        x += s + gp;
        if (i == g_dropIdx - 1)
            x += open;
        if (sepW > 0 && i == np - 1)
            x += sepW; /* 区切りは線なし・余白のみ */
    }
    return g_count > 0 ? x - g_gap : g_base * 2;
}

static void Layout(void)
{
    float cx[MAX_ITEMS], sz[MAX_ITEMS];
    float total = LayoutPass(cx, sz, 0, 1);
    float x = (g_LW - total) * 0.5f;
    g_barL = x - g_padx;
    g_barR = x + total + g_padx;
    for (int i = 0; i < g_count; i++)
    {
        g_items[i].sz = sz[i];
        g_items[i].cx = x + cx[i];
    }
}

static BOOL Step(void)
{
    BOOL busy = FALSE;
    DWORD now = GetTickCount();
    float sigma = MAG_SIGMA * g_dpi;
    float c[MAX_ITEMS], s[MAX_ITEMS];
    float total = LayoutPass(c, s, 1, 1);
    float off = (g_LW - total) * 0.5f;

    for (int i = 0; i < g_count; i++)
    {
        Item *it = &g_items[i];
        float target = 1.0f;
        if (i == g_dragOut)
            target = 0.0f; /* 引き出し中は隙間を詰める */
        else if (g_hover && !g_dragged && !g_dropActive)
        {
            /* マウス直下を最大に、距離に応じてガウス状に周辺も拡大 */
            float dx = g_mouseU - (off + c[i]);
            target = 1.0f + (MAX_SCALE - 1.0f) * expf(-(dx * dx) / (2 * sigma * sigma));
        }
        float diff = target - it->scale;
        if (fabsf(diff) < 0.003f)
            it->scale = target;
        else
        {
            it->scale += diff * 0.35f;
            busy = TRUE;
        }
        if (it->bounceStart && now - it->bounceStart < BOUNCE_MS)
            busy = TRUE;
    }

    /* ドロップ位置の隙間 */
    float openT = (g_dropActive && g_dropOk) ? 1.0f : 0.0f;
    float od = openT - g_dropOpen;
    if (fabsf(od) < 0.003f)
        g_dropOpen = openT;
    else
    {
        g_dropOpen += od * 0.3f;
        busy = TRUE;
    }

    /* 自動的に隠す */
    float slideTarget = 0.0f;
    if (g_autohide && !g_hover && !g_menuOpen && g_dragOut < 0 && now - g_leaveTick > (DWORD)g_hideMs)
        slideTarget = 1.0f;
    float sd = slideTarget - g_slide;
    if (fabsf(sd) < 0.003f)
        g_slide = slideTarget;
    else
    {
        g_slide += sd * 0.25f;
        busy = TRUE;
    }
    if (g_autohide && !g_hover && g_slide < 1.0f)
        busy = TRUE; /* 隠れるまで待機 */

    return busy || g_hover;
}

static int HitTest(float u, float v)
{
    if (v > g_hoverV)
        return -1;
    for (int i = 0; i < g_count; i++)
    {
        float l = g_items[i].cx - g_items[i].sz * 0.5f - g_gap * 0.5f;
        float r = g_items[i].cx + g_items[i].sz * 0.5f + g_gap * 0.5f;
        if (u >= l && u < r)
            return i;
    }
    return -1;
}

static int HitLP(LPARAM lp) /* クライアント座標(lParam)からヒット判定 */
{
    float u, v;
    W2L((float)(short)LOWORD(lp), (float)(short)HIWORD(lp), &u, &v);
    return HitTest(u, v);
}

/* ------------------------------------------------------------------ */
/* 描画プリミティブ                                                      */
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

static void DrawRRect(float L, float T, float R, float B, float r, const Style *s)
{
    float cx = (L + R) * 0.5f, cy = (T + B) * 0.5f, hw = (R - L) * 0.5f, hh = (B - T) * 0.5f;
    int x0 = ClampI((int)L - 2, 0, g_W), x1 = ClampI((int)R + 3, 0, g_W);
    int y0 = ClampI((int)T - 2, 0, g_H), y1 = ClampI((int)B + 3, 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
            float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r; /* 角丸矩形SDF */
            float outer = Clamp01(0.5f - d);
            if (outer <= 0)
                continue;
            float inner = (s->ba > 0) ? Clamp01(0.5f - (d + 1.0f * g_dpi)) : outer; /* 枠線なしなら全面を塗る */
            float af = s->fa * inner, ab = s->ba * (outer - inner);
            float a = af + ab;
            uint32_t A = (uint32_t)(a * 255.0f + 0.5f);
            uint32_t R8 = (uint32_t)(s->fr * af + s->br * ab + 0.5f);
            uint32_t G8 = (uint32_t)(s->fg * af + s->bg * ab + 0.5f);
            uint32_t B8 = (uint32_t)(s->fb * af + s->bb * ab + 0.5f);
            g_bits[y * g_W + x] = (A << 24) | (R8 << 16) | (G8 << 8) | B8;
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

static float LiftOf(const Item *it, DWORD now)
{
    if (!it->bounceStart || now - it->bounceStart >= BOUNCE_MS)
        return 0;
    /* 減衰しながら上下に揺れる (上へ大きく・下へ少し)。2往復で収まる */
    float t = (float)(now - it->bounceStart) / BOUNCE_MS;
    float v = sinf(t * 4.0f * 3.14159265f) * (1.0f - t);
    return v >= 0.0f ? g_bounce * v : g_bounce * 0.3f * v;
}

static void DrawItem(const Item *it, DWORD now)
{
    int dw = (int)(it->sz + 0.5f);
    if (dw < 4)
        return;
    float xc, yc;
    L2W(it->cx, g_baseline + it->sz * 0.5f + LiftOf(it, now), &xc, &yc);
    int dx0 = (int)floorf(xc - dw * 0.5f + 0.5f);
    int dy0 = (int)floorf(yc - dw * 0.5f + 0.5f);

    int lvl = 0;
    for (int l = MIP_LEVELS - 1; l >= 0; l--)
        if (g_mipSize[l] >= dw)
        {
            lvl = l;
            break;
        }
    const uint32_t *m = it->mip[lvl];
    int ms = g_mipSize[lvl];
    float k = (float)ms / dw;

    for (int y = 0; y < dw; y++)
    {
        int py = dy0 + y;
        if (py < 0 || py >= g_H)
            continue;
        float v = (y + 0.5f) * k - 0.5f;
        for (int x = 0; x < dw; x++)
        {
            int px = dx0 + x;
            if (px < 0 || px >= g_W)
                continue;
            uint32_t s = Sample(m, ms, (x + 0.5f) * k - 0.5f, v);
            uint32_t sa = s >> 24;
            if (!sa)
                continue;
            uint32_t *dp = &g_bits[py * g_W + px];
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

static void DrawDot(float cx, float cy, float r, float R, float G, float B, float alpha)
{
    int x0 = (int)(cx - r - 2), x1 = (int)(cx + r + 3), y0 = (int)(cy - r - 2), y1 = (int)(cy + r + 3);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = Clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
            BlendPx(x, y, R, G, B, cov * alpha);
        }
}

/* 起動中のアプリ: アイコンの画面端側に丸を1つ */
/* クリック起動から15秒間は「起動中」扱い (検出が遅いアプリ用) */
static BOOL IsPending(const Item *it)
{
    return it->launchTick && GetTickCount() - it->launchTick < 15000;
}
static BOOL AnyPending(void)
{
    for (int i = 0; i < g_count; i++)
        if (g_items[i].nwin == 0 && IsPending(&g_items[i]))
            return TRUE;
    return FALSE;
}

static void DrawIndicator(const Item *it)
{
    if (it->nwin <= 0 && !IsPending(it))
        return;
    float x, y;
    L2W(it->cx, g_margin + g_pady * 0.38f, &x, &y);
    DrawDot(x, y, 2.6f * g_dpi, 235.f, 235.f, 240.f, 0.95f);
}
/* ラベル (GDIで白文字をマスクに描き、自前で合成) */
static void BuildLabel(const wchar_t *text)
{
    if (g_lblMask && wcscmp(text, g_lblText) == 0)
        return;
    free(g_lblMask);
    g_lblMask = NULL;
    wcsncpy(g_lblText, text, 127);
    g_lblText[127] = 0;

    int len = (int)wcslen(g_lblText);
    HDC dc = CreateCompatibleDC(NULL);
    HFONT f = CreateFontW(-(int)(13 * g_dpi + 0.5f), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(dc, f);
    SIZE sz;
    GetTextExtentPoint32W(dc, g_lblText, len, &sz);
    int w = sz.cx + 2, h = sz.cy + 2;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP ob = (HBITMAP)SelectObject(dc, bm);
    memset(bits, 0, (size_t)w * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 1, 1, g_lblText, len);
    GdiFlush();

    g_lblMask = (uint8_t *)malloc((size_t)w * h);
    for (int i = 0; i < w * h; i++)
        g_lblMask[i] = (uint8_t)((((uint32_t *)bits)[i] >> 8) & 255);
    g_lblW = w;
    g_lblH = h;

    SelectObject(dc, ob);
    DeleteObject(bm);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteDC(dc);
}

static void DrawLabel(const Item *it, DWORD now)
{
    BuildLabel(it->name);
    float d = g_dpi;
    float pw = g_lblW + 20 * d, ph = g_lblH + 10 * d;
    float vo = g_baseline + it->sz + LiftOf(it, now) + 8 * d; /* アイコン先端の外側 */
    float u0, u1, v0, v1;
    if (!IsVert())
    { /* 横置き: アイコンの外側(上/下)に中央揃え */
        u0 = it->cx - pw * 0.5f;
        if (u0 > g_LW - pw - 2)
            u0 = g_LW - pw - 2;
        if (u0 < 2)
            u0 = 2;
        u1 = u0 + pw;
        v0 = vo;
        v1 = vo + ph;
    }
    else
    { /* 縦置き: アイコンの外側(右/左)に縦中央揃え */
        u0 = it->cx - ph * 0.5f;
        if (u0 > g_LW - ph - 2)
            u0 = g_LW - ph - 2;
        if (u0 < 2)
            u0 = 2;
        u1 = u0 + ph;
        v0 = vo;
        v1 = vo + pw;
    }
    float L, T, R, B;
    LRect(u0, v0, u1, v1, &L, &T, &R, &B);
    DrawRRect(L, T, R, B, ph * 0.5f, &g_labelStyle);

    int tx = (int)(L + 10 * d), ty = (int)(T + 5 * d);
    for (int y = 0; y < g_lblH; y++)
        for (int x = 0; x < g_lblW; x++)
        {
            uint8_t m = g_lblMask[y * g_lblW + x];
            if (m)
                BlendPx(tx + x, ty + y, 255, 255, 255, m / 255.0f);
        }
}

static void Render(void)
{
    memset(g_bits, 0, (size_t)g_W * g_H * 4);
    Layout();

    /* アルファ1のほぼ透明領域: 0だとマウスが貫通するためホバー検知用に塗る */
    float L, T, R, B;
    LRect(0, 0, (float)g_LW, g_hoverV, &L, &T, &R, &B);
    int hx0 = ClampI((int)floorf(L), 0, g_W), hx1 = ClampI((int)ceilf(R), 0, g_W);
    int hy0 = ClampI((int)floorf(T), 0, g_H), hy1 = ClampI((int)ceilf(B), 0, g_H);
    for (int y = hy0; y < hy1; y++)
        for (int x = hx0; x < hx1; x++)
            g_bits[y * g_W + x] = 0x01000000u;

    LRect(g_barL, g_barV0, g_barR, g_barV1, &L, &T, &R, &B);
    float r = 18.0f * g_dpi, hh = (g_barV1 - g_barV0) * 0.5f;
    DrawRRect(L, T, R, B, r < hh ? r : hh, &g_barStyle);

    DWORD now = GetTickCount();
    for (int i = 0; i < g_count; i++)
    {
        DrawItem(&g_items[i], now);
        if (i != g_dragOut)
            DrawIndicator(&g_items[i]);
    }

    if (g_hover && !g_dragged && g_slide < 0.5f)
    {
        int hit = HitTest(g_mouseU, g_mouseV);
        if (hit >= 0)
            DrawLabel(&g_items[hit], now);
    }

    int off = (int)(g_slide * g_hideDelta); /* 隠れる時は画面端の方向へスライド */
    POINT dst = {g_posX + (g_edge == 2 ? -off : (g_edge == 3 ? off : 0)),
                 g_posY0 + (g_edge == 0 ? off : (g_edge == 1 ? -off : 0))};
    POINT src = {0, 0};
    SIZE sz = {g_W, g_H};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, NULL, &dst, &sz, g_memdc, &src, 0, &bf, ULW_ALPHA);
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
/* 操作                                                                 */
/* ------------------------------------------------------------------ */
#define WM_LAUNCHFAIL (WM_APP + 1)
typedef struct
{
    wchar_t spec[MAX_PATH * 2];
    wchar_t path[MAX_PATH * 2];
} LaunchReq;

/* 起動の遅いアプリ (Discord 等) でドックが固まらないよう、別スレッドで起動する */
static DWORD WINAPI LaunchThread(LPVOID p)
{
    LaunchReq *rq = (LaunchReq *)p;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC; /* スレッドが先に終わっても起動処理を打ち切らせない */
    sei.lpVerb = L"open";
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

static void Launch(int idx)
{
    DWORD now = GetTickCount();
    if (g_items[idx].launchTick && now - g_items[idx].launchTick < 2000)
        return; /* 起動待ちの連打で二重起動しない */
    LaunchReq *rq = (LaunchReq *)malloc(sizeof(LaunchReq));
    if (!rq)
        return;
    wcscpy(rq->spec, g_items[idx].spec);
    ResolveSpec(g_items[idx].spec, rq->path, MAX_PATH * 2);
    HANDLE th = CreateThread(NULL, 0, LaunchThread, rq, 0, NULL);
    if (!th)
    {
        free(rq);
        MessageBeep(MB_ICONERROR);
        return;
    }
    CloseHandle(th);
    g_items[idx].launchTick = now;
    g_items[idx].bounceStart = now; /* 押した瞬間から揺らす (失敗したら止める) */
    EnsureTimer();
}

static void OnClickItem(int idx)
{
    Item *it = &g_items[idx];
    if (it->nwin > 0 && GetKeyState(VK_SHIFT) >= 0)
    {
        HWND fgw = GetForegroundWindow();
        BOOL active = FALSE;
        for (int i = 0; i < it->nwin; i++)
            if (it->wins[i] == fgw)
                active = TRUE;
        if (active)
        { /* 前面にある状態でもう一度クリック -> 最小化(収縮) */
            for (int i = 0; i < it->nwin; i++)
                ShowWindowAsync(it->wins[i], SW_MINIMIZE);
        }
        else
            ActivateItem(it); /* 裏・最小化中なら前面へ */
    }
    else
        Launch(idx); /* 未起動 -> 起動 */
}

static void RemovePinned(int idx)
{
    FreeItem(&g_items[idx]);
    memmove(&g_items[idx], &g_items[idx + 1], (g_count - idx - 1) * sizeof(Item));
    g_count--;
    SaveConfig();
    RefreshRunning(); /* 実行中なら未固定アプリとして残る */
    ResizeDock();
    Render();
}

static const int g_hideChoices[] = {300, 600, 1000, 2000, 3000, 5000};
#define N_HIDE_CHOICES ((int)(sizeof(g_hideChoices) / sizeof(g_hideChoices[0])))

static void ApplyLayoutChange(void)
{
    g_slide = 0.0f;
    SaveConfig();
    ResizeDock();
    Render();
}

static void ShowContextMenu(HWND hwnd, int hit)
{
    HMENU menu = CreatePopupMenu();
    if (hit >= 0)
    {
        Item *it = &g_items[hit];
        if (it->nwin > 0)
        {
            AppendMenuW(menu, MF_STRING, 3, L"ウィンドウを閉じる");
            AppendMenuW(menu, MF_STRING, 5, L"新しく起動");
        }
        if (it->pinned)
            AppendMenuW(menu, MF_STRING, 1, L"ドックから削除");
        else
            AppendMenuW(menu, MF_STRING, 4, L"ドックに固定");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }

    /* 設定 */
    BOOL vert = IsVert();
    HMENU set = CreatePopupMenu(), mo = CreatePopupMenu(), mp = CreatePopupMenu(), mt = CreatePopupMenu();
    AppendMenuW(mo, MF_STRING | (!vert ? MF_CHECKED : 0), 10, L"横");
    AppendMenuW(mo, MF_STRING | (vert ? MF_CHECKED : 0), 11, L"縦");
    AppendMenuW(mp, MF_STRING | (g_edge == 0 ? MF_CHECKED : 0) | (vert ? MF_GRAYED : 0), 12, L"下");
    AppendMenuW(mp, MF_STRING | (g_edge == 1 ? MF_CHECKED : 0) | (vert ? MF_GRAYED : 0), 13, L"上");
    AppendMenuW(mp, MF_STRING | (g_edge == 2 ? MF_CHECKED : 0) | (!vert ? MF_GRAYED : 0), 14, L"左");
    AppendMenuW(mp, MF_STRING | (g_edge == 3 ? MF_CHECKED : 0) | (!vert ? MF_GRAYED : 0), 15, L"右");
    AppendMenuW(mp, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mp, MF_STRING, 16, L"端の中央に戻す");
    AppendMenuW(mp, MF_STRING | MF_GRAYED, 0, L"Ctrl+ドラッグで自由に移動");
    for (int i = 0; i < N_HIDE_CHOICES; i++)
    {
        wchar_t b[32];
        swprintf(b, 32, L"%d.%d 秒", g_hideChoices[i] / 1000, (g_hideChoices[i] % 1000) / 100);
        AppendMenuW(mt, MF_STRING | (g_hideMs == g_hideChoices[i] ? MF_CHECKED : 0), 20 + i, b);
    }
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mo, L"向き");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mp, L"位置");
    AppendMenuW(set, MF_SEPARATOR, 0, NULL);
    AppendMenuW(set, MF_STRING | (g_autohide ? MF_CHECKED : 0), 6, L"自動的に隠す");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mt, L"隠れるまでの時間");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)set, L"設定");
    AppendMenuW(menu, MF_STRING, 2, L"ドックを終了");

    UINT flags = TPM_RETURNCMD;
    switch (g_edge)
    {
    case 0:
        flags |= TPM_BOTTOMALIGN | TPM_CENTERALIGN;
        break;
    case 1:
        flags |= TPM_TOPALIGN | TPM_CENTERALIGN;
        break;
    case 2:
        flags |= TPM_LEFTALIGN | TPM_VCENTERALIGN;
        break;
    default:
        flags |= TPM_RIGHTALIGN | TPM_VCENTERALIGN;
        break;
    }
    POINT pt;
    GetCursorPos(&pt);
    g_menuOpen = TRUE;
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, flags, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu); /* サブメニューも一緒に破棄される */
    PostMessage(hwnd, WM_NULL, 0, 0);
    g_menuOpen = FALSE;
    g_leaveTick = GetTickCount();

    switch (cmd)
    {
    case 1:
        if (hit >= 0)
            RemovePinned(hit);
        break;
    case 2:
        DestroyWindow(hwnd);
        return;
    case 3:
        if (hit >= 0)
            for (int i = 0; i < g_items[hit].nwin; i++)
                PostMessageW(g_items[hit].wins[i], WM_CLOSE, 0, 0);
        break;
    case 4:
        if (hit >= 0)
        {
            PinItem(hit);
            SaveConfig();
            Render();
        }
        break;
    case 5:
        if (hit >= 0)
            Launch(hit);
        break;
    case 6:
        g_autohide = !g_autohide;
        SaveConfig();
        break;
    case 10:
        if (g_edge >= 2)
        {
            g_edge = 0;
            ApplyLayoutChange();
        }
        break;
    case 11:
        if (g_edge < 2)
        {
            g_edge = 3;
            ApplyLayoutChange();
        }
        break;
    case 12:
    case 13:
    case 14:
    case 15:
        g_edge = cmd - 12;
        ApplyLayoutChange();
        break;
    case 16:
        g_frac = 0.5f;
        ApplyLayoutChange();
        break;
    default:
        if (cmd >= 20 && cmd < 20 + N_HIDE_CHOICES)
        {
            g_hideMs = g_hideChoices[cmd - 20];
            SaveConfig();
        }
        break;
    }
    EnsureTimer();
}

/* ------------------------------------------------------------------ */
/* ドラッグ中アイコンのゴースト (ドックの外へ引き出して削除)               */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK GhostProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static void DestroyGhost(void)
{
    if (g_ghost)
    {
        DestroyWindow(g_ghost);
        g_ghost = NULL;
    }
    if (g_ghostDC)
    {
        SelectObject(g_ghostDC, g_ghostOld);
        DeleteObject(g_ghostBmp);
        DeleteDC(g_ghostDC);
        g_ghostDC = NULL;
        g_ghostBmp = NULL;
        g_ghostBits = NULL;
    }
}

static void MoveGhost(void)
{
    if (!g_ghost)
        return;
    POINT pt;
    GetCursorPos(&pt);
    SetWindowPos(g_ghost, HWND_TOPMOST, pt.x - g_ghostAX, pt.y - g_ghostAY, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

static void CreateGhost(int idx)
{
    DestroyGhost();
    float d = g_dpi, gs = g_base;
    BuildLabel(L"ドックから削除");
    float pw = g_lblW + 20 * d, ph = g_lblH + 10 * d;
    int gw = (int)(fmaxf(gs, pw)) + 8, gh = (int)(ph + 8 * d + gs) + 8;

    HDC scr = GetDC(NULL);
    g_ghostDC = CreateCompatibleDC(scr);
    ReleaseDC(NULL, scr);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = gw;
    bi.bmiHeader.biHeight = -gh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_ghostBmp = CreateDIBSection(g_ghostDC, &bi, DIB_RGB_COLORS, (void **)&g_ghostBits, NULL, 0);
    g_ghostOld = (HBITMAP)SelectObject(g_ghostDC, g_ghostBmp);
    memset(g_ghostBits, 0, (size_t)gw * gh * 4);

    /* 描画プリミティブがグローバルのバッファを使うので、一時的に差し替える */
    uint32_t *sb = g_bits;
    int sW = g_W, sH = g_H, sEdge = g_edge;
    float sBase = g_baseline;
    g_bits = g_ghostBits;
    g_W = gw;
    g_H = gh;
    g_edge = 0;

    float L = (gw - pw) * 0.5f;
    DrawRRect(L, 4, L + pw, 4 + ph, ph * 0.5f, &g_labelStyle);
    int tx = (int)(L + 10 * d), ty = (int)(4 + 5 * d);
    for (int y = 0; y < g_lblH; y++)
        for (int x = 0; x < g_lblW; x++)
        {
            uint8_t m = g_lblMask[y * g_lblW + x];
            if (m)
                BlendPx(tx + x, ty + y, 255, 255, 255, m / 255.0f);
        }
    Item tmp = g_items[idx];
    tmp.cx = gw * 0.5f;
    tmp.sz = gs;
    tmp.bounceStart = 0;
    g_baseline = 4.0f;
    DrawItem(&tmp, 0);

    g_bits = sb;
    g_W = sW;
    g_H = sH;
    g_edge = sEdge;
    g_baseline = sBase;

    g_ghostAX = gw / 2;
    g_ghostAY = (int)(gh - 4 - gs * 0.5f);
    g_ghost = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                              L"PromDockGhost", L"", WS_POPUP, 0, 0, gw, gh, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_ghost)
        return;
    POINT pt;
    GetCursorPos(&pt);
    POINT dst = {pt.x - g_ghostAX, pt.y - g_ghostAY}, src = {0, 0};
    SIZE sz = {gw, gh};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 235, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_ghost, NULL, &dst, &sz, g_ghostDC, &src, 0, &bf, ULW_ALPHA);
    ShowWindow(g_ghost, SW_SHOWNOACTIVATE);
}

/* ------------------------------------------------------------------ */
/* OLEドロップターゲット (ファイルをドロップして追加)                      */
/* ------------------------------------------------------------------ */
static BOOL DropHasFiles(IDataObject *d)
{
    FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return IDataObject_QueryGetData(d, &fe) == S_OK;
}

static DWORD DropEffect(DWORD allowed)
{
    if (allowed & DROPEFFECT_LINK)
        return DROPEFFECT_LINK;
    if (allowed & DROPEFFECT_COPY)
        return DROPEFFECT_COPY;
    return DROPEFFECT_NONE;
}

static void DropUpdate(POINTL pt)
{
    POINT p = {pt.x, pt.y};
    ScreenToClient(g_hwnd, &p);
    W2L((float)p.x, (float)p.y, &g_mouseU, &g_mouseV);
    g_hover = TRUE;
    g_dropActive = TRUE;

    float c[MAX_ITEMS], s[MAX_ITEMS];
    float total = LayoutPass(c, s, 1, 0); /* 隙間なしの静止配置で判定 (ちらつき防止) */
    float off = (g_LW - total) * 0.5f;
    int np = NPinned(), k = 0;
    for (int i = 0; i < np; i++)
        if (off + c[i] < g_mouseU)
            k++;
    g_dropIdx = k;
    EnsureTimer();
}

static HRESULT STDMETHODCALLTYPE DT_QueryInterface(IDropTarget *t, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget))
    {
        *ppv = t;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DT_AddRef(IDropTarget *t)
{
    (void)t;
    return 1;
}
static ULONG STDMETHODCALLTYPE DT_Release(IDropTarget *t)
{
    (void)t;
    return 1;
}

static HRESULT STDMETHODCALLTYPE DT_DragEnter(IDropTarget *t, IDataObject *d, DWORD ks, POINTL pt, DWORD *eff)
{
    (void)t;
    (void)ks;
    g_dropOk = DropHasFiles(d);
    if (g_dropOk)
        DropUpdate(pt);
    *eff = g_dropOk ? DropEffect(*eff) : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DT_DragOver(IDropTarget *t, DWORD ks, POINTL pt, DWORD *eff)
{
    (void)t;
    (void)ks;
    if (g_dropOk)
        DropUpdate(pt);
    *eff = g_dropOk ? DropEffect(*eff) : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DT_DragLeave(IDropTarget *t)
{
    (void)t;
    g_dropActive = FALSE;
    g_dropOk = FALSE;
    g_hover = FALSE;
    g_leaveTick = GetTickCount();
    EnsureTimer();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DT_Drop(IDropTarget *t, IDataObject *d, DWORD ks, POINTL pt, DWORD *eff)
{
    (void)t;
    (void)ks;
    BOOL added = FALSE;
    if (g_dropOk)
    {
        DropUpdate(pt);
        FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm;
        memset(&sm, 0, sizeof(sm));
        if (SUCCEEDED(IDataObject_GetData(d, &fe, &sm)))
        {
            HDROP hd = (HDROP)sm.hGlobal;
            UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
            int at = g_dropIdx;
            for (UINT i = 0; i < n; i++)
            {
                wchar_t path[MAX_PATH * 2];
                if (DragQueryFileW(hd, i, path, MAX_PATH * 2) && AddItemAt(path, TRUE, at))
                {
                    added = TRUE;
                    at++;
                }
            }
            ReleaseStgMedium(&sm);
        }
    }
    g_dropActive = FALSE;
    g_dropOk = FALSE;
    g_dropOpen = 0.0f;
    g_hover = TRUE;
    g_leaveTick = GetTickCount(); /* カーソルはまだドック上 */
    if (!g_tracking)
    {
        TRACKMOUSEEVENT tm = {sizeof(tm), TME_LEAVE, g_hwnd, 0};
        TrackMouseEvent(&tm);
        g_tracking = TRUE;
    }
    if (added)
    {
        SaveConfig();
        RefreshRunning();
        ResizeDock();
    }
    Render();
    EnsureTimer();
    *eff = added ? DropEffect(*eff) : DROPEFFECT_NONE;
    return S_OK;
}

static IDropTargetVtbl g_dtVtbl = {
    DT_QueryInterface, DT_AddRef, DT_Release,
    DT_DragEnter, DT_DragOver, DT_DragLeave, DT_Drop};
static IDropTarget g_dropTarget = {&g_dtVtbl};

/* Ctrl+ドラッグ: カーソルに一番近い画面の端へ吸着して移動 */
static void MoveDockToCursor(void)
{
    POINT pt;
    GetCursorPos(&pt);
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    float dB = (float)(wa.bottom - pt.y), dT = (float)(pt.y - wa.top);
    float dL = (float)(pt.x - wa.left), dR = (float)(wa.right - pt.x);
    int e = 0;
    float best = dB;
    if (dT < best)
    {
        best = dT;
        e = 1;
    }
    if (dL < best)
    {
        best = dL;
        e = 2;
    }
    if (dR < best)
    {
        best = dR;
        e = 3;
    }
    float frac = (e >= 2) ? (float)(pt.y - wa.top) / (float)(wa.bottom - wa.top)
                          : (float)(pt.x - wa.left) / (float)(wa.right - wa.left);
    frac = Clamp01(frac);
    if (e != g_edge || fabsf(frac - g_frac) > 0.0005f)
    {
        g_edge = e;
        g_frac = frac;
        g_slide = 0.0f;
        ResizeDock();
        Render();
    }
}

static BOOL DragArmed(LPARAM lp)
{
    if (!g_armed)
    {
        int dx = (short)LOWORD(lp) - g_downPt.x, dy = (short)HIWORD(lp) - g_downPt.y;
        float th = 5.0f * g_dpi;
        if ((float)(dx * dx + dy * dy) >= th * th)
            g_armed = TRUE;
    }
    return g_armed;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (g_moving)
        {
            MoveDockToCursor();
            return 0;
        }
        W2L((float)(short)LOWORD(lp), (float)(short)HIWORD(lp), &g_mouseU, &g_mouseV);
        g_hover = TRUE;
        if (!g_tracking)
        {
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            g_tracking = TRUE;
        }
        if ((wp & MK_LBUTTON) && g_pressed >= 0 && g_items[g_pressed].pinned && DragArmed(lp))
        {
            /* ドックの上方向へ大きく外れたら「引き出し中」(離すと削除) */
            BOOL out = g_mouseV > g_barV1 + 40.0f * g_dpi;
            if (out && g_dragOut < 0)
            {
                g_dragOut = g_pressed;
                g_dragged = TRUE;
                CreateGhost(g_pressed);
            }
            else if (!out && g_dragOut >= 0)
            {
                g_dragOut = -1;
                DestroyGhost();
            }

            if (g_dragOut >= 0)
                MoveGhost();
            else
            { /* ドラッグ並べ替え */
                int hit = HitTest(g_mouseU, g_mouseV);
                if (hit >= 0 && hit != g_pressed && g_items[hit].pinned)
                {
                    while (g_pressed != hit)
                    {
                        int nx = g_pressed + (hit > g_pressed ? 1 : -1);
                        SwapItems(g_pressed, nx);
                        g_pressed = nx;
                    }
                    g_dragged = TRUE;
                }
            }
        }
        EnsureTimer();
        return 0;

    case WM_MOUSELEAVE:
        g_tracking = FALSE;
        g_hover = FALSE;
        g_leaveTick = GetTickCount();
        EnsureTimer();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_ANIM)
        {
            BOOL keep = Step();
            Render();
            if (!keep)
            {
                KillTimer(hwnd, TIMER_ANIM);
                g_animOn = FALSE;
            }
        }
        else if (wp == TIMER_POLL)
        {
            static int iconTry = 0;
            BOOL iconFixed = FALSE;
            if (HasMissingIcons() && (iconTry < 40 || iconTry % 8 == 0))
                iconFixed = RetryMissingIcons();
            if (HasMissingIcons())
                iconTry++;
            else
                iconTry = 0;
            int r = RefreshRunning();
            if (r == 2)
            {
                ResizeDock();
                Render();
            }
            else if (r == 1 || iconFixed || AnyPending()) /* ← ここが⑥の変更箇所 */
                Render();
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (GetKeyState(VK_CONTROL) < 0)
        { /* Ctrl+ドラッグ: ドックを移動 */
            g_moving = TRUE;
            g_pressed = -1;
            g_dragged = TRUE;
            SetCapture(hwnd);
            return 0;
        }
        g_pressed = HitLP(lp);
        g_dragged = FALSE;
        g_armed = FALSE;
        g_downPt.x = (short)LOWORD(lp);
        g_downPt.y = (short)HIWORD(lp);
        SetCapture(hwnd);
        return 0;

    case WM_LBUTTONUP:
    {
        if (g_moving)
        {
            g_moving = FALSE;
            g_dragged = FALSE;
            ReleaseCapture();
            SaveConfig();
            EnsureTimer();
            return 0;
        }
        int out = g_dragOut;
        g_dragOut = -1;
        ReleaseCapture();
        if (out >= 0)
        { /* ドックの外で離した -> 削除 */
            DestroyGhost();
            g_pressed = -1;
            g_dragged = FALSE;
            RemovePinned(out);
            EnsureTimer();
            return 0;
        }
        if (g_dragged)
        {
            SaveConfig();
        }
        else if (g_pressed >= 0 && g_pressed < g_count)
        { /* ほぼ動かさず離した -> クリック */
            int dx = (short)LOWORD(lp) - g_downPt.x, dy = (short)HIWORD(lp) - g_downPt.y;
            float th = 8.0f * g_dpi;
            if ((float)(dx * dx + dy * dy) <= th * th)
                OnClickItem(g_pressed);
        }
        g_pressed = -1;
        g_dragged = FALSE;
        g_armed = FALSE;
        EnsureTimer();
        return 0;
    }

    case WM_MBUTTONUP:
    {
        int hit = HitLP(lp);
        if (hit >= 0)
            Launch(hit);
        return 0;
    }

    case WM_RBUTTONUP:
        ShowContextMenu(hwnd, HitLP(lp));
        return 0;

    case WM_CAPTURECHANGED: /* ドラッグ中断 */
        if (g_moving)
        {
            g_moving = FALSE;
            g_dragged = FALSE;
            SaveConfig();
        }
        if (g_dragOut >= 0)
        {
            g_dragOut = -1;
            DestroyGhost();
            g_pressed = -1;
            g_dragged = FALSE;
            EnsureTimer();
        }
        return 0;

    case WM_DISPLAYCHANGE:
        ResizeDock();
        Render();
        return 0;

    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA)
        {
            ResizeDock();
            Render();
        }
        break;

    case WM_LAUNCHFAIL:
    { /* 起動失敗: 揺れを止めて警告音 */
        LaunchReq *rq = (LaunchReq *)lp;
        for (int i = 0; i < g_count; i++)
            if (_wcsicmp(g_items[i].spec, rq->spec) == 0)
            {
                g_items[i].bounceStart = 0;
                g_items[i].launchTick = 0;
            }
        free(rq);
        MessageBeep(MB_ICONERROR);
        return 0;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdLine, int nShow)
{
    (void)hPrev;
    (void)cmdLine;
    (void)nShow;

    SetProcessDPIAware();
    OleInitialize(NULL);

    HDC scr = GetDC(NULL);
    g_dpi = GetDeviceCaps(scr, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, scr);

    GetModuleFileNameW(NULL, g_cfgPath, MAX_PATH);
    wchar_t *slash = wcsrchr(g_cfgPath, L'\\');
    if (slash)
        wcscpy(slash + 1, L"promdock.txt");
    else
        wcscpy(g_cfgPath, L"promdock.txt");

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromDockWnd";
    RegisterClassExW(&wc);

    WNDCLASSEXW gc;
    memset(&gc, 0, sizeof(gc));
    gc.cbSize = sizeof(gc);
    gc.lpfnWndProc = GhostProc;
    gc.hInstance = hInst;
    gc.lpszClassName = L"PromDockGhost";
    RegisterClassExW(&gc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"PromDock", WS_POPUP, 0, 0, 100, 100, NULL, NULL, hInst, NULL);

    RegisterDragDrop(g_hwnd, &g_dropTarget);
    g_leaveTick = GetTickCount();
    LoadConfig();
    RefreshRunning();
    ResizeDock();
    Render();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(g_hwnd, TIMER_POLL, POLL_MS, NULL);
    if (g_autohide)
        EnsureTimer();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0)
    {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    for (int i = 0; i < g_count; i++)
        FreeItem(&g_items[i]);
    free(g_lblMask);
    DestroyGhost();
    OleUninitialize();
    return 0;
}