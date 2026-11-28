/*
 * notice.c - 通知バッジ (PromDock / 同じexeにビルド)
 *
 * 実行中アプリのタスクバー通知(未読数)を、ドックのアイコンの右上に赤いバッジで重ねて表示する。
 *
 * 仕組み:
 *   Windows には「他のアプリのタスクバーバッジ(未読数)」を読み取る公式APIがないため、次の2つで判定する。
 *     1) ウィンドウタイトルの未読数表記
 *          "(3) Discord" / "[12] LINE" / "Inbox (5) - ..." / "受信トレイ (5) - ..." など。
 *          ブラウザ上のWebアプリ (Gmail / Slack / X など) も、タイトルに数が出るものは拾える。
 *          同じアプリのウィンドウが複数あるときは合計する。99を超えたら「99+」。
 *     2) タスクバーの点滅 (FlashWindowEx)
 *          シェルフックで点滅の開始を受け取り、数が分からないアプリには数字なしの赤い点を出す。
 *          そのウィンドウが前面になるか閉じられると消える。
 *
 *   ※ ストアアプリ等が SetOverlayIcon で出すバッジ画像そのものは外部から読めないため、対象外です。
 *
 * 使い方 (promdock.c から):
 *   Notice_Init / Notice_Shutdown ........ 起動時 / 終了時
 *   Notice_Query(wins, n) ............... アプリ(のウィンドウ群)の通知状態を返す  >0:未読数 / -1:数なし(点滅) / 0:なし
 *   Notice_DrawBadge(...) ............... ドックのDIBへバッジを合成
 *   Notice_Enabled / Notice_SetEnabled .. 有効/無効 (setting.c の ドック > 動作 から切り替え)
 *   設定は exe と同じフォルダの notice.ini に保存 (enabled=1)
 */
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#endif

#ifndef HSHELL_WINDOWDESTROYED
#define HSHELL_WINDOWDESTROYED 2
#endif
#ifndef HSHELL_WINDOWACTIVATED
#define HSHELL_WINDOWACTIVATED 4
#endif
#ifndef HSHELL_REDRAW
#define HSHELL_REDRAW 6
#endif
#ifndef HSHELL_HIGHBIT
#define HSHELL_HIGHBIT 0x8000
#endif
#ifndef HSHELL_FLASH
#define HSHELL_FLASH (HSHELL_REDRAW | HSHELL_HIGHBIT)
#endif
#ifndef HSHELL_RUDEAPPACTIVATED
#define HSHELL_RUDEAPPACTIVATED (HSHELL_WINDOWACTIVATED | HSHELL_HIGHBIT)
#endif

#define MAX_FLASH 64
#define MAX_COUNT 9999
#define MASK_CACHE 12

static BOOL g_on = TRUE;
static BOOL g_loaded = FALSE;
static wchar_t g_ini[MAX_PATH];

static HWND g_hook = NULL; /* シェルフックを受けるだけの非表示ウィンドウ */
static UINT g_msgShell = 0;
static BOOL g_hooked = FALSE;

typedef struct
{
    HWND h;
    DWORD tick; /* 最後に点滅イベントを受けた時刻 */
} FlashRec;
static FlashRec g_fl[MAX_FLASH];

/* ------------------------------------------------------------------ */
/* 設定 (notice.ini)                                                    */
/* ------------------------------------------------------------------ */
static void EnsureLoaded(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    wchar_t *slash = wcsrchr(g_ini, L'\\');
    if (slash)
        wcscpy(slash + 1, L"notice.ini");
    else
        wcscpy(g_ini, L"notice.ini");
    g_on = GetPrivateProfileIntW(L"Notice", L"enabled", 1, g_ini) != 0;
}

BOOL Notice_Enabled(void)
{
    EnsureLoaded();
    return g_on;
}

void Notice_SetEnabled(BOOL on)
{
    EnsureLoaded();
    g_on = on ? TRUE : FALSE;
    WritePrivateProfileStringW(L"Notice", L"enabled", g_on ? L"1" : L"0", g_ini);
    if (!g_on)
        memset(g_fl, 0, sizeof(g_fl));
}

/* ------------------------------------------------------------------ */
/* 点滅の検出 (シェルフック)                                              */
/* ------------------------------------------------------------------ */
static void FlashAdd(HWND h)
{
    if (!h)
        return;
    int freeIdx = -1;
    for (int i = 0; i < MAX_FLASH; i++)
    {
        if (g_fl[i].h == h)
        {
            g_fl[i].tick = GetTickCount();
            return;
        }
        if (!g_fl[i].h && freeIdx < 0)
            freeIdx = i;
    }
    if (freeIdx < 0)
        freeIdx = 0; /* 満杯なら一番古い扱いで先頭を上書き */
    g_fl[freeIdx].h = h;
    g_fl[freeIdx].tick = GetTickCount();
}

static void FlashRemove(HWND h)
{
    for (int i = 0; i < MAX_FLASH; i++)
        if (g_fl[i].h == h)
            g_fl[i].h = NULL;
}

static BOOL FlashHas(HWND h)
{
    for (int i = 0; i < MAX_FLASH; i++)
        if (g_fl[i].h == h)
            return TRUE;
    return FALSE;
}

static LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msgShell && msg == g_msgShell)
    {
        HWND target = (HWND)lp;
        switch ((int)wp)
        {
        case HSHELL_FLASH: /* タスクバーのボタンが点滅した: lParam = ウィンドウ */
            if (g_on && target && target != GetForegroundWindow())
                FlashAdd(target);
            break;
        case HSHELL_WINDOWACTIVATED:
        case HSHELL_RUDEAPPACTIVATED: /* 前面になった = 見た扱い */
            FlashRemove(target);
            break;
        case HSHELL_WINDOWDESTROYED:
            FlashRemove(target);
            break;
        default:
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* タイトルから未読数を読む                                               */
/* ------------------------------------------------------------------ */
static int ParseDigitsClose(const wchar_t *p, wchar_t close)
{
    int n = 0, digits = 0;
    while (*p >= L'0' && *p <= L'9' && digits < 5)
    {
        n = n * 10 + (*p - L'0');
        p++;
        digits++;
    }
    if (digits == 0 || *p != close)
        return 0;
    return n > MAX_COUNT ? MAX_COUNT : n;
}

static int ParseTitle(const wchar_t *t)
{
    while (*t == L' ' || *t == L'\t' || *t == 0x2022 /* • */ || *t == L'*')
        t++;
    if (*t == L'(')
        return ParseDigitsClose(t + 1, L')'); /* "(3) Discord" */
    if (*t == L'[')
        return ParseDigitsClose(t + 1, L']'); /* "[12] LINE" */

    /* メール系: "Inbox (5) - user@example.com" / "受信トレイ (5) - ..." */
    static const wchar_t *const keys[] = {L"Inbox (", L"受信トレイ ("};
    for (int i = 0; i < 2; i++)
    {
        const wchar_t *k = wcsstr(t, keys[i]);
        if (k)
            return ParseDigitsClose(k + wcslen(keys[i]), L')');
    }
    return 0;
}

static BOOL GetTitle(HWND h, wchar_t *buf, int cch)
{
    DWORD_PTR r = 0;
    buf[0] = 0;
    /* 応答しないウィンドウでドックが固まらないよう、タイムアウト付きで取得 */
    if (!SendMessageTimeoutW(h, WM_GETTEXT, (WPARAM)cch, (LPARAM)buf, SMTO_ABORTIFHUNG | SMTO_BLOCK, 40, &r))
        return FALSE;
    buf[cch - 1] = 0;
    return TRUE;
}

/* 戻り値: >0 未読数 / -1 数は不明だが通知あり (点滅) / 0 なし */
int Notice_Query(const HWND *wins, int n)
{
    EnsureLoaded();
    if (!g_on || !wins || n <= 0)
        return 0;
    HWND fg = GetForegroundWindow();
    long total = 0;
    BOOL flash = FALSE;
    for (int i = 0; i < n; i++)
    {
        wchar_t t[256];
        if (GetTitle(wins[i], t, 256))
            total += ParseTitle(t);
        if (wins[i] != fg && FlashHas(wins[i]))
            flash = TRUE;
    }
    if (total > 0)
        return total > MAX_COUNT ? MAX_COUNT : (int)total;
    return flash ? -1 : 0;
}

/* ------------------------------------------------------------------ */
/* バッジの描画                                                          */
/* ------------------------------------------------------------------ */
typedef struct
{
    wchar_t txt[8];
    int px; /* フォントの高さ */
    int w, h;
    uint8_t *m; /* 白文字のカバレッジ (0..255) */
} MaskEnt;
static MaskEnt g_mask[MASK_CACHE];
static int g_maskNext = 0;

static MaskEnt *GetMask(const wchar_t *txt, int px)
{
    for (int i = 0; i < MASK_CACHE; i++)
        if (g_mask[i].m && g_mask[i].px == px && wcscmp(g_mask[i].txt, txt) == 0)
            return &g_mask[i];

    int len = (int)wcslen(txt);
    HDC dc = CreateCompatibleDC(NULL);
    HFONT f = CreateFontW(-px, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(dc, f);
    SIZE sz;
    GetTextExtentPoint32W(dc, txt, len, &sz);
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
    uint8_t *m = NULL;
    if (bm && bits)
    {
        HBITMAP ob = (HBITMAP)SelectObject(dc, bm);
        memset(bits, 0, (size_t)w * h * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutW(dc, 1, 1, txt, len);
        GdiFlush();
        m = (uint8_t *)malloc((size_t)w * h);
        if (m)
            for (int i = 0; i < w * h; i++)
                m[i] = (uint8_t)((((uint32_t *)bits)[i] >> 8) & 255);
        SelectObject(dc, ob);
    }
    if (bm)
        DeleteObject(bm);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteDC(dc);
    if (!m)
        return NULL;

    MaskEnt *e = &g_mask[g_maskNext];
    g_maskNext = (g_maskNext + 1) % MASK_CACHE;
    free(e->m);
    wcsncpy(e->txt, txt, 7);
    e->txt[7] = 0;
    e->px = px;
    e->w = w;
    e->h = h;
    e->m = m;
    return e;
}

static float Cl01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

/* premultiplied BGRA の上へ合成 */
static void Put(uint32_t *bits, int W, int H, int x, int y, float r, float g, float b, float a)
{
    if (x < 0 || y < 0 || x >= W || y >= H || a <= 0.0f)
        return;
    uint32_t *dp = &bits[y * W + x];
    uint32_t d = *dp;
    float inv = 1.0f - a;
    float oa = a * 255.0f + ((d >> 24) & 255) * inv;
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

/* 角丸(ピル)の塗り。幅 pw / 高さ ph、中心 (cx, cy)。grow だけ外側へ広げる */
static void FillPill(uint32_t *bits, int W, int H, float cx, float cy, float pw, float ph, float grow,
                     float r, float g, float b, float a)
{
    float hw = pw * 0.5f + grow, hh = ph * 0.5f + grow;
    float rad = hh;
    float straight = hw - rad; /* 左右の半円を除いた直線部の半長 */
    int x0 = (int)floorf(cx - hw - 1.0f), x1 = (int)ceilf(cx + hw + 1.0f);
    int y0 = (int)floorf(cy - hh - 1.0f), y1 = (int)ceilf(cy + hh + 1.0f);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = fabsf(x + 0.5f - cx) - straight;
            if (dx < 0.0f)
                dx = 0.0f;
            float dy = y + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy) - rad;
            float cov = Cl01(0.5f - dist);
            if (cov > 0.0f)
                Put(bits, W, H, x, y, r, g, b, a * cov);
        }
}

/*
 * アイコンの右上へバッジを描く。
 *   right, top ... アイコンの右上の角 (ウィンドウ座標)
 *   iconPx ....... アイコンの描画サイズ (バッジの大きさの基準)
 *   count ........ >0: 数字入り / <0: 数なしの赤い点
 */
void Notice_DrawBadge(uint32_t *bits, int W, int H, float right, float top, float iconPx, float dpi, int count)
{
    if (!bits || count == 0)
        return;
    float h = iconPx * 0.40f;
    if (h < 14.0f * dpi)
        h = 14.0f * dpi;
    if (h > 28.0f * dpi)
        h = 28.0f * dpi;

    const float R = 235.0f, G = 59.0f, B = 48.0f;
    float ring = 1.5f * dpi;

    if (count < 0)
    { /* 数なし: 小さな点 */
        float d = h * 0.62f;
        float cx = right - d * 0.35f, cy = top + d * 0.35f;
        FillPill(bits, W, H, cx, cy, d, d, ring, 0.0f, 0.0f, 0.0f, 0.30f);
        FillPill(bits, W, H, cx, cy, d, d, 0.0f, R, G, B, 1.0f);
        return;
    }

    wchar_t txt[8];
    if (count > 99)
        wcscpy(txt, L"99+");
    else
        _snwprintf(txt, 8, L"%d", count);
    txt[7] = 0;

    int px = (int)(h * 0.62f + 0.5f);
    if (px < 8)
        px = 8;
    MaskEnt *me = GetMask(txt, px);
    float tw = me ? (float)me->w : h * 0.6f;
    float pw = tw + h * 0.55f;
    if (pw < h)
        pw = h;

    float cx = right - pw * 0.5f + h * 0.25f; /* 角から少しはみ出させる */
    float cy = top + h * 0.5f - h * 0.25f;
    FillPill(bits, W, H, cx, cy, pw, h, ring, 0.0f, 0.0f, 0.0f, 0.30f); /* 縁の影 (どの背景でも見えるように) */
    FillPill(bits, W, H, cx, cy, pw, h, 0.0f, R, G, B, 1.0f);

    if (me)
    {
        int ox = (int)floorf(cx - me->w * 0.5f + 0.5f);
        int oy = (int)floorf(cy - me->h * 0.5f + 0.5f) - (int)(h * 0.03f);
        for (int y = 0; y < me->h; y++)
            for (int x = 0; x < me->w; x++)
            {
                uint8_t c = me->m[y * me->w + x];
                if (c)
                    Put(bits, W, H, ox + x, oy + y, 255.0f, 255.0f, 255.0f, c / 255.0f);
            }
    }
}

/* ------------------------------------------------------------------ */
/* 初期化 / 終了                                                         */
/* ------------------------------------------------------------------ */
void Notice_Init(HINSTANCE hInst)
{
    EnsureLoaded();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HookProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PromDockNoticeHook";
    RegisterClassExW(&wc);

    g_msgShell = RegisterWindowMessageW(L"SHELLHOOK");
    g_hook = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    if (g_hook)
        g_hooked = RegisterShellHookWindow(g_hook) ? TRUE : FALSE; /* 失敗してもタイトルの数字だけで動く */
}

void Notice_Shutdown(void)
{
    if (g_hook)
    {
        if (g_hooked)
            DeregisterShellHookWindow(g_hook);
        DestroyWindow(g_hook);
        g_hook = NULL;
        g_hooked = FALSE;
    }
    for (int i = 0; i < MASK_CACHE; i++)
    {
        free(g_mask[i].m);
        g_mask[i].m = NULL;
    }
    memset(g_fl, 0, sizeof(g_fl));
}