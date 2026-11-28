/*
 * searchbar.c - PromDock のデスクトップ検索バー (promdock.c / launcher.c / setting.c と同じ exe にビルド)
 *
 *     cl ... /Febuild\PromDock.exe promdock.c launcher.c setting.c searchbar.c /link /SUBSYSTEM:WINDOWS
 *
 * 機能:
 *   - 「ブラウザ」と「検索エンジン」を選んで追加すると、デスクトップに半透明の検索バーが置かれる
 *       ブラウザ  : 既定のブラウザ / Chrome / Edge / Firefox / Brave / Opera / Vivaldi / Cốc Cốc
 *       エンジン  : Google / DuckDuckGo / Bing / Cốc Cốc / Yahoo! JAPAN / Brave Search / Ecosia
 *   - 入力欄が空のときは「Search」とだけ薄く表示
 *   - Enter か右端の虫眼鏡ボタンで、選んだブラウザで検索結果を開く (複数追加も可能・最大 SB_MAX 個)
 *   - アプリ設定の「移動」を押すと、解除するまで検索バーをドラッグで好きな場所に動かせる
 *     解除は、設定の「移動を解除」か、移動中の検索バーを右クリック。通常時の右クリックはメニュー。位置は自動保存
 *   - 枠線・グラデーション・影のない薄い長方形。濃いチャコールの半透明 + 金色のカーソルだけ
 *   - 最前面ではなく最奥 (壁紙のすぐ上=デスクトップの上) に置く。他のウィンドウは検索バーの上に重なる
 *   - フォントは SB_FONTS の先頭から、インストールされている最初のものを使う (ヒラギノ丸ゴシック等があれば優先)
 *   - 日本語 IME 入力、カーソル移動、範囲選択、コピー/切り取り/貼り付け (Ctrl+C/X/V/A) に対応
 *   - 設定は exe と同じフォルダの searchbar.ini に保存
 *       [SearchBar]
 *       count=1
 *       bar0_engine=google
 *       bar0_browser=default
 *       bar0_x=800
 *       bar0_y=60
 *
 * 外部 (promdock.c / setting.c) から呼ぶ関数:
 *     SearchBar_Init(hInst) / SearchBar_Shutdown()          ... 起動時/終了時に1回
 *     SearchBar_Add / _Remove / _Count / _Describe          ... 設定ウィンドウの一覧操作
 *     SearchBar_BrowserCount / _BrowserLabel / _EngineCount / _EngineName ... 選択肢の取得
 *     SearchBar_MoveMode / SearchBar_SetMoveMode            ... 移動モード
 *   ※ Settings_Init() は SearchBar_Init() より先に走る (初回設定) ので、
 *      追加・削除などは初期化前に呼ばれても安全に動くようにしてある。
 */
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shellapi.h>
#include <imm.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

/* setting.c: 一覧・ボタンの表示を更新する */
void Settings_RefreshSearchBar(void);
/* setting.c: アプリ設定を開く */
void Settings_OpenPage(int page); /* 3: 検索バーのページ */

/* このファイルが公開する関数 */
void SearchBar_Init(HINSTANCE hInst);
void SearchBar_Shutdown(void);
int SearchBar_BrowserCount(void);
void SearchBar_BrowserLabel(int i, wchar_t *out, int cch);
int SearchBar_EngineCount(void);
const wchar_t *SearchBar_EngineName(int i);
int SearchBar_Count(void);
void SearchBar_Describe(int i, wchar_t *out, int cch);
BOOL SearchBar_Add(int browser, int engine);
void SearchBar_Remove(int index);
BOOL SearchBar_MoveMode(void);
void SearchBar_SetMoveMode(BOOL on);

#define SB_MAX 8
/* 使うフォントの優先順 (最初に見つかったものを使用)。Windows 標準の「Yu Gothic UI」は Light(細字)で使う。
   ヒラギノ丸ゴシックは Windows に入っていないため、入れてあれば最優先で使われる */
static const wchar_t *const SB_FONTS[] = {L"Hiragino Maru Gothic ProN", L"ヒラギノ丸ゴシック ProN", L"Kosugi Maru",
                                          L"M PLUS Rounded 1c", L"Yu Gothic UI", L"Meiryo UI", L"Segoe UI"};
#define SB_MAXTEXT 200
#define TIMER_CARET 1
#define TIMER_Z 2 /* 最奥の位置を保つための見張り */
#define POS_AUTO 0x7fffffff
#define MENU_MOVE 1
#define MENU_DELETE 2
#define MENU_SETTINGS 3

/* ------------------------------------------------------------------ */
/* 検索エンジン / ブラウザの一覧                                           */
/* ------------------------------------------------------------------ */
typedef struct
{
    const wchar_t *id, *name, *url; /* url の %s が検索語に置き換わる */
    int r, g, b;                    /* バッジ・フォーカス時のアクセント色 */
    const wchar_t *badge;           /* バッジに描く1文字 */
} Engine;

static const Engine g_engines[] = {
    {L"google", L"Google", L"https://www.google.com/search?q=%s", 66, 133, 244, L"G"},
    {L"duckduckgo", L"DuckDuckGo", L"https://duckduckgo.com/?q=%s", 222, 88, 51, L"D"},
    {L"bing", L"Bing", L"https://www.bing.com/search?q=%s", 18, 137, 215, L"B"},
    {L"coccoc", L"Cốc Cốc", L"https://coccoc.com/search?query=%s", 0, 166, 81, L"C"},
    {L"yahoojp", L"Yahoo! JAPAN", L"https://search.yahoo.co.jp/search?p=%s", 255, 0, 51, L"Y"},
    {L"brave", L"Brave Search", L"https://search.brave.com/search?q=%s", 251, 84, 43, L"B"},
    {L"ecosia", L"Ecosia", L"https://www.ecosia.org/search?q=%s", 54, 172, 60, L"E"},
};
#define N_ENGINES ((int)(sizeof(g_engines) / sizeof(g_engines[0])))

typedef struct
{
    const wchar_t *id, *name;
    const wchar_t *appExe;   /* レジストリ App Paths の名前 (NULL なら使わない) */
    const wchar_t *paths[5]; /* よくあるインストール先 (環境変数は展開される) */
} Browser;

static const Browser g_browsers[] = {
    {L"default", L"既定のブラウザ", NULL, {NULL}},
    {L"chrome", L"Google Chrome", L"chrome.exe", {L"%ProgramFiles%\\Google\\Chrome\\Application\\chrome.exe", L"%ProgramFiles(x86)%\\Google\\Chrome\\Application\\chrome.exe", L"%LocalAppData%\\Google\\Chrome\\Application\\chrome.exe", NULL}},
    {L"edge", L"Microsoft Edge", L"msedge.exe", {L"%ProgramFiles(x86)%\\Microsoft\\Edge\\Application\\msedge.exe", L"%ProgramFiles%\\Microsoft\\Edge\\Application\\msedge.exe", NULL}},
    {L"firefox", L"Mozilla Firefox", L"firefox.exe", {L"%ProgramFiles%\\Mozilla Firefox\\firefox.exe", L"%ProgramFiles(x86)%\\Mozilla Firefox\\firefox.exe", NULL}},
    {L"brave", L"Brave", L"brave.exe", {L"%ProgramFiles%\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", L"%ProgramFiles(x86)%\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", L"%LocalAppData%\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", NULL}},
    {L"opera", L"Opera", L"opera.exe", {L"%LocalAppData%\\Programs\\Opera\\opera.exe", L"%ProgramFiles%\\Opera\\opera.exe", L"%LocalAppData%\\Programs\\Opera GX\\opera.exe", NULL}},
    {L"vivaldi", L"Vivaldi", L"vivaldi.exe", {L"%LocalAppData%\\Vivaldi\\Application\\vivaldi.exe", L"%ProgramFiles%\\Vivaldi\\Application\\vivaldi.exe", NULL}},
    {L"coccoc", L"Cốc Cốc", NULL, {L"%ProgramFiles%\\CocCoc\\Browser\\Application\\browser.exe", L"%ProgramFiles(x86)%\\CocCoc\\Browser\\Application\\browser.exe", L"%LocalAppData%\\CocCoc\\Browser\\Application\\browser.exe", NULL}},
};
#define N_BROWSERS ((int)(sizeof(g_browsers) / sizeof(g_browsers[0])))

/* ------------------------------------------------------------------ */
/* 状態                                                                  */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint8_t *p; /* 白文字のアルファマスク */
    int w, h;
} Mask;

typedef struct Bar
{
    int engine, browser, x, y;
    HWND hwnd;
    HDC dc;
    HBITMAP dib, old;
    uint32_t *bits;
    wchar_t text[SB_MAXTEXT];
    int len, caret, anchor, scroll;
    BOOL focused, caretOn, hoverBtn, btnDown, dragSel, track, placing;
    Mask mText, mHold;
} Bar;

typedef struct
{
    float L, T, R, B, rad, cy;
    float bdCx, bdR;   /* 左のバッジ */
    float btnCx, btnR; /* 右の検索ボタン */
    float tx0, tx1;    /* 文字領域 */
} Geo;

static HINSTANCE g_hInst;
static BOOL g_ready, g_loaded, g_move;
static float g_dpi = 1.0f;
static wchar_t g_ini[MAX_PATH];
static Bar *g_bars[SB_MAX];
static int g_nbars;
static HFONT g_font;
static HDC g_mdc; /* 文字幅の計測用 (g_font を選択したまま) */
static int g_lineH;
static int g_M, g_PW, g_PH, g_WW, g_WH; /* 影の余白 / 検索バー本体 / ウィンドウ全体 */
static Mask g_moveHint;
static wchar_t g_browserPath[N_BROWSERS][MAX_PATH];
static BOOL g_browserFound[N_BROWSERS];

static int CALLBACK FontFoundProc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
    (void)lf;
    (void)tm;
    (void)type;
    *(BOOL *)lp = TRUE;
    return 0;
}

static const wchar_t *PickFontName(void)
{
    HDC dc = GetDC(NULL);
    const wchar_t *found = L"Segoe UI";
    for (int i = 0; i < (int)(sizeof(SB_FONTS) / sizeof(SB_FONTS[0])); i++)
    {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        wcsncpy(lf.lfFaceName, SB_FONTS[i], LF_FACESIZE - 1);
        BOOL ok = FALSE;
        EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)FontFoundProc, (LPARAM)&ok, 0);
        if (ok)
        {
            found = SB_FONTS[i];
            break;
        }
    }
    ReleaseDC(NULL, dc);
    return found;
}

static int S(int v) { return (int)(v * g_dpi + 0.5f); }
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ------------------------------------------------------------------ */
/* ブラウザの場所を探す                                                    */
/* ------------------------------------------------------------------ */
static BOOL FileExists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL RegAppPath(HKEY root, const wchar_t *exe, wchar_t *out, DWORD cch)
{
    wchar_t key[300];
    _snwprintf(key, 300, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", exe);
    key[299] = 0;
    HKEY k;
    if (RegOpenKeyExW(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return FALSE;
    wchar_t raw[MAX_PATH];
    DWORD type = 0, sz = (DWORD)sizeof(raw) - sizeof(wchar_t);
    LONG r = RegQueryValueExW(k, NULL, NULL, &type, (LPBYTE)raw, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return FALSE;
    raw[sz / sizeof(wchar_t)] = 0;
    wchar_t *s = raw;
    size_t n = wcslen(s);
    if (n >= 2 && s[0] == L'"' && s[n - 1] == L'"')
    {
        s[n - 1] = 0;
        s++;
    }
    ExpandEnvironmentStringsW(s, out, cch);
    out[cch - 1] = 0;
    return FileExists(out);
}

static BOOL FindBrowser(int bi, wchar_t *out, int cch)
{
    if (bi <= 0 || bi >= N_BROWSERS)
        return FALSE;
    if (!g_browserFound[bi])
    {
        const Browser *br = &g_browsers[bi];
        wchar_t path[MAX_PATH];
        BOOL ok = FALSE;
        for (int i = 0; br->paths[i] && !ok; i++)
        {
            ExpandEnvironmentStringsW(br->paths[i], path, MAX_PATH);
            ok = FileExists(path);
        }
        if (!ok && br->appExe)
            ok = RegAppPath(HKEY_LOCAL_MACHINE, br->appExe, path, MAX_PATH) ||
                 RegAppPath(HKEY_CURRENT_USER, br->appExe, path, MAX_PATH);
        if (!ok)
            return FALSE;
        wcscpy(g_browserPath[bi], path);
        g_browserFound[bi] = TRUE;
    }
    wcsncpy(out, g_browserPath[bi], cch - 1);
    out[cch - 1] = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 保存 / 読み込み (searchbar.ini)                                        */
/* ------------------------------------------------------------------ */
static int FindEngineId(const wchar_t *id)
{
    for (int i = 0; i < N_ENGINES; i++)
        if (_wcsicmp(g_engines[i].id, id) == 0)
            return i;
    return 0;
}

static int FindBrowserId(const wchar_t *id)
{
    for (int i = 0; i < N_BROWSERS; i++)
        if (_wcsicmp(g_browsers[i].id, id) == 0)
            return i;
    return 0;
}

static void SaveBars(void)
{
    wchar_t k[48], v[32];
    _snwprintf(v, 32, L"%d", g_nbars);
    WritePrivateProfileStringW(L"SearchBar", L"count", v, g_ini);
    for (int i = 0; i < g_nbars; i++)
    {
        Bar *b = g_bars[i];
        _snwprintf(k, 48, L"bar%d_engine", i);
        WritePrivateProfileStringW(L"SearchBar", k, g_engines[b->engine].id, g_ini);
        _snwprintf(k, 48, L"bar%d_browser", i);
        WritePrivateProfileStringW(L"SearchBar", k, g_browsers[b->browser].id, g_ini);
        _snwprintf(k, 48, L"bar%d_x", i);
        _snwprintf(v, 32, L"%d", b->x);
        WritePrivateProfileStringW(L"SearchBar", k, v, g_ini);
        _snwprintf(k, 48, L"bar%d_y", i);
        _snwprintf(v, 32, L"%d", b->y);
        WritePrivateProfileStringW(L"SearchBar", k, v, g_ini);
    }
    for (int i = g_nbars; i < SB_MAX; i++)
    { /* 削除した分の古い項目を消す */
        const wchar_t *suf[] = {L"engine", L"browser", L"x", L"y"};
        for (int s = 0; s < 4; s++)
        {
            _snwprintf(k, 48, L"bar%d_%s", i, suf[s]);
            WritePrivateProfileStringW(L"SearchBar", k, NULL, g_ini);
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
        wcscpy(slash + 1, L"searchbar.ini");
    else
        wcscpy(g_ini, L"searchbar.ini");

    int n = ClampI((int)GetPrivateProfileIntW(L"SearchBar", L"count", 0, g_ini), 0, SB_MAX);
    for (int i = 0; i < n; i++)
    {
        wchar_t k[48], v[64];
        Bar *b = (Bar *)calloc(1, sizeof(Bar));
        if (!b)
            break;
        _snwprintf(k, 48, L"bar%d_engine", i);
        GetPrivateProfileStringW(L"SearchBar", k, L"google", v, 64, g_ini);
        b->engine = FindEngineId(v);
        _snwprintf(k, 48, L"bar%d_browser", i);
        GetPrivateProfileStringW(L"SearchBar", k, L"default", v, 64, g_ini);
        b->browser = FindBrowserId(v);
        _snwprintf(k, 48, L"bar%d_x", i);
        b->x = (int)GetPrivateProfileIntW(L"SearchBar", k, POS_AUTO, g_ini);
        _snwprintf(k, 48, L"bar%d_y", i);
        b->y = (int)GetPrivateProfileIntW(L"SearchBar", k, POS_AUTO, g_ini);
        g_bars[g_nbars++] = b;
    }
}

/* ------------------------------------------------------------------ */
/* 描画プリミティブ (ウィンドウごとの premultiplied BGRA ビットマップへ)         */
/* ------------------------------------------------------------------ */
static void BlendPx(Bar *b, int x, int y, float r, float g, float bl, float a)
{
    if (x < 0 || y < 0 || x >= g_WW || y >= g_WH || a <= 0)
        return;
    uint32_t *dp = &b->bits[y * g_WW + x];
    uint32_t d = *dp;
    float inv = 1.0f - a;
    float oa = a * 255 + ((d >> 24) & 255) * inv;
    float orr = r * a + ((d >> 16) & 255) * inv;
    float og = g * a + ((d >> 8) & 255) * inv;
    float ob = bl * a + (d & 255) * inv;
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

static float RRectDist(float px, float py, float L, float T, float R, float B, float rad)
{
    float cx = (L + R) * 0.5f, cy = (T + B) * 0.5f, hw = (R - L) * 0.5f, hh = (B - T) * 0.5f;
    float qx = fabsf(px - cx) - hw + rad, qy = fabsf(py - cy) - hh + rad;
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;
}

/* 検索バー本体: 枠線・グラデーションなしの単色チャコール半透明。内側の影は抜いて透け感を出す */
static void DrawPill(Bar *b, const Geo *g, BOOL strong)
{
    int x0 = ClampI((int)g->L - 2, 0, g_WW), x1 = ClampI((int)g->R + 3, 0, g_WW);
    int y0 = ClampI((int)g->T - 2, 0, g_WH), y1 = ClampI((int)g->B + 3, 0, g_WH);
    float a = strong ? 0.86f : 0.76f;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float cov = Clamp01(0.5f - RRectDist(x + 0.5f, y + 0.5f, g->L, g->T, g->R, g->B, g->rad));
            if (cov <= 0)
                continue;
            uint32_t *dp = &b->bits[y * g_WW + x];
            uint32_t d = *dp, out = 0;
            for (int sh = 0; sh < 32; sh += 8)
                out |= (uint32_t)(((d >> sh) & 255) * (1.0f - cov)) << sh;
            *dp = out;
            BlendPx(b, x, y, 28, 28, 32, a * cov);
        }
}

static void RectClip(Bar *b, float fx0, float fy0, float fx1, float fy1, int cl, int cr, float r, float g, float bl,
                     float a)
{
    int x0 = (int)(fx0 + 0.5f), x1 = (int)(fx1 + 0.5f), y0 = (int)(fy0 + 0.5f), y1 = (int)(fy1 + 0.5f);
    if (x0 < cl)
        x0 = cl;
    if (x1 > cr)
        x1 = cr;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            BlendPx(b, x, y, r, g, bl, a);
}

static void BlitMask(Bar *b, const Mask *m, int dx, int dy, int cl, int cr, float r, float g, float bl, float alpha)
{
    if (!m->p)
        return;
    for (int y = 0; y < m->h; y++)
    {
        int py = dy + y;
        if (py < 0 || py >= g_WH)
            continue;
        for (int x = 0; x < m->w; x++)
        {
            int px = dx + x;
            if (px < cl || px >= cr)
                continue;
            uint8_t v = m->p[y * m->w + x];
            if (v)
                BlendPx(b, px, py, r, g, bl, v / 255.0f * alpha);
        }
    }
}

static float SegDist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = Clamp01((wx * vx + wy * vy) / (vx * vx + vy * vy));
    float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return sqrtf(dx * dx + dy * dy);
}

static void DrawMagnifier(Bar *b, float cx, float cy, float size, float alpha)
{
    float lx = cx - 0.08f * size, ly = cy - 0.08f * size;
    float r = 0.30f * size, th = fmaxf(1.6f, 0.09f * size);
    float hx0 = lx + r * 0.7071f, hy0 = ly + r * 0.7071f;
    float hx1 = lx + (r + 0.24f * size) * 0.7071f, hy1 = ly + (r + 0.24f * size) * 0.7071f;
    int x0 = (int)(cx - size), x1 = (int)(cx + size), y0 = (int)(cy - size), y1 = (int)(cy + size);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f, py = y + 0.5f;
            float dl = fabsf(sqrtf((px - lx) * (px - lx) + (py - ly) * (py - ly)) - r) - th * 0.5f;
            float dh = SegDist(px, py, hx0, hy0, hx1, hy1) - th * 0.5f;
            float cov = Clamp01(0.5f - fminf(dl, dh));
            if (cov > 0)
                BlendPx(b, x, y, 238, 238, 240, cov * alpha);
        }
}

/* ------------------------------------------------------------------ */
/* 文字: GDI で白文字をマスクに描いて合成 (ランチャーと同じ方式)               */
/* ------------------------------------------------------------------ */
static Mask MakeMask(HFONT font, const wchar_t *s, int n, int extra)
{
    Mask m = {NULL, 0, 0};
    if (n <= 0)
        return m;
    HDC dc = CreateCompatibleDC(NULL);
    HFONT of = (HFONT)SelectObject(dc, font);
    SIZE sz;
    TEXTMETRICW tm;
    SetTextCharacterExtra(dc, extra); /* 字間 (プレースホルダーを上品に) */
    GetTextExtentPoint32W(dc, s, n, &sz);
    GetTextMetricsW(dc, &tm);
    int w = ClampI(sz.cx + 4, 1, 8192), h = tm.tmHeight + 2;

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
    if (bm && bits)
    {
        HBITMAP ob = (HBITMAP)SelectObject(dc, bm);
        memset(bits, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 1, 1, s, n);
        GdiFlush();
        m.p = (uint8_t *)malloc((size_t)w * h);
        if (m.p)
        {
            for (int i = 0; i < w * h; i++)
                m.p[i] = (uint8_t)((((uint32_t *)bits)[i] >> 8) & 255);
            m.w = w;
            m.h = h;
        }
        SelectObject(dc, ob);
    }
    if (bm)
        DeleteObject(bm);
    SelectObject(dc, of);
    DeleteDC(dc);
    return m;
}

static void FreeMask(Mask *m)
{
    free(m->p);
    m->p = NULL;
    m->w = m->h = 0;
}

/* 各文字の右端までの幅 pre[0..n] (pre[0]=0) */
static void MeasureText(const wchar_t *s, int n, int *pre)
{
    pre[0] = 0;
    if (n <= 0)
        return;
    int dx[SB_MAXTEXT], fit = 0;
    SIZE sz;
    if (!GetTextExtentExPointW(g_mdc, s, n, 0x7fffffff, &fit, dx, &sz))
    {
        for (int i = 0; i < n; i++)
            pre[i + 1] = (i + 1) * S(9);
        return;
    }
    for (int i = 0; i < n; i++)
        pre[i + 1] = dx[i];
}

/* ------------------------------------------------------------------ */
/* レイアウト                                                            */
/* ------------------------------------------------------------------ */
static void GetGeo(Geo *g)
{
    float d = g_dpi, M = (float)g_M;
    g->L = M;
    g->T = M;
    g->R = M + g_PW;
    g->B = M + g_PH;
    g->rad = 1.5f * d; /* ほぼ直角の長方形 */
    g->cy = M + g_PH * 0.5f;
    g->bdCx = g->L;
    g->bdR = 0;
    g->btnR = 12.0f * d;
    g->btnCx = g->R - 18.0f * d;
    g->tx0 = g->L + 16.0f * d;
    g->tx1 = g->btnCx - g->btnR - 4.0f * d;
}

static BOOL InBtn(const Geo *g, float x, float y)
{
    float dx = x - g->btnCx, dy = y - g->cy, r = g->btnR + 3 * g_dpi;
    return dx * dx + dy * dy <= r * r;
}

static BOOL InPill(const Geo *g, float x, float y)
{
    return x >= g->L && x <= g->R && y >= g->T && y <= g->B;
}

static int HitCaret(Bar *b, float x)
{
    Geo g;
    GetGeo(&g);
    int pre[SB_MAXTEXT + 1];
    MeasureText(b->text, b->len, pre);
    float rel = x - g.tx0 + b->scroll;
    int i;
    for (i = 0; i < b->len; i++)
        if (rel < (pre[i] + pre[i + 1]) * 0.5f)
            break;
    return i;
}

/* ------------------------------------------------------------------ */
/* 描画                                                                  */
/* ------------------------------------------------------------------ */
static void BarRender(Bar *b)
{
    if (!b->bits || !b->hwnd)
        return;
    float d = g_dpi;
    const float ar = 214, ag = 184, ab = 132; /* 控えめなシャンパンゴールド (カーソル・選択のみ) */
    Geo g;
    GetGeo(&g);
    memset(b->bits, 0, (size_t)g_WW * g_WH * 4);

    DrawPill(b, &g, b->focused || g_move);

    /* 文字領域 */
    int tl = (int)g.tx0, tr = (int)g.tx1;
    int pre[SB_MAXTEXT + 1];
    MeasureText(b->text, b->len, pre);
    if (g_move)
        BlitMask(b, &g_moveHint, tl - 1, (int)(g.cy - g_moveHint.h * 0.5f + 0.5f), tl, tr, ar, ag, ab, 0.95f);
    else if (b->len == 0)
        BlitMask(b, &b->mHold, tl - 1, (int)(g.cy - b->mHold.h * 0.5f + 0.5f), tl, tr, 255, 255, 255, 0.40f);
    else
    {
        if (b->anchor != b->caret)
        {
            int lo = b->anchor < b->caret ? b->anchor : b->caret, hi = b->anchor < b->caret ? b->caret : b->anchor;
            RectClip(b, g.tx0 - b->scroll + pre[lo], g.cy - g_lineH * 0.5f, g.tx0 - b->scroll + pre[hi],
                     g.cy + g_lineH * 0.5f, tl, tr, ar, ag, ab, 0.32f);
        }
        BlitMask(b, &b->mText, tl - b->scroll - 1, (int)(g.cy - b->mText.h * 0.5f + 0.5f), tl, tr, 238, 238, 240, 0.94f);
    }
    if (!g_move && b->focused && b->caretOn)
    {
        float cx = g.tx0 - b->scroll + pre[b->caret];
        float cw = fmaxf(1.0f, 1.5f * d);
        RectClip(b, cx, g.cy - g_lineH * 0.5f + 1, cx + cw, g.cy + g_lineH * 0.5f - 1, tl - 1, tr + 2, ar, ag, ab, 0.95f);
    }

    /* 右端: 虫眼鏡だけ (背景の丸なし)。入力があると明るくなる */
    BOOL active = b->len > 0 && !g_move;
    float a = g_move ? 0.18f : (active ? (b->btnDown ? 0.70f : (b->hoverBtn ? 1.0f : 0.92f)) : (b->hoverBtn ? 0.75f : 0.45f));
    DrawMagnifier(b, g.btnCx, g.cy, g.btnR * 2 * 0.60f, a);

    POINT src = {0, 0};
    SIZE sz = {g_WW, g_WH};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(b->hwnd, NULL, NULL, &sz, b->dc, &src, 0, &bf, ULW_ALPHA);
}

/* 文字・カーソルが変わったとき: マスク再生成 + カーソルが見えるようスクロール + 再描画 */
static void BarUpdate(Bar *b, BOOL remask)
{
    if (remask)
    {
        FreeMask(&b->mText);
        b->mText = MakeMask(g_font, b->text, b->len, 0);
    }
    Geo g;
    GetGeo(&g);
    int pre[SB_MAXTEXT + 1];
    MeasureText(b->text, b->len, pre);
    int vis = (int)(g.tx1 - g.tx0), total = pre[b->len], cp = pre[b->caret];
    if (total <= vis)
        b->scroll = 0;
    else
    {
        if (cp - b->scroll > vis - 2)
            b->scroll = cp - vis + 2;
        if (cp - b->scroll < 0)
            b->scroll = cp;
        b->scroll = ClampI(b->scroll, 0, total - vis + 2);
    }
    b->caretOn = TRUE;
    BarRender(b);
}

static void BarRebuildStatic(Bar *b)
{
    FreeMask(&b->mHold);
    b->mHold = MakeMask(g_font, L"Search", 6, S(1));
}

/* ------------------------------------------------------------------ */
/* 編集操作                                                              */
/* ------------------------------------------------------------------ */
static BOOL DeleteSel(Bar *b)
{
    if (b->anchor == b->caret)
        return FALSE;
    int lo = b->anchor < b->caret ? b->anchor : b->caret, hi = b->anchor < b->caret ? b->caret : b->anchor;
    memmove(&b->text[lo], &b->text[hi], (b->len - hi + 1) * sizeof(wchar_t));
    b->len -= hi - lo;
    b->caret = b->anchor = lo;
    return TRUE;
}

static void InsertText(Bar *b, const wchar_t *s, int n)
{
    DeleteSel(b);
    if (n > SB_MAXTEXT - 1 - b->len)
        n = SB_MAXTEXT - 1 - b->len;
    if (n <= 0)
        return;
    memmove(&b->text[b->caret + n], &b->text[b->caret], (b->len - b->caret + 1) * sizeof(wchar_t));
    memcpy(&b->text[b->caret], s, n * sizeof(wchar_t));
    b->len += n;
    b->caret += n;
    b->anchor = b->caret;
}

static void ClearText(Bar *b)
{
    b->text[0] = 0;
    b->len = b->caret = b->anchor = b->scroll = 0;
}

static void CopySel(Bar *b, HWND hwnd)
{
    if (b->anchor == b->caret)
        return;
    int lo = b->anchor < b->caret ? b->anchor : b->caret, hi = b->anchor < b->caret ? b->caret : b->anchor;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (hi - lo + 1) * sizeof(wchar_t));
    if (!h)
        return;
    wchar_t *p = (wchar_t *)GlobalLock(h);
    memcpy(p, &b->text[lo], (hi - lo) * sizeof(wchar_t));
    p[hi - lo] = 0;
    GlobalUnlock(h);
    if (OpenClipboard(hwnd))
    {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, h))
            GlobalFree(h);
        CloseClipboard();
    }
    else
        GlobalFree(h);
}

static void PasteText(Bar *b, HWND hwnd)
{
    if (!OpenClipboard(hwnd))
        return;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h)
    {
        const wchar_t *p = (const wchar_t *)GlobalLock(h);
        if (p)
        {
            wchar_t tmp[SB_MAXTEXT];
            int n = 0;
            for (; *p && n < SB_MAXTEXT - 1; p++)
                tmp[n++] = (*p == L'\r' || *p == L'\n' || *p == L'\t') ? L' ' : *p;
            InsertText(b, tmp, n);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

/* ------------------------------------------------------------------ */
/* 検索の実行                                                            */
/* ------------------------------------------------------------------ */
static wchar_t *BuildUrl(const Engine *e, const wchar_t *q, int qn)
{
    char u8[SB_MAXTEXT * 4 + 4];
    int n = WideCharToMultiByte(CP_UTF8, 0, q, qn, u8, (int)sizeof(u8) - 1, NULL, NULL);
    if (n <= 0)
        return NULL;
    wchar_t *enc = (wchar_t *)malloc((n * 3 + 1) * sizeof(wchar_t));
    if (!enc)
        return NULL;
    static const wchar_t hex[] = L"0123456789ABCDEF";
    int o = 0;
    for (int i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)u8[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
            c == '.' || c == '~')
            enc[o++] = c;
        else if (c == ' ')
            enc[o++] = L'+';
        else
        {
            enc[o++] = L'%';
            enc[o++] = hex[c >> 4];
            enc[o++] = hex[c & 15];
        }
    }
    enc[o] = 0;

    const wchar_t *ph = wcsstr(e->url, L"%s");
    size_t pre = ph ? (size_t)(ph - e->url) : wcslen(e->url);
    wchar_t *url = (wchar_t *)malloc((wcslen(e->url) + o + 1) * sizeof(wchar_t));
    if (url)
    {
        wcsncpy(url, e->url, pre);
        url[pre] = 0;
        wcscat(url, enc);
        if (ph)
            wcscat(url, ph + 2);
    }
    free(enc);
    return url;
}

static void DoSearch(Bar *b)
{
    int s = 0, e = b->len;
    while (s < e && b->text[s] == L' ')
        s++;
    while (e > s && b->text[e - 1] == L' ')
        e--;
    if (e <= s)
        return;
    wchar_t *url = BuildUrl(&g_engines[b->engine], b->text + s, e - s);
    if (!url)
        return;
    wchar_t exe[MAX_PATH];
    BOOL done = FALSE;
    if (FindBrowser(b->browser, exe, MAX_PATH))
    {
        size_t cch = wcslen(url) + 4;
        wchar_t *args = (wchar_t *)malloc(cch * sizeof(wchar_t));
        if (args)
        {
            _snwprintf(args, cch, L"\"%s\"", url);
            args[cch - 1] = 0;
            done = (INT_PTR)ShellExecuteW(NULL, L"open", exe, args, NULL, SW_SHOWNORMAL) > 32;
            free(args);
        }
    }
    if (!done) /* 既定のブラウザ、または選んだブラウザが見つからないとき */
        ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    free(url);
    ClearText(b);
    BarUpdate(b, TRUE);
}

/* ------------------------------------------------------------------ */
/* ウィンドウ                                                            */
/* ------------------------------------------------------------------ */
/* ---- 最奥への配置: デスクトップ本体 (Progman / WorkerW) の直上 ---- */
static BOOL ClassIs(HWND w, const wchar_t *name)
{
    wchar_t c[32];
    return GetClassNameW(w, c, 32) && wcscmp(c, name) == 0;
}

static BOOL IsDesktopHost(HWND w) { return ClassIs(w, L"Progman") || ClassIs(w, L"WorkerW"); }

static BOOL AtDesktopLevel(HWND h)
{
    HWND w = GetWindow(h, GW_HWNDNEXT);
    while (w && (ClassIs(w, L"PromSearchBarWnd") || ClassIs(w, L"PromPhotoFrameWnd") ||
                 ClassIs(w, L"PromClockWnd"))) /* 他の検索バー・画像・時計は飛ばす */
        w = GetWindow(w, GW_HWNDNEXT);
    return w && IsDesktopHost(w);
}

static void ZPlace(Bar *b)
{
    if (!b->hwnd || AtDesktopLevel(b->hwnd))
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
    b->placing = TRUE;
    SetWindowPos(b->hwnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    b->placing = FALSE;
}

static Bar *BarOf(HWND h) { return (Bar *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static void PlaceIme(Bar *b)
{
    HIMC himc = ImmGetContext(b->hwnd);
    if (!himc)
        return;
    Geo g;
    GetGeo(&g);
    int pre[SB_MAXTEXT + 1];
    MeasureText(b->text, b->len, pre);
    COMPOSITIONFORM cf;
    memset(&cf, 0, sizeof(cf));
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)(g.tx0 - b->scroll + pre[b->caret]);
    cf.ptCurrentPos.y = (LONG)(g.cy - g_lineH * 0.5f);
    ImmSetCompositionWindow(himc, &cf);
    LOGFONTW lf;
    if (GetObjectW(g_font, sizeof(lf), &lf))
        ImmSetCompositionFontW(himc, &lf);
    ImmReleaseContext(b->hwnd, himc);
}

static int BarIndex(Bar *b)
{
    for (int i = 0; i < g_nbars; i++)
        if (g_bars[i] == b)
            return i;
    return -1;
}

static void ShowBarMenu(Bar *b)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MENU_MOVE, L"移動する (解除は右クリック)");
    AppendMenuW(menu, MF_STRING, MENU_DELETE, L"この検索バーを削除");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"アプリ設定…");
    POINT pt;
    GetCursorPos(&pt);
    HWND h = b->hwnd;
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
    DestroyMenu(menu);
    PostMessageW(h, WM_NULL, 0, 0);
    if (cmd == MENU_MOVE)
        SearchBar_SetMoveMode(!g_move);
    else if (cmd == MENU_DELETE)
    {
        int i = BarIndex(b);
        if (i >= 0)
            SearchBar_Remove(i); /* b は破棄されるので、これ以降触らない */
    }
    else if (cmd == MENU_SETTINGS)
        Settings_OpenPage(3);
}

static LRESULT CALLBACK BarProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NCCREATE)
    {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    }
    Bar *b = BarOf(h);
    if (!b)
        return DefWindowProcW(h, m, w, l);

    switch (m)
    {
    case WM_WINDOWPOSCHANGING:
        if (!b->placing) /* クリックで手前に出てこないよう、自分以外からの重なり順の変更は無効にする */
            ((WINDOWPOS *)l)->flags |= SWP_NOZORDER;
        return 0;

    case WM_NCHITTEST:
        if (g_move)
            return HTCAPTION; /* 移動モード中は、どこを掴んでもドラッグで動かせる */
        break;

    case WM_SETCURSOR:
    {
        if (LOWORD(l) == HTCAPTION)
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            return TRUE;
        }
        if (LOWORD(l) == HTCLIENT)
        {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(h, &p);
            Geo g;
            GetGeo(&g);
            LPCWSTR c = IDC_ARROW;
            if (InBtn(&g, (float)p.x, (float)p.y))
                c = IDC_HAND;
            else if (InPill(&g, (float)p.x, (float)p.y) && p.x >= g.tx0 - 4)
                c = IDC_IBEAM;
            SetCursor(LoadCursor(NULL, c));
            return TRUE;
        }
        break;
    }

    case WM_EXITSIZEMOVE:
    { /* ドラッグ移動が終わったら位置を保存 */
        RECT rc;
        GetWindowRect(h, &rc);
        b->x = rc.left;
        b->y = rc.top;
        SaveBars();
        return 0;
    }

    case WM_SETFOCUS:
    {
        b->focused = TRUE;
        b->caretOn = TRUE;
        UINT bt = GetCaretBlinkTime();
        if (bt == 0 || bt == INFINITE)
            bt = 530;
        SetTimer(h, TIMER_CARET, bt, NULL);
        BarRender(b);
        return 0;
    }
    case WM_KILLFOCUS:
        b->focused = FALSE;
        b->btnDown = b->dragSel = FALSE;
        KillTimer(h, TIMER_CARET);
        BarRender(b);
        return 0;

    case WM_TIMER:
        if (w == TIMER_CARET)
        {
            b->caretOn = !b->caretOn;
            BarRender(b);
        }
        else if (w == TIMER_Z)
            ZPlace(b);
        return 0;

    case WM_MOUSEMOVE:
    {
        float x = (float)(short)LOWORD(l), y = (float)(short)HIWORD(l);
        Geo g;
        GetGeo(&g);
        BOOL hov = InBtn(&g, x, y);
        BOOL redraw = (hov != b->hoverBtn);
        b->hoverBtn = hov;
        if (!b->track)
        {
            TRACKMOUSEEVENT te;
            memset(&te, 0, sizeof(te));
            te.cbSize = sizeof(te);
            te.dwFlags = TME_LEAVE;
            te.hwndTrack = h;
            TrackMouseEvent(&te);
            b->track = TRUE;
        }
        if (b->dragSel)
        {
            int c = HitCaret(b, x);
            if (c != b->caret)
            {
                b->caret = c;
                BarUpdate(b, FALSE);
                redraw = FALSE;
            }
        }
        if (redraw)
            BarRender(b);
        return 0;
    }
    case WM_MOUSELEAVE:
        b->track = FALSE;
        if (b->hoverBtn)
        {
            b->hoverBtn = FALSE;
            BarRender(b);
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        if (g_move)
            return 0;
        float x = (float)(short)LOWORD(l), y = (float)(short)HIWORD(l);
        Geo g;
        GetGeo(&g);
        if (InBtn(&g, x, y))
        {
            b->btnDown = TRUE;
            SetCapture(h);
            BarRender(b);
        }
        else if (InPill(&g, x, y))
        {
            if (m == WM_LBUTTONDBLCLK)
            { /* ダブルクリックで全選択 */
                b->anchor = 0;
                b->caret = b->len;
            }
            else
            {
                b->caret = HitCaret(b, x);
                if (GetKeyState(VK_SHIFT) >= 0)
                    b->anchor = b->caret;
                b->dragSel = TRUE;
                SetCapture(h);
            }
            BarUpdate(b, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        float x = (float)(short)LOWORD(l), y = (float)(short)HIWORD(l);
        Geo g;
        GetGeo(&g);
        BOOL wasBtn = b->btnDown;
        b->btnDown = b->dragSel = FALSE;
        if (GetCapture() == h)
            ReleaseCapture();
        if (wasBtn && InBtn(&g, x, y))
            DoSearch(b);
        else
            BarRender(b);
        return 0;
    }

    case WM_NCRBUTTONUP: /* 移動モード中の右クリックで解除 (移動モード中は全面が HTCAPTION) */
        if (g_move)
        {
            SearchBar_SetMoveMode(FALSE);
            return 0;
        }
        break;
    case WM_NCRBUTTONDOWN:
        if (g_move)
            return 0;
        break;

    case WM_CONTEXTMENU:
        if (g_move)
        {
            SearchBar_SetMoveMode(FALSE);
            return 0;
        }
        ShowBarMenu(b);
        return 0;

    case WM_KEYDOWN:
    {
        if (g_move)
            return 0;
        BOOL shift = GetKeyState(VK_SHIFT) < 0;
        BOOL sel = b->anchor != b->caret;
        int lo = b->anchor < b->caret ? b->anchor : b->caret, hi = b->anchor < b->caret ? b->caret : b->anchor;
        switch (w)
        {
        case VK_RETURN:
            DoSearch(b);
            return 0;
        case VK_ESCAPE:
            if (b->len > 0)
            {
                ClearText(b);
                BarUpdate(b, TRUE);
            }
            else
                SetForegroundWindow(GetShellWindow());
            return 0;
        case VK_LEFT:
            if (!shift && sel)
                b->caret = lo;
            else if (b->caret > 0)
                b->caret--;
            if (!shift)
                b->anchor = b->caret;
            BarUpdate(b, FALSE);
            return 0;
        case VK_RIGHT:
            if (!shift && sel)
                b->caret = hi;
            else if (b->caret < b->len)
                b->caret++;
            if (!shift)
                b->anchor = b->caret;
            BarUpdate(b, FALSE);
            return 0;
        case VK_HOME:
            b->caret = 0;
            if (!shift)
                b->anchor = 0;
            BarUpdate(b, FALSE);
            return 0;
        case VK_END:
            b->caret = b->len;
            if (!shift)
                b->anchor = b->len;
            BarUpdate(b, FALSE);
            return 0;
        case VK_DELETE:
            if (!DeleteSel(b) && b->caret < b->len)
            {
                memmove(&b->text[b->caret], &b->text[b->caret + 1], (b->len - b->caret) * sizeof(wchar_t));
                b->len--;
            }
            BarUpdate(b, TRUE);
            return 0;
        }
        break;
    }

    case WM_CHAR:
    {
        if (g_move)
            return 0;
        wchar_t c = (wchar_t)w;
        switch (c)
        {
        case 8: /* Backspace */
            if (!DeleteSel(b) && b->caret > 0)
            {
                memmove(&b->text[b->caret - 1], &b->text[b->caret], (b->len - b->caret + 1) * sizeof(wchar_t));
                b->len--;
                b->caret--;
                b->anchor = b->caret;
            }
            BarUpdate(b, TRUE);
            return 0;
        case 1: /* Ctrl+A */
            b->anchor = 0;
            b->caret = b->len;
            BarUpdate(b, FALSE);
            return 0;
        case 3: /* Ctrl+C */
            CopySel(b, h);
            return 0;
        case 24: /* Ctrl+X */
            CopySel(b, h);
            DeleteSel(b);
            BarUpdate(b, TRUE);
            return 0;
        case 22: /* Ctrl+V */
            PasteText(b, h);
            BarUpdate(b, TRUE);
            return 0;
        default:
            if (c >= 32 && c != 127)
            {
                InsertText(b, &c, 1);
                BarUpdate(b, TRUE);
            }
            return 0;
        }
    }

    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
        PlaceIme(b); /* 変換中の文字が入力位置のそばに出るように */
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCDESTROY:
        b->hwnd = NULL;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* 作成 / 破棄                                                           */
/* ------------------------------------------------------------------ */
static void ClampToWork(int *x, int *y)
{
    RECT r = {*x, *y, *x + g_WW, *y + g_WH};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    *x = ClampI(*x, mi.rcWork.left - g_M, mi.rcWork.right - g_WW + g_M);
    *y = ClampI(*y, mi.rcWork.top - g_M, mi.rcWork.bottom - g_WH + g_M);
}

static void AutoPos(int idx, int *x, int *y)
{
    POINT o = {0, 0};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    *x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - g_WW) / 2;
    *y = mi.rcWork.top + S(60) + idx * (g_PH + S(16));
}

static void BarCreate(Bar *b, int idx)
{
    if (b->x == POS_AUTO || b->y == POS_AUTO)
        AutoPos(idx, &b->x, &b->y);
    ClampToWork(&b->x, &b->y);
    b->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"PromSearchBarWnd", L"PromSearchBar",
                              WS_POPUP, b->x, b->y, g_WW, g_WH, NULL, NULL, g_hInst, b);
    if (!b->hwnd)
        return;
    b->dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_WW;
    bi.bmiHeader.biHeight = -g_WH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    b->dib = CreateDIBSection(b->dc, &bi, DIB_RGB_COLORS, (void **)&b->bits, NULL, 0);
    if (!b->dib || !b->bits)
        return;
    b->old = (HBITMAP)SelectObject(b->dc, b->dib);
    BarRebuildStatic(b);
    BarRender(b);
    ZPlace(b);
    ShowWindow(b->hwnd, SW_SHOWNOACTIVATE);
    ZPlace(b);
    SetTimer(b->hwnd, TIMER_Z, 1000, NULL);
}

static void BarDestroy(Bar *b)
{
    if (b->hwnd)
        DestroyWindow(b->hwnd);
    if (b->dc)
    {
        if (b->dib)
        {
            SelectObject(b->dc, b->old);
            DeleteObject(b->dib);
        }
        DeleteDC(b->dc);
    }
    FreeMask(&b->mText);
    FreeMask(&b->mHold);
    free(b);
}

/* ------------------------------------------------------------------ */
/* 公開関数                                                              */
/* ------------------------------------------------------------------ */
int SearchBar_BrowserCount(void) { return N_BROWSERS; }
int SearchBar_EngineCount(void) { return N_ENGINES; }

const wchar_t *SearchBar_EngineName(int i)
{
    return (i >= 0 && i < N_ENGINES) ? g_engines[i].name : L"";
}

void SearchBar_BrowserLabel(int i, wchar_t *out, int cch)
{
    if (i < 0 || i >= N_BROWSERS)
    {
        out[0] = 0;
        return;
    }
    wchar_t tmp[MAX_PATH];
    BOOL missing = i > 0 && !FindBrowser(i, tmp, MAX_PATH);
    _snwprintf(out, cch, L"%s%s", g_browsers[i].name, missing ? L" (未検出)" : L"");
    out[cch - 1] = 0;
}

int SearchBar_Count(void)
{
    EnsureLoaded();
    return g_nbars;
}

void SearchBar_Describe(int i, wchar_t *out, int cch)
{
    EnsureLoaded();
    if (i < 0 || i >= g_nbars)
    {
        out[0] = 0;
        return;
    }
    _snwprintf(out, cch, L"%s  ·  %s", g_engines[g_bars[i]->engine].name, g_browsers[g_bars[i]->browser].name);
    out[cch - 1] = 0;
}

BOOL SearchBar_Add(int browser, int engine)
{
    EnsureLoaded();
    if (g_nbars >= SB_MAX)
        return FALSE;
    Bar *b = (Bar *)calloc(1, sizeof(Bar));
    if (!b)
        return FALSE;
    b->engine = ClampI(engine, 0, N_ENGINES - 1);
    b->browser = ClampI(browser, 0, N_BROWSERS - 1);
    b->x = b->y = POS_AUTO;
    g_bars[g_nbars++] = b;
    if (g_ready)
        BarCreate(b, g_nbars - 1);
    SaveBars();
    return TRUE;
}

void SearchBar_Remove(int index)
{
    EnsureLoaded();
    if (index < 0 || index >= g_nbars)
        return;
    Bar *b = g_bars[index];
    for (int i = index; i < g_nbars - 1; i++)
        g_bars[i] = g_bars[i + 1];
    g_bars[--g_nbars] = NULL;
    BarDestroy(b);
    if (g_nbars == 0)
        g_move = FALSE;
    SaveBars();
    Settings_RefreshSearchBar();
}

BOOL SearchBar_MoveMode(void) { return g_move; }

void SearchBar_SetMoveMode(BOOL on)
{
    EnsureLoaded();
    on = on && g_nbars > 0;
    if (on == g_move)
        return;
    g_move = on;
    for (int i = 0; i < g_nbars; i++)
        if (g_bars[i]->hwnd)
        {
            g_bars[i]->btnDown = g_bars[i]->dragSel = FALSE;
            BarRender(g_bars[i]);
        }
    if (!on)
        SaveBars();
    Settings_RefreshSearchBar();
}

void SearchBar_Init(HINSTANCE hInst)
{
    g_hInst = hInst;
    HDC scr = GetDC(NULL);
    g_dpi = GetDeviceCaps(scr, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, scr);
    EnsureLoaded();

    g_M = 0; /* 影がないので余白なし */
    g_PW = S(320);
    g_PH = S(34);
    g_WW = g_PW + g_M * 2;
    g_WH = g_PH + g_M * 2;

    g_font = CreateFontW(-S(14), 0, 0, 0, FW_LIGHT, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, PickFontName());
    g_mdc = CreateCompatibleDC(NULL);
    SelectObject(g_mdc, g_font);
    TEXTMETRICW tm;
    GetTextMetricsW(g_mdc, &tm);
    g_lineH = tm.tmHeight;
    g_moveHint = MakeMask(g_font, L"Drag · Right-click to lock", 26, S(1));

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = BarProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromSearchBarWnd";
    RegisterClassExW(&wc);

    g_ready = TRUE;
    for (int i = 0; i < g_nbars; i++)
        BarCreate(g_bars[i], i);
    SaveBars(); /* 自動配置した位置を記録 */
}

void SearchBar_Shutdown(void)
{
    g_ready = FALSE;
    for (int i = 0; i < g_nbars; i++)
    {
        BarDestroy(g_bars[i]);
        g_bars[i] = NULL;
    }
    g_nbars = 0;
    FreeMask(&g_moveHint);
    if (g_mdc)
        DeleteDC(g_mdc);
    if (g_font)
        DeleteObject(g_font);
    g_mdc = NULL;
    g_font = NULL;
}