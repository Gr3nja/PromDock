/*
 * clock.c - PromDock のデスクトップ時計ウィジェット (promdock.c / launcher.c / setting.c / searchbar.c / photoframe.c と同じ exe にビルド)
 *
 *     cl ... /Febuild\PromDock.exe promdock.c launcher.c setting.c searchbar.c photoframe.c clock.c /link /SUBSYSTEM:WINDOWS
 *
 * 機能:
 *   - デスクトップに半透明の時計を置く (アプリ設定の「時計」ページで表示のオン/オフ)
 *       スタイル : デジタル / アナログ
 *       表示     : 24時間表示 (オフで午前/午後の12時間表示) / 秒 / 日付
 *       見た目   : 大きさ (60〜200%) / 背景の濃さ (30〜100%)
 *   - 検索バー・画像と同じく、最前面ではなく最奥 (壁紙のすぐ上=デスクトップの上) に置く。他のウィンドウは時計の上に重なる
 *   - ドラッグで好きな場所に移動できる (アプリ設定でロックも可能)。位置は自動保存
 *   - 時計を右クリック: スタイル / 24時間 / 秒 / 日付 / ドラッグ移動 の切り替え、時計を消す、アプリ設定
 *   - 濃いチャコールの半透明 + 白い文字 + 控えめな金色 (AM/PM) の落ち着いた見た目。アナログの針 (秒針も) は白
 *   - スタイルごとにデザインを選べる (variant)
 *       デジタル : 0:カード / 1:細字(大・日付なし) / 2:縦並び / 3:分を小さく / 4:7セグ
 *       アナログ : 0:カード / 1:リング / 2:波形ライト / 3:波形ダーク / 4:波形白枠 / 5:石(明) / 6:石(暗)
 *   - 設定は exe と同じフォルダの clock.ini に保存
 *       [Clock]
 *       enabled=1
 *       style=0        (0:デジタル / 1:アナログ)
 *       variant_digital=0   (0〜4)
 *       variant_analog=0    (0〜6)
 *       h24=1
 *       seconds=1
 *       date=1
 *       size=100       (%)
 *       opacity=78     (%)
 *       movable=1
 *       x=1500
 *       y=40
 *
 * 外部 (promdock.c / setting.c) から呼ぶ関数:
 *     Clock_Init(hInst) / Clock_Shutdown()                ... 起動時/終了時に1回
 *     Clock_Enabled / _SetEnabled                         ... 表示のオン/オフ
 *     Clock_GetStyle / _SetStyle                          ... 0:デジタル 1:アナログ
 *     Clock_GetVariant / _SetVariant / _VariantCount      ... 今のスタイルのデザイン番号
 *     Clock_Get24h / _Set24h, _GetSeconds / _SetSeconds, _GetDate / _SetDate
 *     Clock_GetSize / _SetSize, _GetOpacity / _SetOpacity ... 大きさ(%) / 背景の濃さ(%)
 *     Clock_Movable / _SetMovable / _ResetPosition        ... 配置
 *   ※ Settings_Init() は Clock_Init() より先に走る (初回設定) ので、
 *      設定の変更は初期化前に呼ばれても安全に動くようにしてある。
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

/* setting.c */
void Settings_RefreshClock(void);
void Settings_OpenPage(int page); /* 5: 時計のページ */

/* このファイルが公開する関数 */
void Clock_Init(HINSTANCE hInst);
void Clock_Shutdown(void);
BOOL Clock_Enabled(void);
void Clock_SetEnabled(BOOL on);
int Clock_GetStyle(void);
void Clock_SetStyle(int style);
int Clock_GetVariant(void);
void Clock_SetVariant(int v);
int Clock_VariantCount(int style);
BOOL Clock_Get24h(void);
void Clock_Set24h(BOOL on);
BOOL Clock_GetSeconds(void);
void Clock_SetSeconds(BOOL on);
BOOL Clock_GetDate(void);
void Clock_SetDate(BOOL on);
int Clock_GetSize(void);
void Clock_SetSize(int pct);
int Clock_GetOpacity(void);
void Clock_SetOpacity(int pct);
BOOL Clock_Movable(void);
void Clock_SetMovable(BOOL on);
void Clock_ResetPosition(void);

/* 使うフォントの優先順 (最初に見つかったものを使用。検索バーと同じ) */
static const wchar_t *const CK_FONTS[] = {L"Hiragino Maru Gothic ProN", L"ヒラギノ丸ゴシック ProN", L"Kosugi Maru",
                                          L"M PLUS Rounded 1c", L"Yu Gothic UI", L"Meiryo UI", L"Segoe UI"};
#define TIMER_TICK 1 /* 時刻が変わったか見張る */
#define TIMER_Z 2    /* 最奥の位置を保つための見張り */
#define POS_AUTO 0x7fffffff
#define MENU_STYLE_DIGITAL 1
#define MENU_STYLE_ANALOG 2
#define MENU_24H 3
#define MENU_SECONDS 4
#define MENU_DATE 5
#define MENU_MOVABLE 6
#define MENU_HIDE 7
#define MENU_SETTINGS 8
#define MENU_VAR 100 /* 100+番号: デザイン */

/* ------------------------------------------------------------------ */
/* 状態                                                                  */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint8_t *p; /* 白文字のアルファマスク */
    int w, h;
    int top, bot; /* 実際に文字が描かれている最初 / 最後の行 */
} Mask;

static HINSTANCE g_hInst;
static BOOL g_ready, g_loaded;
static BOOL g_on, g_h24 = TRUE, g_sec = TRUE, g_date = TRUE, g_movable = TRUE;
static int g_style, g_size = 100, g_opa = 78, g_x = POS_AUTO, g_y = POS_AUTO;
static int g_vr[2]; /* スタイルごとのデザイン番号 (0:デジタル 1:アナログ) */
static wchar_t g_ini[MAX_PATH];
static float g_dpi = 1.0f;

static HWND g_hwnd;
static HDC g_dc;
static HBITMAP g_dib, g_old;
static uint32_t *g_bits;
static int g_W, g_H;
static BOOL g_placing;
static int g_lastKey = -1;

static HFONT g_fTime, g_fDate, g_fAp, g_fSub;
static HDC g_mdc;                        /* 文字幅の計測用 */
static Mask g_glyph[11];                 /* 時刻用の文字 0-9 と : (一度作って使い回す) */
static int g_adv[11], g_digitW, g_lineH; /* 文字の送り幅 / 数字の共通幅 / 行の高さ */
static Mask g_mDate, g_mAp, g_mSub;
static wchar_t g_dateStr[48], g_apStr[4], g_subStr[8];
static int g_digTop, g_digBot, g_subDigH; /* 数字の上端・下端 / 小さい数字の高さ */

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static int S(float v) { return (int)(v * g_dpi + 0.5f); }

/* ------------------------------------------------------------------ */
/* 保存 / 読み込み (clock.ini)                                            */
/* ------------------------------------------------------------------ */
static void SaveClock(void)
{
    wchar_t v[32];
#define PUT(key, val)                                        \
    do                                                       \
    {                                                        \
        _snwprintf(v, 32, L"%d", (int)(val));                \
        WritePrivateProfileStringW(L"Clock", key, v, g_ini); \
    } while (0)
    PUT(L"enabled", g_on);
    PUT(L"style", g_style);
    PUT(L"variant_digital", g_vr[0]);
    PUT(L"variant_analog", g_vr[1]);
    PUT(L"h24", g_h24);
    PUT(L"seconds", g_sec);
    PUT(L"date", g_date);
    PUT(L"size", g_size);
    PUT(L"opacity", g_opa);
    PUT(L"movable", g_movable);
    PUT(L"x", g_x);
    PUT(L"y", g_y);
#undef PUT
}

static void EnsureLoaded(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    wchar_t *slash = wcsrchr(g_ini, L'\\');
    if (slash)
        wcscpy(slash + 1, L"clock.ini");
    else
        wcscpy(g_ini, L"clock.ini");

    g_on = GetPrivateProfileIntW(L"Clock", L"enabled", 0, g_ini) != 0;
    g_style = ClampI((int)GetPrivateProfileIntW(L"Clock", L"style", 0, g_ini), 0, 1);
    g_vr[0] = ClampI((int)GetPrivateProfileIntW(L"Clock", L"variant_digital", 0, g_ini), 0, 4);
    g_vr[1] = ClampI((int)GetPrivateProfileIntW(L"Clock", L"variant_analog", 0, g_ini), 0, 6);
    g_h24 = GetPrivateProfileIntW(L"Clock", L"h24", 1, g_ini) != 0;
    g_sec = GetPrivateProfileIntW(L"Clock", L"seconds", 1, g_ini) != 0;
    g_date = GetPrivateProfileIntW(L"Clock", L"date", 1, g_ini) != 0;
    g_size = ClampI((int)GetPrivateProfileIntW(L"Clock", L"size", 100, g_ini), 60, 200);
    g_opa = ClampI((int)GetPrivateProfileIntW(L"Clock", L"opacity", 78, g_ini), 30, 100);
    g_movable = GetPrivateProfileIntW(L"Clock", L"movable", 1, g_ini) != 0;
    g_x = (int)GetPrivateProfileIntW(L"Clock", L"x", POS_AUTO, g_ini);
    g_y = (int)GetPrivateProfileIntW(L"Clock", L"y", POS_AUTO, g_ini);
}

/* ------------------------------------------------------------------ */
/* フォント / 文字 (GDI で白文字をマスクに描いて合成。検索バーと同じ方式)       */
/* ------------------------------------------------------------------ */
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
    for (int i = 0; i < (int)(sizeof(CK_FONTS) / sizeof(CK_FONTS[0])); i++)
    {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        wcsncpy(lf.lfFaceName, CK_FONTS[i], LF_FACESIZE - 1);
        BOOL ok = FALSE;
        EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)FontFoundProc, (LPARAM)&ok, 0);
        if (ok)
        {
            found = CK_FONTS[i];
            break;
        }
    }
    ReleaseDC(NULL, dc);
    return found;
}

static Mask MakeMask(HFONT font, const wchar_t *s, int n)
{
    Mask m = {NULL, 0, 0, 0, 0};
    if (n <= 0)
        return m;
    HDC dc = CreateCompatibleDC(NULL);
    HFONT of = (HFONT)SelectObject(dc, font);
    SIZE sz;
    TEXTMETRICW tm;
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
            m.top = h;
            m.bot = -1;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (m.p[y * w + x] > 48)
                    {
                        if (y < m.top)
                            m.top = y;
                        m.bot = y;
                    }
            if (m.bot < m.top)
            {
                m.top = 0;
                m.bot = h - 1;
            }
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
    m->w = m->h = m->top = m->bot = 0;
}

static void FreeFonts(void)
{
    for (int i = 0; i < 11; i++)
        FreeMask(&g_glyph[i]);
    FreeMask(&g_mDate);
    FreeMask(&g_mAp);
    FreeMask(&g_mSub);
    g_dateStr[0] = 0;
    g_apStr[0] = 0;
    g_subStr[0] = 0;
    if (g_mdc)
        SelectObject(g_mdc, GetStockObject(SYSTEM_FONT));
    if (g_fTime)
        DeleteObject(g_fTime);
    if (g_fDate)
        DeleteObject(g_fDate);
    if (g_fAp)
        DeleteObject(g_fAp);
    if (g_fSub)
        DeleteObject(g_fSub);
    g_fTime = g_fDate = g_fAp = g_fSub = NULL;
}

static HFONT MkFont(int px, int weight, const wchar_t *name)
{
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
}

static int TextW(HFONT f, const wchar_t *s)
{
    SIZE sz;
    SelectObject(g_mdc, f);
    GetTextExtentPoint32W(g_mdc, s, (int)wcslen(s), &sz);
    return sz.cx;
}

static int TextH(HFONT f)
{
    TEXTMETRICW tm;
    SelectObject(g_mdc, f);
    GetTextMetricsW(g_mdc, &tm);
    return tm.tmHeight;
}

/* 大きさ・スタイル・デザインが変わったとき: フォントと、時刻用の文字マスクを作り直す */
static void RebuildFonts(void)
{
    FreeFonts();
    float sc = g_size / 100.0f;
    const wchar_t *name = PickFontName();
    int var = g_vr[g_style == 1 ? 1 : 0];
    int tpx = 46, dpx = 15, apx = 14, spx = 0; /* 時刻 / 日付 / AM・PM / 小さい数字 の大きさ (論理px) */
    int tw = FW_LIGHT, dw = FW_LIGHT, sw = FW_LIGHT;
    if (g_style == 1)
    {
        dpx = 13;
        if (var >= 2)
        {
            dpx = 12;
            dw = FW_NORMAL;
        }
    }
    else if (var == 1) /* 細字(大) */
    {
        tpx = 66;
        dpx = 17;
        apx = 16;
    }
    else if (var == 2) /* 縦並び */
    {
        tpx = 56;
        spx = 56;
        sw = FW_SEMIBOLD;
        dpx = 16;
    }
    else if (var == 3) /* 分を小さく */
    {
        tpx = 84;
        spx = 38;
        dpx = 16;
    }
    g_fTime = MkFont(S(tpx * sc), tw, name);
    g_fDate = MkFont(S(dpx * sc), dw, name);
    g_fAp = MkFont(S(apx * sc), FW_LIGHT, name);
    if (spx)
        g_fSub = MkFont(S(spx * sc), sw, name);

    SelectObject(g_mdc, g_fTime);
    TEXTMETRICW tm;
    GetTextMetricsW(g_mdc, &tm);
    g_lineH = tm.tmHeight;
    g_digitW = 0;
    for (int i = 0; i < 11; i++)
    {
        wchar_t ch = i < 10 ? (wchar_t)(L'0' + i) : L':';
        SIZE sz;
        GetTextExtentPoint32W(g_mdc, &ch, 1, &sz);
        g_adv[i] = sz.cx;
        if (i < 10 && sz.cx > g_digitW)
            g_digitW = sz.cx;
        g_glyph[i] = MakeMask(g_fTime, &ch, 1);
    }
    g_digTop = g_glyph[0].top;
    g_digBot = g_glyph[0].bot;
    g_subDigH = 0;
    if (g_fSub)
    {
        Mask z = MakeMask(g_fSub, L"0", 1);
        g_subDigH = z.bot - z.top + 1;
        FreeMask(&z);
    }
}

/* ------------------------------------------------------------------ */
/* 描画プリミティブ (premultiplied BGRA ビットマップへ)                       */
/* ------------------------------------------------------------------ */
static void BlendPx(int x, int y, float r, float g, float bl, float a)
{
    if (x < 0 || y < 0 || x >= g_W || y >= g_H || a <= 0)
        return;
    uint32_t *dp = &g_bits[y * g_W + x];
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

/* 背景: 枠線・グラデーションなしの単色チャコール半透明 */
static void DrawBg(float rad, float alpha)
{
    for (int y = 0; y < g_H; y++)
        for (int x = 0; x < g_W; x++)
        {
            float cov = Clamp01(0.5f - RRectDist(x + 0.5f, y + 0.5f, 0, 0, (float)g_W, (float)g_H, rad));
            if (cov > 0)
                BlendPx(x, y, 28, 28, 32, alpha * cov);
        }
}

static float SegDist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 1e-6f ? Clamp01((wx * vx + wy * vy) / len2) : 0.0f;
    float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return sqrtf(dx * dx + dy * dy);
}

/* 丸い端の線 (秒針・目盛り・針) */
static void DrawSeg(float ax, float ay, float bx, float by, float th, float r, float g, float bl, float a)
{
    int x0 = (int)floorf(fminf(ax, bx) - th - 1), x1 = (int)ceilf(fmaxf(ax, bx) + th + 1);
    int y0 = (int)floorf(fminf(ay, by) - th - 1), y1 = (int)ceilf(fmaxf(ay, by) + th + 1);
    x0 = ClampI(x0, 0, g_W);
    x1 = ClampI(x1, 0, g_W);
    y0 = ClampI(y0, 0, g_H);
    y1 = ClampI(y1, 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float cov = Clamp01(0.5f - (SegDist(x + 0.5f, y + 0.5f, ax, ay, bx, by) - th * 0.5f));
            if (cov > 0)
                BlendPx(x, y, r, g, bl, cov * a);
        }
}

static void DrawDot(float cx, float cy, float rad, float r, float g, float bl, float a)
{
    int x0 = ClampI((int)floorf(cx - rad - 1), 0, g_W), x1 = ClampI((int)ceilf(cx + rad + 1), 0, g_W);
    int y0 = ClampI((int)floorf(cy - rad - 1), 0, g_H), y1 = ClampI((int)ceilf(cy + rad + 1), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = Clamp01(0.5f - (sqrtf(dx * dx + dy * dy) - rad));
            if (cov > 0)
                BlendPx(x, y, r, g, bl, cov * a);
        }
}

static void BlitMask(const Mask *m, int dx, int dy, float r, float g, float bl, float alpha)
{
    if (!m->p)
        return;
    for (int y = 0; y < m->h; y++)
    {
        int py = dy + y;
        if (py < 0 || py >= g_H)
            continue;
        for (int x = 0; x < m->w; x++)
        {
            int px = dx + x;
            if (px < 0 || px >= g_W)
                continue;
            uint8_t v = m->p[y * m->w + x];
            if (v)
                BlendPx(px, py, r, g, bl, v / 255.0f * alpha);
        }
    }
}

/* 背景のない時計でも全面をつかめるように、ほぼ透明 (alpha=1) で全体を塗る (完全に透明な所はクリックが通り抜けてしまう) */
static void HitFill(void)
{
    for (int i = 0; i < g_W * g_H; i++)
        g_bits[i] = 0x01000000u;
}

/* 背景のない文字は、どんな壁紙の上でも読めるようにうっすら影を付ける */
static void BlitMaskS(const Mask *m, int dx, int dy, float r, float g, float bl, float alpha)
{
    BlitMask(m, dx, dy + (int)fmaxf(1.0f, 1.5f * g_dpi), 0, 0, 0, alpha * 0.35f);
    BlitMask(m, dx, dy, r, g, bl, alpha);
}

/* 輪 (リング) */
static void DrawRing(float cx, float cy, float rad, float th, float r, float g, float bl, float a)
{
    int x0 = ClampI((int)floorf(cx - rad - th - 1), 0, g_W), x1 = ClampI((int)ceilf(cx + rad + th + 1), 0, g_W);
    int y0 = ClampI((int)floorf(cy - rad - th - 1), 0, g_H), y1 = ClampI((int)ceilf(cy + rad + th + 1), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = Clamp01(0.5f - (fabsf(sqrtf(dx * dx + dy * dy) - rad) - th * 0.5f));
            if (cov > 0)
                BlendPx(x, y, r, g, bl, cov * a);
        }
}

/* 波形の縁 (花のように縁が波打つ円): 半径 = R0 + A*cos(n*角度)。stroke=0 なら塗りつぶし、>0 なら輪郭線 */
static void DrawScallop(float cx, float cy, float R0, float A, int n, float stroke, float r, float g, float bl,
                        float a)
{
    float Rmax = R0 + A + stroke;
    int x0 = ClampI((int)floorf(cx - Rmax - 2), 0, g_W), x1 = ClampI((int)ceilf(cx + Rmax + 2), 0, g_W);
    int y0 = ClampI((int)floorf(cy - Rmax - 2), 0, g_H), y1 = ClampI((int)ceilf(cy + Rmax + 2), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float ph = n * atan2f(dy, dx);
            float rr = R0 + A * cosf(ph), dr = -A * n * sinf(ph);
            float q = dr / fmaxf(d, 1.0f);
            float f = (d - rr) / sqrtf(1.0f + q * q); /* 縁までのおおよその距離 (外が正) */
            float cov = stroke > 0 ? Clamp01(0.5f - (fabsf(f) - stroke * 0.5f)) : Clamp01(0.5f - f);
            if (cov > 0)
                BlendPx(x, y, r, g, bl, cov * a);
        }
}

/* 凸多角形 (4x4 の超サンプリングで縁をなめらかに) */
static void DrawPoly(const float *px, const float *py, int n, float r, float g, float bl, float a)
{
    float minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    for (int i = 1; i < n; i++)
    {
        minx = fminf(minx, px[i]);
        maxx = fmaxf(maxx, px[i]);
        miny = fminf(miny, py[i]);
        maxy = fmaxf(maxy, py[i]);
    }
    int x0 = ClampI((int)floorf(minx), 0, g_W), x1 = ClampI((int)ceilf(maxx) + 1, 0, g_W);
    int y0 = ClampI((int)floorf(miny), 0, g_H), y1 = ClampI((int)ceilf(maxy) + 1, 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++)
                {
                    float sxp = x + (sx + 0.5f) * 0.25f, syp = y + (sy + 0.5f) * 0.25f;
                    int pos = 0, neg = 0;
                    for (int i = 0; i < n; i++)
                    {
                        int j = (i + 1) % n;
                        float cr = (px[j] - px[i]) * (syp - py[i]) - (py[j] - py[i]) * (sxp - px[i]);
                        if (cr > 0)
                            pos++;
                        else if (cr < 0)
                            neg++;
                    }
                    if (pos == 0 || neg == 0)
                        hit++;
                }
            if (hit)
                BlendPx(x, y, r, g, bl, a * hit / 16.0f);
        }
}

/* コンクリートの文字盤: 上は縦じま、下は同心の半円 (縦じまが下で U 字に回り込む)。
 * 縦に伸びる半直線 (中心から上) までの距離で溝を作る */
static void DrawStone(float cx, float cy, float R, float br, float bg, float bb, float a, float arcY)
{
    float period = R * 0.125f;
    int x0 = ClampI((int)floorf(cx - R - 1), 0, g_W), x1 = ClampI((int)ceilf(cx + R + 1), 0, g_W);
    int y0 = ClampI((int)floorf(cy - R - 1), 0, g_H), y1 = ClampI((int)ceilf(cy + R + 1), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = Clamp01(0.5f - (sqrtf(dx * dx + dy * dy) - R));
            if (cov <= 0)
                continue;
            float dist = dy <= arcY ? fabsf(dx) : sqrtf(dx * dx + (dy - arcY) * (dy - arcY));
            float k = cosf(dist / period * 6.2831853f); /* -1..1 */
            float sh = 1.0f + 0.14f * k;
            /* 縁にむかって少しだけ暗く (厚みの表現) */
            float edge = Clamp01((R - sqrtf(dx * dx + dy * dy)) / (R * 0.04f));
            sh *= 0.90f + 0.10f * edge;
            BlendPx(x, y, fminf(255, br * sh), fminf(255, bg * sh), fminf(255, bb * sh), a * cov);
        }
}

/* 7セグ時計の寸法 (大きさ指定から) */
static void Seg7Metrics(float *dh, float *dw, float *t, float *sp, float *colW)
{
    float sc = g_size / 100.0f;
    *dh = (float)S(54 * sc);
    *dw = *dh * 0.56f;
    *t = *dh * 0.14f;
    *sp = *dw * 0.20f;
    *colW = *t * 2.2f;
}

/* ------------------------------------------------------------------ */
/* 描画                                                                  */
/* ------------------------------------------------------------------ */
static const wchar_t *const kWeekday[7] = {L"日", L"月", L"火", L"水", L"木", L"金", L"土"};
static const wchar_t *const kVarDigital[5] = {L"カード", L"細字 (大)", L"縦並び", L"分を小さく", L"7セグ"};
static const wchar_t *const kVarAnalog[7] = {L"カード", L"リング", L"波形 (ライト)", L"波形 (ダーク)", L"波形 (白い枠)",
                                             L"コンクリート (明)", L"コンクリート (暗)"};

/* 日付の最大幅 (窓の幅を決めるのに使う) */
static int DateMaxW(void)
{
    return TextW(g_fDate, (g_style == 0 && g_vr[0] == 1) ? L"12月30日水曜日" : L"12月30日 (水)");
}

/* 窓の大きさ。デジタルの細字(大)・縦並び・分を小さく は、フォントの実寸から決める (RebuildFonts の後に呼ぶ) */
static void CalcSize(int *w, int *h)
{
    float sc = g_size / 100.0f;
    if (g_style == 1)
    {
        *w = *h = S(200 * sc);
        return;
    }
    int var = g_vr[0];
    if (var == 0)
    {
        *w = S(264 * sc);
        *h = S((g_date ? 112 : 84) * sc);
        return;
    }
    int pad = S(14 * sc), gap = S(8 * sc);
    int digH = g_digBot - g_digTop + 1;
    BOOL showDate = g_date && var != 1; /* 細字(大)は日付なし */
    int dateW = showDate ? DateMaxW() : 0;
    int dateH = showDate ? gap + TextH(g_fDate) : 0;
    int apW = g_h24 ? 0 : TextW(g_fAp, L"PM");
    int cw, ch;
    if (var == 4)
    {
        float dh, dw, t, sp, colW;
        Seg7Metrics(&dh, &dw, &t, &sp, &colW);
        *w = (int)(4 * dw + 4 * sp + colW + 0.5f) + (g_h24 ? 0 : S(6 * sc) + apW) + 2 * S(16 * sc);
        *h = (int)(dh + 0.5f) + 2 * S(16 * sc);
        return;
    }
    if (var == 1)
    {
        cw = (g_sec ? 6 : 4) * g_digitW + (g_sec ? 2 : 1) * g_adv[10] + (g_h24 ? 0 : S(6 * sc) + apW);
        ch = digH + dateH;
    }
    else if (var == 2)
    {
        cw = 2 * g_digitW + (g_h24 ? 0 : S(4 * sc) + apW);
        ch = digH + gap + g_subDigH + dateH;
    }
    else
    {
        cw = 2 * g_digitW + S(6 * sc) + TextW(g_fSub, L"00");
        ch = digH + dateH;
    }
    *w = (cw > dateW ? cw : dateW) + 2 * pad;
    *h = ch + 2 * pad;
}

/* 日付の文字マスクを (文字列が変わったときだけ) 作り直す */
static void UpdateDateMask(const SYSTEMTIME *st)
{
    wchar_t ds[48];
    const wchar_t *wd = kWeekday[st->wDayOfWeek % 7];
    if (g_style == 0 && g_vr[0] == 1)
        _snwprintf(ds, 48, L"%d月%d日%s曜日", st->wMonth, st->wDay, wd);
    else if (g_style == 1 && g_vr[1] >= 2 && g_vr[1] <= 4)
        _snwprintf(ds, 48, L"%s %d", wd, st->wDay); /* 波形: 「日 23」(曜日と日にち) */
    else
        _snwprintf(ds, 48, L"%d月%d日 (%s)", st->wMonth, st->wDay, wd);
    ds[47] = 0;
    if (wcscmp(ds, g_dateStr) != 0)
    {
        wcscpy(g_dateStr, ds);
        FreeMask(&g_mDate);
        g_mDate = MakeMask(g_fDate, ds, (int)wcslen(ds));
    }
}

/* 小さい数字 (縦並びの分 / 分を小さく) の文字マスク */
static void UpdateSubMask(const wchar_t *s)
{
    if (wcscmp(s, g_subStr) != 0)
    {
        wcsncpy(g_subStr, s, 7);
        g_subStr[7] = 0;
        FreeMask(&g_mSub);
        g_mSub = MakeMask(g_fSub, g_subStr, (int)wcslen(g_subStr));
    }
}

static void UpdateApMask(BOOL pm)
{
    const wchar_t *ap = pm ? L"PM" : L"AM";
    if (wcscmp(ap, g_apStr) != 0)
    {
        wcscpy(g_apStr, ap);
        FreeMask(&g_mAp);
        g_mAp = MakeMask(g_fAp, ap, 2);
    }
}

/* 秒の表示が時計の見た目に影響するか (縦並び・分を小さく は秒を使わない) */
static BOOL ShowsSeconds(void)
{
    if (g_style == 0 && g_vr[0] >= 2)
        return FALSE; /* 縦並び・分を小さく・7セグ は秒を使わない */
    return g_sec;
}

static void RenderDigital(const SYSTEMTIME *st)
{
    const float gr = 214, gg = 184, gb = 132; /* 控えめなシャンパンゴールド */
    float sc = g_size / 100.0f;
    DrawBg(14.0f * g_dpi * sc, g_opa / 100.0f);

    int hr = st->wHour;
    BOOL pm = hr >= 12;
    if (!g_h24)
    {
        hr %= 12;
        if (hr == 0)
            hr = 12;
    }
    wchar_t ts[16];
    if (g_sec)
        _snwprintf(ts, 16, g_h24 ? L"%02d:%02d:%02d" : L"%d:%02d:%02d", hr, st->wMinute, st->wSecond);
    else
        _snwprintf(ts, 16, g_h24 ? L"%02d:%02d" : L"%d:%02d", hr, st->wMinute);
    ts[15] = 0;

    /* 午前/午後 (12時間表示のときだけ、時刻の右に小さく) */
    if (!g_h24)
        UpdateApMask(pm);

    /* 数字は同じ幅のマスに入れて、秒が変わっても全体がぶれないようにする */
    int total = 0;
    for (const wchar_t *c = ts; *c; c++)
        total += (*c == L':') ? g_adv[10] : g_digitW;
    int gap = S(6 * sc);
    if (!g_h24)
        total += gap + g_mAp.w;
    float cy = g_date ? g_H * 0.40f : g_H * 0.5f;
    int x = (g_W - total) / 2;
    for (const wchar_t *c = ts; *c; c++)
    {
        BOOL colon = *c == L':';
        int idx = colon ? 10 : (*c - L'0');
        int cell = colon ? g_adv[10] : g_digitW;
        const Mask *m = &g_glyph[idx];
        int px = x + (cell - g_adv[idx]) / 2 - 1;
        int py = (int)(cy - m->h * 0.5f + 0.5f);
        BlitMask(m, px, py, 238, 238, 240, 0.96f); /* 数字もコロンも白 */
        x += cell;
    }
    if (!g_h24)
        BlitMask(&g_mAp, x + gap - 1, (int)(cy + g_lineH * 0.14f), gr, gg, gb, 0.92f);

    if (g_date)
    {
        UpdateDateMask(st);
        BlitMask(&g_mDate, (g_W - g_mDate.w) / 2, (int)(g_H * 0.78f - g_mDate.h * 0.5f + 0.5f), 238, 238, 240, 0.58f);
    }
}

/* g_glyph の文字で時刻を描く (数字の上端が top)。背景なしデザイン用で、影付き */
static int CellsW(const wchar_t *s)
{
    int t = 0;
    for (; *s; s++)
        t += (*s == L':') ? g_adv[10] : g_digitW;
    return t;
}

static void DrawGlyphStr(const wchar_t *s, int x, int top)
{
    for (; *s; s++)
    {
        BOOL colon = *s == L':';
        int idx = colon ? 10 : (*s - L'0');
        int cell = colon ? g_adv[10] : g_digitW;
        BlitMaskS(&g_glyph[idx], x + (cell - g_adv[idx]) / 2 - 1, top - g_digTop, 248, 248, 250, 0.97f);
        x += cell;
    }
}

/* 背景なしのデジタル: 1:細字(大) / 2:縦並び / 3:分を小さく */
static void RenderDigitalFree(const SYSTEMTIME *st)
{
    const float gr = 226, gg = 200, gb = 150;
    int var = g_vr[0];
    float sc = g_size / 100.0f;
    int pad = S(14 * sc), gap = S(8 * sc);
    int digH = g_digBot - g_digTop + 1;
    HitFill();

    int hr = st->wHour;
    BOOL pm = hr >= 12;
    if (!g_h24)
    {
        hr %= 12;
        if (hr == 0)
            hr = 12;
    }
    if (!g_h24)
        UpdateApMask(pm);
    int y = pad;

    if (var == 1)
    {
        wchar_t ts[16];
        if (g_sec)
            _snwprintf(ts, 16, g_h24 ? L"%02d:%02d:%02d" : L"%d:%02d:%02d", hr, st->wMinute, st->wSecond);
        else
            _snwprintf(ts, 16, g_h24 ? L"%02d:%02d" : L"%d:%02d", hr, st->wMinute);
        ts[15] = 0;
        int tw = CellsW(ts), gp = S(6 * sc);
        int total = tw + (g_h24 ? 0 : gp + g_mAp.w);
        int x = (g_W - total) / 2;
        DrawGlyphStr(ts, x, y);
        if (!g_h24)
            BlitMaskS(&g_mAp, x + tw + gp, y + digH - 1 - g_mAp.bot, gr, gg, gb, 0.95f);
    }
    else if (var == 2)
    {
        wchar_t hs[8], ms[8];
        _snwprintf(hs, 8, g_h24 ? L"%02d" : L"%d", hr);
        _snwprintf(ms, 8, L"%02d", st->wMinute);
        hs[7] = 0;
        ms[7] = 0;
        int hw2 = 2 * g_digitW, gp = S(4 * sc);
        int block = hw2 + (g_h24 ? 0 : gp + g_mAp.w);
        int x0 = (g_W - block) / 2;
        DrawGlyphStr(hs, x0 + (hw2 - CellsW(hs)) / 2, y);
        if (!g_h24)
            BlitMaskS(&g_mAp, x0 + hw2 + gp, y - g_mAp.top, gr, gg, gb, 0.95f);
        y += digH + gap;
        UpdateSubMask(ms);
        BlitMaskS(&g_mSub, (g_W - g_mSub.w) / 2, y - g_mSub.top, 248, 248, 250, 0.97f);
        y += g_subDigH;
    }
    else
    {
        wchar_t hs[8], ms[8];
        _snwprintf(hs, 8, g_h24 ? L"%02d" : L"%d", hr);
        _snwprintf(ms, 8, L"%02d", st->wMinute);
        hs[7] = 0;
        ms[7] = 0;
        int hw2 = 2 * g_digitW, gp = S(6 * sc), mw = TextW(g_fSub, L"00");
        int x0 = (g_W - (hw2 + gp + mw)) / 2;
        DrawGlyphStr(hs, x0 + hw2 - CellsW(hs), y); /* 時は分の側に寄せる */
        UpdateSubMask(ms);
        BlitMaskS(&g_mSub, x0 + hw2 + gp, y - g_mSub.top, 248, 248, 250, 0.97f);
        if (!g_h24)
            BlitMaskS(&g_mAp, x0 + hw2 + gp, y + g_subDigH + S(6 * sc) - g_mAp.top, gr, gg, gb, 0.95f);
        y += digH;
    }

    if (g_date && var != 1)
    {
        UpdateDateMask(st);
        y += gap;
        BlitMaskS(&g_mDate, (g_W - g_mDate.w) / 2, y, 248, 248, 250, 0.80f);
    }
}

static void Hand(float cx, float cy, float ang, float len, float th, float r, float g, float bl, float a)
{
    DrawSeg(cx, cy, cx + sinf(ang) * len, cy - cosf(ang) * len, th, r, g, bl, a);
}

/* アナログ (針は全部白系)
 *   0:カード   = 12・3・6・9時の印だけの、ミニマルな半透明の丸
 *   1:リング   = 背景なしの細い輪と細い針
 *   2〜4:波形  = 縁が波打つ丸に、太く丸い針 (2:ライト / 3:ダーク / 4:白い枠だけ)  */
static void RenderAnalog(const SYSTEMTIME *st)
{
    int var = g_vr[1];
    float d = g_dpi * g_size / 100.0f; /* 論理 1px の大きさ */
    float R = g_W * 0.5f, cx = R, cy = g_H * 0.5f;
    const float rad = 3.14159265f / 180.0f;
    float ha = ((st->wHour % 12) + st->wMinute / 60.0f) * 30.0f * rad;
    float ma = (st->wMinute + st->wSecond / 60.0f) * 6.0f * rad;
    float sa = st->wSecond * 6.0f * rad;
    float opa = g_opa / 100.0f;

    if (var == 0)
    {
        DrawBg(R, opa);
        /* 12・3・6・9時の印だけ */
        for (int i = 0; i < 4; i++)
        {
            float a = i * 90.0f * 3.14159265f / 180.0f, sx = sinf(a), sy = -cosf(a);
            DrawSeg(cx + sx * R * 0.80f, cy + sy * R * 0.80f, cx + sx * R * 0.88f, cy + sy * R * 0.88f,
                    fmaxf(1.5f, 2.0f * d), 238, 238, 240, 0.78f);
        }
        if (g_date)
        {
            UpdateDateMask(st);
            BlitMask(&g_mDate, (int)(cx - g_mDate.w * 0.5f), (int)(cy + R * 0.42f - g_mDate.h * 0.5f + 0.5f), 238, 238,
                     240, 0.5f);
        }
        Hand(cx, cy, ha, R * 0.42f, 3.6f * d, 238, 238, 240, 0.96f);
        Hand(cx, cy, ma, R * 0.66f, 2.6f * d, 238, 238, 240, 0.96f);
        if (g_sec)
            Hand(cx, cy, sa, R * 0.74f, fmaxf(1.2f, 1.3f * d), 255, 255, 255, 0.95f); /* 秒針も白 */
        DrawDot(cx, cy, 3.4f * d, 255, 255, 255, 1.0f);                               /* 中心も白 */
    }
    else if (var == 1)
    {
        DrawDot(cx, cy, R - 1.0f, 28, 28, 32, 0.30f * opa); /* ごくうすい下地 (背景の濃さで調整) */
        DrawRing(cx, cy, R - 1.5f * d - 1.0f, fmaxf(1.6f, 2.2f * d), 255, 255, 255, 0.95f);
        if (g_date)
        {
            UpdateDateMask(st);
            BlitMaskS(&g_mDate, (int)(cx - g_mDate.w * 0.5f), (int)(cy + R * 0.40f - g_mDate.h * 0.5f + 0.5f), 255, 255,
                      255, 0.75f);
        }
        Hand(cx, cy, ha, R * 0.42f, fmaxf(1.4f, 2.4f * d), 255, 255, 255, 0.97f);
        Hand(cx, cy, ma, R * 0.68f, fmaxf(1.2f, 1.8f * d), 255, 255, 255, 0.97f);
        if (g_sec)
            Hand(cx, cy, sa, R * 0.80f, fmaxf(1.0f, 1.0f * d), 255, 255, 255, 0.85f);
        DrawDot(cx, cy, 2.6f * d, 255, 255, 255, 1.0f);
    }
    else if (var <= 4)
    {
        float Rm = R - 1.0f, A = Rm * 0.045f;
        float fr, fg, fb, hr_, hg, hb, mr, mg, mb, dr, dg, db; /* 地 / 時針 / 分針 / 日付の色 */
        if (var == 2)                                          /* ライト */
        {
            fr = 250, fg = 248, fb = 243;
            hr_ = 84, hg = 38, hb = 22;
            mr = 150, mg = 76, mb = 38;
            dr = 80, dg = 66, db = 56;
            DrawScallop(cx, cy, Rm - A, A, 12, 0, fr, fg, fb, 0.35f + 0.65f * opa);
        }
        else if (var == 3) /* ダーク */
        {
            fr = 52, fg = 52, fb = 55;
            hr_ = 222, hg = 104, hb = 26;
            mr = 248, mg = 160, mb = 96;
            dr = 238, dg = 238, db = 238;
            DrawScallop(cx, cy, Rm - A, A, 12, 0, fr, fg, fb, 0.35f + 0.65f * opa);
        }
        else /* 白い枠だけ */
        {
            float st_ = fmaxf(2.0f, 5.0f * d);
            hr_ = hg = hb = 255;
            mr = mg = mb = 238;
            dr = dg = db = 255;
            DrawScallop(cx, cy, Rm - A - st_ * 0.5f, A, 12, 0, 28, 28, 32, 0.22f * opa); /* ごくうすい下地 */
            DrawScallop(cx, cy, Rm - A - st_ * 0.5f, A, 12, st_, 255, 255, 255, 0.96f);
        }
        if (g_date)
        {
            UpdateDateMask(st);
            Mask *m = &g_mDate;
            if (m->p)
                BlitMask(m, (int)(cx - m->w * 0.5f), (int)(cy - Rm * 0.70f - m->h * 0.5f + 0.5f), dr, dg, db, 0.9f);
        }
        Hand(cx, cy, ha, Rm * 0.36f, Rm * 0.115f, hr_, hg, hb, 1.0f);
        Hand(cx, cy, ma, Rm * 0.58f, Rm * 0.095f, mr, mg, mb, 1.0f);
        if (g_sec)
            Hand(cx, cy, sa, Rm * 0.72f, fmaxf(1.2f, Rm * 0.022f), mr, mg, mb, 0.9f);
    }
    else /* 5:石(明) / 6:石(暗) — 縦じまと同心の半円を刻んだコンクリート調の文字盤 + 銀色の針 */
    {
        float Rm = R - 1.0f, a = 0.35f + 0.65f * opa;
        if (var == 5)
            DrawStone(cx, cy, Rm, 156, 152, 146, a, Rm * 0.10f);
        else
            DrawStone(cx, cy, Rm, 98, 98, 96, a, Rm * 0.10f);
        /* 針: 影 → 銀色 (中心の向こう側に少し突き出る) */
        float so = 1.8f * d, sh = 2.4f * d;
        float angs[3] = {ha, ma, sa};
        float lens[3] = {Rm * 0.50f, Rm * 0.90f, Rm * 0.86f}, backs[3] = {Rm * 0.10f, Rm * 0.12f, Rm * 0.16f};
        float ths[3] = {fmaxf(2.4f, 4.2f * d), fmaxf(1.8f, 2.8f * d), fmaxf(1.0f, 1.2f * d)};
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < 3; i++)
            {
                if (i == 2 && !g_sec)
                    continue;
                float sn = sinf(angs[i]), cs = cosf(angs[i]);
                float ox = pass == 0 ? so : 0, oy = pass == 0 ? sh : 0;
                float ax = cx - sn * backs[i] + ox, ay = cy + cs * backs[i] + oy;
                float bx = cx + sn * lens[i] + ox, by = cy - cs * lens[i] + oy;
                if (pass == 0)
                    DrawSeg(ax, ay, bx, by, ths[i], 0, 0, 0, 0.28f);
                else if (i == 2)
                    DrawSeg(ax, ay, bx, by, ths[i], 226, 226, 220, 0.85f);
                else
                {
                    DrawSeg(ax, ay, bx, by, ths[i], 206, 206, 200, 1.0f);
                    DrawSeg(ax, ay, bx, by, ths[i] * 0.45f, 244, 244, 238, 0.9f); /* 金属のつや */
                }
            }
        DrawDot(cx + so * 0.6f, cy + sh * 0.6f, 4.6f * d, 0, 0, 0, 0.25f);
        DrawDot(cx, cy, 4.4f * d, 214, 214, 208, 1.0f);
        DrawDot(cx - 0.8f * d, cy - 0.8f * d, 1.8f * d, 250, 250, 246, 0.9f);
    }
}

/* ---- 7セグ ---- */
static void Seg7Hex(float x0, float y0, float x1, float y1, float t, float a)
{
    float ht = t * 0.5f, px[6], py[6];
    if (fabsf(y1 - y0) < 0.01f) /* 横 */
    {
        px[0] = x0, py[0] = y0;
        px[1] = x0 + ht, py[1] = y0 - ht;
        px[2] = x1 - ht, py[2] = y0 - ht;
        px[3] = x1, py[3] = y0;
        px[4] = x1 - ht, py[4] = y0 + ht;
        px[5] = x0 + ht, py[5] = y0 + ht;
    }
    else /* 縦 */
    {
        px[0] = x0, py[0] = y0;
        px[1] = x0 + ht, py[1] = y0 + ht;
        px[2] = x0 + ht, py[2] = y1 - ht;
        px[3] = x0, py[3] = y1;
        px[4] = x0 - ht, py[4] = y1 - ht;
        px[5] = x0 - ht, py[5] = y0 + ht;
    }
    DrawPoly(px, py, 6, 248, 248, 250, a);
}

static void Seg7Digit(float x, float y, float w, float h, float t, int digit)
{
    static const uint8_t k7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    int m = (digit >= 0 && digit <= 9) ? k7[digit] : 0;
    float ht = t * 0.5f, gp = t * 0.14f; /* 線分の先端どうしを角で向かい合わせ、すき間だけ空ける */
    float xl = x + ht, xr = x + w - ht, yt = y + ht, ym = y + h * 0.5f, yb = y + h - ht;
    float hx0 = xl + gp, hx1 = xr - gp;
    const float ghost = 0.13f, lit = 0.97f;
    /* a b c d e f g */
    Seg7Hex(hx0, yt, hx1, yt, t, (m & 0x01) ? lit : ghost);
    Seg7Hex(xr, yt + gp, xr, ym - gp, t, (m & 0x02) ? lit : ghost);
    Seg7Hex(xr, ym + gp, xr, yb - gp, t, (m & 0x04) ? lit : ghost);
    Seg7Hex(hx0, yb, hx1, yb, t, (m & 0x08) ? lit : ghost);
    Seg7Hex(xl, ym + gp, xl, yb - gp, t, (m & 0x10) ? lit : ghost);
    Seg7Hex(xl, yt + gp, xl, ym - gp, t, (m & 0x20) ? lit : ghost);
    Seg7Hex(hx0, ym, hx1, ym, t, (m & 0x40) ? lit : ghost);
}

static void RenderSeg7(const SYSTEMTIME *st)
{
    const float gr = 214, gg = 184, gb = 132;
    float sc = g_size / 100.0f;
    DrawBg(14.0f * g_dpi * sc, g_opa / 100.0f);
    float dh, dw, t, sp, colW;
    Seg7Metrics(&dh, &dw, &t, &sp, &colW);
    int hr = st->wHour;
    BOOL pm = hr >= 12;
    if (!g_h24)
    {
        hr %= 12;
        if (hr == 0)
            hr = 12;
        UpdateApMask(pm);
    }
    int pad = S(16 * sc), gap = S(6 * sc);
    float total = 4 * dw + 4 * sp + colW;
    float full = total + (g_h24 ? 0 : gap + g_mAp.w);
    float x = (g_W - full) * 0.5f, y = (g_H - dh) * 0.5f;
    (void)pad;
    int d[4] = {(!g_h24 && hr < 10) ? -1 : hr / 10, hr % 10, st->wMinute / 10, st->wMinute % 10};
    Seg7Digit(x, y, dw, dh, t, d[0]);
    x += dw + sp;
    Seg7Digit(x, y, dw, dh, t, d[1]);
    x += dw + sp;
    float cxm = x + colW * 0.5f; /* コロン (2つの四角) */
    for (int k = 0; k < 2; k++)
    {
        float cyy = y + dh * (k ? 0.70f : 0.30f), hs = t * 0.55f;
        float px[4] = {cxm - hs, cxm + hs, cxm + hs, cxm - hs}, py[4] = {cyy - hs, cyy - hs, cyy + hs, cyy + hs};
        DrawPoly(px, py, 4, 248, 248, 250, 0.97f);
    }
    x += colW + sp;
    Seg7Digit(x, y, dw, dh, t, d[2]);
    x += dw + sp;
    Seg7Digit(x, y, dw, dh, t, d[3]);
    x += dw;
    if (!g_h24)
        BlitMask(&g_mAp, (int)(x + gap), (int)(y + dh) - 1 - g_mAp.bot, gr, gg, gb, 0.92f);
}

static void ClockRender(void)
{
    if (!g_hwnd || !g_bits)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    memset(g_bits, 0, (size_t)g_W * g_H * 4);
    if (g_style == 1)
        RenderAnalog(&st);
    else if (g_vr[0] == 0)
        RenderDigital(&st);
    else if (g_vr[0] == 4)
        RenderSeg7(&st);
    else
        RenderDigitalFree(&st);

    POINT src = {0, 0};
    SIZE sz = {g_W, g_H};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, NULL, NULL, &sz, g_dc, &src, 0, &bf, ULW_ALPHA);
}

/* 表示が変わるのは、秒を出すときは毎秒・出さないときは毎分 (日付が変わる 0 時も含む) */
static int TimeKey(void)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wDay * 100000 + st.wHour * 3600 + st.wMinute * 60 + (ShowsSeconds() ? st.wSecond : 0);
}

/* ------------------------------------------------------------------ */
/* 最奥への配置: デスクトップ本体 (Progman / WorkerW) の直上                 */
/* (検索バー・画像と同じ方式。それらのウィンドウは重なり順の判定で飛ばす)       */
/* ------------------------------------------------------------------ */
static BOOL ClassIs(HWND w, const wchar_t *name)
{
    wchar_t c[32];
    return GetClassNameW(w, c, 32) && wcscmp(c, name) == 0;
}

static BOOL IsDesktopHost(HWND w) { return ClassIs(w, L"Progman") || ClassIs(w, L"WorkerW"); }

static BOOL IsOurs(HWND w)
{
    return ClassIs(w, L"PromClockWnd") || ClassIs(w, L"PromPhotoFrameWnd") || ClassIs(w, L"PromSearchBarWnd") ||
           ClassIs(w, L"PromWeatherWnd") || ClassIs(w, L"PromSysMonWnd");
}

static BOOL AtDesktopLevel(HWND h)
{
    HWND w = GetWindow(h, GW_HWNDNEXT);
    while (w && IsOurs(w))
        w = GetWindow(w, GW_HWNDNEXT);
    return w && IsDesktopHost(w);
}

static void ZPlace(void)
{
    if (!g_hwnd || AtDesktopLevel(g_hwnd))
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
    g_placing = TRUE;
    SetWindowPos(g_hwnd, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_placing = FALSE;
}

/* ------------------------------------------------------------------ */
/* 作成 / 破棄 / 再配置                                                   */
/* ------------------------------------------------------------------ */
static void FreeBuffers(void)
{
    if (g_dc)
    {
        if (g_dib)
        {
            SelectObject(g_dc, g_old);
            DeleteObject(g_dib);
        }
        DeleteDC(g_dc);
    }
    g_dc = NULL;
    g_dib = g_old = NULL;
    g_bits = NULL;
}

static BOOL MakeBuffers(void)
{
    g_dc = CreateCompatibleDC(NULL);
    if (!g_dc)
        return FALSE;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_W;
    bi.bmiHeader.biHeight = -g_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    if (!g_dib || !g_bits)
    {
        FreeBuffers();
        return FALSE;
    }
    g_old = (HBITMAP)SelectObject(g_dc, g_dib);
    return TRUE;
}

static void ClampToWork(void)
{
    RECT r = {g_x, g_y, g_x + g_W, g_y + g_H};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    /* はみ出しても構わないが、つかめる部分 (40px) は必ず画面内に残す */
    const int keep = 40;
    g_x = ClampI(g_x, mi.rcWork.left - g_W + keep, mi.rcWork.right - keep);
    g_y = ClampI(g_y, mi.rcWork.top - g_H + keep, mi.rcWork.bottom - keep);
}

static void AutoPos(void)
{
    POINT o = {0, 0};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    g_x = mi.rcWork.right - g_W - S(40); /* 右上 */
    g_y = mi.rcWork.top + S(40);
}

static LRESULT CALLBACK ClockProc(HWND h, UINT m, WPARAM w, LPARAM l);

static void ClockCreate(void)
{
    if (g_hwnd || !g_on || !g_ready)
        return;
    RebuildFonts();
    CalcSize(&g_W, &g_H);
    if (g_x == POS_AUTO || g_y == POS_AUTO)
        AutoPos();
    ClampToWork();
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"PromClockWnd", L"PromClock", WS_POPUP, g_x, g_y, g_W,
                             g_H, NULL, NULL, g_hInst, NULL);
    if (!g_hwnd)
        return;
    if (!MakeBuffers())
    {
        DestroyWindow(g_hwnd);
        return;
    }
    g_lastKey = TimeKey();
    ClockRender();
    ZPlace();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    ZPlace();
    SetTimer(g_hwnd, TIMER_TICK, 200, NULL);
    SetTimer(g_hwnd, TIMER_Z, 1000, NULL);
}

static void ClockDestroyWin(void)
{
    if (g_hwnd)
        DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    FreeBuffers();
}

/* 大きさ・スタイル・日付の有無が変わったとき: 窓の大きさごと作り直す (位置は左上を保つ) */
static void ClockRelayout(void)
{
    if (!g_hwnd)
        return;
    FreeBuffers();
    RebuildFonts();
    CalcSize(&g_W, &g_H);
    SetWindowPos(g_hwnd, NULL, 0, 0, g_W, g_H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (MakeBuffers())
    {
        g_lastKey = TimeKey();
        ClockRender();
    }
}

static void ClockRedraw(void)
{
    g_lastKey = TimeKey();
    ClockRender();
}

/* ------------------------------------------------------------------ */
/* 右クリックメニュー / ウィンドウ                                          */
/* ------------------------------------------------------------------ */
static void ShowClockMenu(void)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_style == 0 ? MF_CHECKED : 0), MENU_STYLE_DIGITAL, L"デジタル");
    AppendMenuW(menu, MF_STRING | (g_style == 1 ? MF_CHECKED : 0), MENU_STYLE_ANALOG, L"アナログ");
    {
        HMENU sub = CreatePopupMenu();
        int vn = Clock_VariantCount(g_style);
        for (int i = 0; i < vn; i++)
            AppendMenuW(sub, MF_STRING | (g_vr[g_style] == i ? MF_CHECKED : 0), MENU_VAR + i,
                        g_style == 0 ? kVarDigital[i] : kVarAnalog[i]);
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"デザイン");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_h24 ? MF_CHECKED : 0) | (g_style == 0 ? 0 : MF_GRAYED), MENU_24H, L"24時間表示");
    AppendMenuW(menu, MF_STRING | (g_sec ? MF_CHECKED : 0), MENU_SECONDS, L"秒を表示");
    AppendMenuW(menu, MF_STRING | (g_date ? MF_CHECKED : 0), MENU_DATE, L"日付を表示");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_movable ? MF_CHECKED : 0), MENU_MOVABLE, L"ドラッグで移動できるようにする");
    AppendMenuW(menu, MF_STRING, MENU_HIDE, L"時計を消す");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"アプリ設定…");
    POINT pt;
    GetCursorPos(&pt);
    HWND h = g_hwnd;
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
    DestroyMenu(menu);
    PostMessageW(h, WM_NULL, 0, 0);
    if (cmd >= MENU_VAR && cmd < MENU_VAR + 16)
    {
        Clock_SetVariant(cmd - MENU_VAR);
        Settings_RefreshClock();
        return;
    }
    switch (cmd)
    {
    case MENU_STYLE_DIGITAL:
        Clock_SetStyle(0);
        break;
    case MENU_STYLE_ANALOG:
        Clock_SetStyle(1);
        break;
    case MENU_24H:
        Clock_Set24h(!g_h24);
        break;
    case MENU_SECONDS:
        Clock_SetSeconds(!g_sec);
        break;
    case MENU_DATE:
        Clock_SetDate(!g_date);
        break;
    case MENU_MOVABLE:
        Clock_SetMovable(!g_movable);
        break;
    case MENU_HIDE:
        Clock_SetEnabled(FALSE); /* 窓が破棄されるので、これ以降触らない */
        break;
    case MENU_SETTINGS:
        Settings_OpenPage(5);
        return;
    default:
        return;
    }
    Settings_RefreshClock();
}

static LRESULT CALLBACK ClockProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_WINDOWPOSCHANGING:
        if (!g_placing) /* クリックで手前に出てこないよう、自分以外からの重なり順の変更は無効にする */
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
        g_x = rc.left;
        g_y = rc.top;
        SaveClock();
        return 0;
    }

    case WM_TIMER:
        if (w == TIMER_TICK)
        {
            int k = TimeKey();
            if (k != g_lastKey)
            {
                g_lastKey = k;
                ClockRender();
            }
        }
        else if (w == TIMER_Z)
            ZPlace();
        return 0;

    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP: /* 移動できる状態では全面が HTCAPTION なので、ここで右クリックを受ける */
    case WM_CONTEXTMENU:
        ShowClockMenu();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCDESTROY:
        g_hwnd = NULL;
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* 公開関数                                                              */
/* ------------------------------------------------------------------ */
BOOL Clock_Enabled(void)
{
    EnsureLoaded();
    return g_on;
}

void Clock_SetEnabled(BOOL on)
{
    EnsureLoaded();
    on = on ? TRUE : FALSE;
    if (on == g_on)
        return;
    g_on = on;
    SaveClock();
    if (!g_ready)
        return;
    if (on)
        ClockCreate();
    else
        ClockDestroyWin();
}

int Clock_GetStyle(void)
{
    EnsureLoaded();
    return g_style;
}

void Clock_SetStyle(int style)
{
    EnsureLoaded();
    style = ClampI(style, 0, 1);
    if (style == g_style)
        return;
    g_style = style;
    SaveClock();
    ClockRelayout();
}

int Clock_VariantCount(int style) { return style == 1 ? 7 : 5; }

int Clock_GetVariant(void)
{
    EnsureLoaded();
    return g_vr[g_style == 1 ? 1 : 0];
}

void Clock_SetVariant(int v)
{
    EnsureLoaded();
    int s = g_style == 1 ? 1 : 0;
    v = ClampI(v, 0, Clock_VariantCount(s) - 1);
    if (v == g_vr[s])
        return;
    g_vr[s] = v;
    SaveClock();
    ClockRelayout();
}

BOOL Clock_Get24h(void)
{
    EnsureLoaded();
    return g_h24;
}

void Clock_Set24h(BOOL on)
{
    EnsureLoaded();
    g_h24 = on ? TRUE : FALSE;
    SaveClock();
    ClockRelayout(); /* 12時間表示ではAM/PMの分だけ幅が変わる */
}

BOOL Clock_GetSeconds(void)
{
    EnsureLoaded();
    return g_sec;
}

void Clock_SetSeconds(BOOL on)
{
    EnsureLoaded();
    g_sec = on ? TRUE : FALSE;
    SaveClock();
    ClockRelayout(); /* 秒の桁の分だけ幅が変わる */
}

BOOL Clock_GetDate(void)
{
    EnsureLoaded();
    return g_date;
}

void Clock_SetDate(BOOL on)
{
    EnsureLoaded();
    g_date = on ? TRUE : FALSE;
    SaveClock();
    ClockRelayout(); /* デジタルでは高さが変わる */
}

int Clock_GetSize(void)
{
    EnsureLoaded();
    return g_size;
}

void Clock_SetSize(int pct)
{
    EnsureLoaded();
    pct = ClampI(pct, 60, 200);
    if (pct == g_size)
        return;
    g_size = pct;
    SaveClock();
    ClockRelayout();
}

int Clock_GetOpacity(void)
{
    EnsureLoaded();
    return g_opa;
}

void Clock_SetOpacity(int pct)
{
    EnsureLoaded();
    pct = ClampI(pct, 30, 100);
    if (pct == g_opa)
        return;
    g_opa = pct;
    SaveClock();
    ClockRedraw();
}

BOOL Clock_Movable(void)
{
    EnsureLoaded();
    return g_movable;
}

void Clock_SetMovable(BOOL on)
{
    EnsureLoaded();
    g_movable = on ? TRUE : FALSE;
    SaveClock();
}

void Clock_ResetPosition(void)
{
    EnsureLoaded();
    g_x = g_y = POS_AUTO;
    if (g_hwnd)
    {
        AutoPos();
        ClampToWork();
        SetWindowPos(g_hwnd, NULL, g_x, g_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SaveClock();
}

void Clock_Init(HINSTANCE hInst)
{
    g_hInst = hInst;
    HDC scr = GetDC(NULL);
    g_dpi = GetDeviceCaps(scr, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, scr);
    EnsureLoaded();
    g_mdc = CreateCompatibleDC(NULL);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ClockProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromClockWnd";
    RegisterClassExW(&wc);

    g_ready = TRUE;
    ClockCreate();
    if (g_hwnd)
        SaveClock(); /* 自動配置した位置を記録 */
}

void Clock_Shutdown(void)
{
    g_ready = FALSE;
    ClockDestroyWin();
    FreeFonts();
    if (g_mdc)
        DeleteDC(g_mdc);
    g_mdc = NULL;
}