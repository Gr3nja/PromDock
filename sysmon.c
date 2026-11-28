/*
 * sysmon.c - PromDock のデスクトップ・システムモニタ (promdock.c / setting.c / clock.c ... と同じ exe にビルド)
 *
 *     cl ... /Febuild\PromDock.exe promdock.c ... clock.c weather.c wallpaper.c sysmon.c /link /SUBSYSTEM:WINDOWS
 *
 * 機能:
 *   - デスクトップに半透明のモニタを置く (アプリ設定の「システムモニタ」ページで表示のオン/オフ)
 *       項目     : CPU 使用率 / メモリ使用率 / ディスク使用率 (Windows のあるドライブ) / ネットワーク速度 (↓受信 ↑送信)
 *       デザイン : 0:バー / 1:リング / 2:コンパクト (文字だけ) / 3:線グラフ / 4:エリアグラフ /
 *                  5:セグメント / 6:半円ゲージ / 7:縦バー / 8:ピル (横並び)
 *                  (線グラフ・エリアグラフは直近 60 回ぶんの推移を表示)
 *       更新間隔 : 1秒 / 2秒 / 5秒
 *       見た目   : 大きさ (60〜200%) / 背景の濃さ (30〜100%)
 *   - 時計・天気と同じく、最前面ではなく最奥 (壁紙のすぐ上=デスクトップの上) に置く
 *   - ドラッグで好きな場所に移動できる (アプリ設定でロックも可能)。位置は自動保存
 *   - 右クリック: デザイン / 項目の表示 / 更新間隔 / ドラッグ移動 / 消す / アプリ設定
 *   - 負荷が高いほど色が変わる (青 → 黄 → 赤)
 *   - 設定は exe と同じフォルダの sysmon.ini に保存
 *       [SysMon]
 *       enabled=1
 *       variant=0        (0〜8: 上のデザイン番号)
 *       cpu=1
 *       mem=1
 *       disk=1
 *       net=1
 *       interval=1       (0:1秒 / 1:2秒 / 2:5秒)
 *       size=100         (%)
 *       opacity=78       (%)
 *       movable=1
 *       x=1500
 *       y=40
 *
 * 外部 (promdock.c / setting.c) から呼ぶ関数:
 *     SysMon_Init(hInst) / SysMon_Shutdown()               ... 起動時/終了時に1回
 *     SysMon_Enabled / _SetEnabled                         ... 表示のオン/オフ
 *     SysMon_GetVariant / _SetVariant / _VariantCount / _VariantName ... デザイン
 *     SysMon_GetItem(i) / _SetItem(i, on)                  ... 0:CPU 1:メモリ 2:ディスク 3:ネットワーク (最後の1つは消せない)
 *     SysMon_GetInterval / _SetInterval                    ... 更新間隔 (0〜2)
 *     SysMon_GetSize / _SetSize, _GetOpacity / _SetOpacity ... 大きさ(%) / 背景の濃さ(%)
 *     SysMon_Movable / _SetMovable / _ResetPosition        ... 配置
 *   ※ Settings_Init() は SysMon_Init() より先に走る (初回設定) ので、
 *      設定の変更は初期化前に呼ばれても安全に動くようにしてある。
 */
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#endif

#define PI_F 3.14159265f

/* setting.c */
void Settings_RefreshSysMon(void);
void Settings_OpenPage(int page); /* 8: システムモニタのページ */

/* このファイルが公開する関数 */
void SysMon_Init(HINSTANCE hInst);
void SysMon_Shutdown(void);
BOOL SysMon_Enabled(void);
void SysMon_SetEnabled(BOOL on);
int SysMon_GetVariant(void);
void SysMon_SetVariant(int v);
int SysMon_VariantCount(void);
const wchar_t *SysMon_VariantName(int i);
BOOL SysMon_GetItem(int i);
void SysMon_SetItem(int i, BOOL on);
int SysMon_GetInterval(void);
void SysMon_SetInterval(int i);
int SysMon_GetSize(void);
void SysMon_SetSize(int pct);
int SysMon_GetOpacity(void);
void SysMon_SetOpacity(int pct);
BOOL SysMon_Movable(void);
void SysMon_SetMovable(BOOL on);
void SysMon_ResetPosition(void);

static const wchar_t *const SM_FONTS[] = {L"Hiragino Maru Gothic ProN", L"ヒラギノ丸ゴシック ProN", L"Kosugi Maru",
                                          L"M PLUS Rounded 1c", L"Yu Gothic UI", L"Meiryo UI", L"Segoe UI"};
#define TIMER_TICK 1 /* 値を読み直して描き直す */
#define TIMER_Z 2    /* 最奥の位置を保つための見張り */
#define POS_AUTO 0x7fffffff
#define N_ITEMS 4
#define N_VARIANTS 9
#define HIST 60       /* 線グラフに残す履歴の数 */
#define MENU_VAR 100  /* 100+番号: デザイン */
#define MENU_ITEM 110 /* 110+番号: 項目 */
#define MENU_INT 120  /* 120+番号: 更新間隔 */
#define MENU_MOVABLE 6
#define MENU_HIDE 7
#define MENU_SETTINGS 8

static const wchar_t *const kItemName[N_ITEMS] = {L"CPU", L"メモリ", L"ディスク", L"ネットワーク"};
static const wchar_t *const kVarName[N_VARIANTS] = {L"バー", L"リング", L"コンパクト", L"線グラフ", L"エリアグラフ",
                                                    L"セグメント", L"半円ゲージ", L"縦バー", L"ピル (横並び)"};
static const wchar_t *const kIntName[3] = {L"1秒", L"2秒", L"5秒"};
static const UINT kIntMs[3] = {1000, 2000, 5000};

/* ------------------------------------------------------------------ */
/* 状態                                                                  */
/* ------------------------------------------------------------------ */
typedef struct
{
    uint8_t *p; /* 白文字のアルファマスク */
    int w, h;
} Mask;

typedef struct
{
    float cpu, mem, disk; /* 0..1 */
    double memUsedGB, memTotGB, diskFreeGB;
    double down, up, peak; /* バイト/秒 */
} Metrics;

static HINSTANCE g_hInst;
static BOOL g_ready, g_loaded;
static BOOL g_on, g_item[N_ITEMS] = {TRUE, TRUE, TRUE, TRUE}, g_movable = TRUE;
static int g_var, g_intv = 1, g_size = 100, g_opa = 78, g_x = POS_AUTO, g_y = POS_AUTO;
static wchar_t g_ini[MAX_PATH];
static float g_dpi = 1.0f;

static HWND g_hwnd;
static HDC g_dc;
static HBITMAP g_dib, g_old;
static uint32_t *g_bits;
static int g_W, g_H;
static BOOL g_placing, g_warm;

static HFONT g_fBold, g_fNorm, g_fSmall, g_fBig;
static HDC g_mdc;
static Metrics g_m;
static float g_h[N_ITEMS][HIST]; /* 履歴 (古い順)。CPU/メモリ/ディスクは 0..1、ネットワークは受信バイト/秒 */
static float g_hUp[HIST];        /* ネットワークの送信バイト/秒 */
static int g_hn;                 /* 履歴が溜まっている数 */
static float g_sc = 1.0f;        /* 大きさ(%) x DPI */

/* CPU / ネットワークの前回値 */
static BOOL g_cpuInit, g_netInit;
static ULONGLONG g_pI, g_pK, g_pU;
static ULONGLONG g_pIn, g_pOut, g_pTick;

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

/* ------------------------------------------------------------------ */
/* 保存 / 読み込み (sysmon.ini)                                           */
/* ------------------------------------------------------------------ */
static void SaveSysMon(void)
{
    wchar_t v[32];
#define PUT(key, val)                                         \
    do                                                        \
    {                                                         \
        _snwprintf(v, 32, L"%d", (int)(val));                 \
        WritePrivateProfileStringW(L"SysMon", key, v, g_ini); \
    } while (0)
    PUT(L"enabled", g_on);
    PUT(L"variant", g_var);
    PUT(L"cpu", g_item[0]);
    PUT(L"mem", g_item[1]);
    PUT(L"disk", g_item[2]);
    PUT(L"net", g_item[3]);
    PUT(L"interval", g_intv);
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
        wcscpy(slash + 1, L"sysmon.ini");
    else
        wcscpy(g_ini, L"sysmon.ini");

    g_on = GetPrivateProfileIntW(L"SysMon", L"enabled", 0, g_ini) != 0;
    g_var = ClampI((int)GetPrivateProfileIntW(L"SysMon", L"variant", 0, g_ini), 0, N_VARIANTS - 1);
    g_item[0] = GetPrivateProfileIntW(L"SysMon", L"cpu", 1, g_ini) != 0;
    g_item[1] = GetPrivateProfileIntW(L"SysMon", L"mem", 1, g_ini) != 0;
    g_item[2] = GetPrivateProfileIntW(L"SysMon", L"disk", 1, g_ini) != 0;
    g_item[3] = GetPrivateProfileIntW(L"SysMon", L"net", 1, g_ini) != 0;
    if (!g_item[0] && !g_item[1] && !g_item[2] && !g_item[3])
        g_item[0] = TRUE; /* 空のモニタにはしない */
    g_intv = ClampI((int)GetPrivateProfileIntW(L"SysMon", L"interval", 1, g_ini), 0, 2);
    g_size = ClampI((int)GetPrivateProfileIntW(L"SysMon", L"size", 100, g_ini), 60, 200);
    g_opa = ClampI((int)GetPrivateProfileIntW(L"SysMon", L"opacity", 78, g_ini), 30, 100);
    g_movable = GetPrivateProfileIntW(L"SysMon", L"movable", 1, g_ini) != 0;
    g_x = (int)GetPrivateProfileIntW(L"SysMon", L"x", POS_AUTO, g_ini);
    g_y = (int)GetPrivateProfileIntW(L"SysMon", L"y", POS_AUTO, g_ini);
}

static int NumItems(void)
{
    int n = 0;
    for (int i = 0; i < N_ITEMS; i++)
        n += g_item[i] ? 1 : 0;
    return n;
}

/* ------------------------------------------------------------------ */
/* 値の取得                                                              */
/* ------------------------------------------------------------------ */
static ULONGLONG FtToU64(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static void SampleCpu(void)
{
    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi, &fk, &fu))
        return;
    ULONGLONG I = FtToU64(fi), K = FtToU64(fk), U = FtToU64(fu); /* K にはアイドル時間も含まれる */
    if (g_cpuInit)
    {
        ULONGLONG dI = I - g_pI, dT = (K - g_pK) + (U - g_pU);
        if (dT > 0)
            g_m.cpu = Clamp01(1.0f - (float)((double)dI / (double)dT));
    }
    g_pI = I;
    g_pK = K;
    g_pU = U;
    g_cpuInit = TRUE;
}

static void SampleMem(void)
{
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return;
    const double GB = 1073741824.0;
    g_m.mem = Clamp01(ms.dwMemoryLoad / 100.0f);
    g_m.memTotGB = (double)ms.ullTotalPhys / GB;
    g_m.memUsedGB = (double)(ms.ullTotalPhys - ms.ullAvailPhys) / GB;
}

static void SampleDisk(void)
{
    wchar_t wd[MAX_PATH], root[4] = L"C:\\";
    if (GetWindowsDirectoryW(wd, MAX_PATH) >= 2 && wd[1] == L':')
        root[0] = wd[0];
    ULARGE_INTEGER avail, total, freeb;
    if (!GetDiskFreeSpaceExW(root, &avail, &total, &freeb) || total.QuadPart == 0)
        return;
    g_m.disk = Clamp01(1.0f - (float)((double)freeb.QuadPart / (double)total.QuadPart));
    g_m.diskFreeGB = (double)freeb.QuadPart / 1073741824.0;
}

/* 実機のネットワークアダプタ (Wi-Fi / 有線) の合計。VPN など仮想のものは二重に数えないよう除く */
static void SampleNet(void)
{
    MIB_IF_TABLE2 *t = NULL;
    if (GetIfTable2(&t) != NO_ERROR || !t)
        return;
    ULONGLONG in = 0, out = 0;
    for (ULONG i = 0; i < t->NumEntries; i++)
    {
        const MIB_IF_ROW2 *r = &t->Table[i];
        if (r->Type == IF_TYPE_SOFTWARE_LOOPBACK || r->OperStatus != IfOperStatusUp)
            continue;
        if (!r->InterfaceAndOperStatusFlags.HardwareInterface)
            continue;
        in += r->InOctets;
        out += r->OutOctets;
    }
    FreeMibTable(t);

    ULONGLONG now = GetTickCount64();
    if (g_netInit && now > g_pTick && in >= g_pIn && out >= g_pOut)
    {
        double sec = (double)(now - g_pTick) / 1000.0;
        g_m.down = (double)(in - g_pIn) / sec;
        g_m.up = (double)(out - g_pOut) / sec;
        g_m.peak = fmax(1048576.0, fmax(g_m.peak * 0.97, fmax(g_m.down, g_m.up))); /* 棒の目盛りは最近の最大値 (最小 1MB/s) */
    }
    else if (!g_netInit)
        g_m.peak = 1048576.0;
    g_pIn = in;
    g_pOut = out;
    g_pTick = now;
    g_netInit = TRUE;
}

static void SampleAll(void)
{
    SampleCpu();
    if (g_item[1])
        SampleMem();
    if (g_item[2])
        SampleDisk();
    if (g_item[3])
        SampleNet();
}

/* 今の値を履歴の末尾に足す (タイマーの度に1回) */
static void PushHist(void)
{
    for (int i = 0; i < N_ITEMS; i++)
        memmove(&g_h[i][0], &g_h[i][1], (HIST - 1) * sizeof(float));
    memmove(&g_hUp[0], &g_hUp[1], (HIST - 1) * sizeof(float));
    g_h[0][HIST - 1] = g_m.cpu;
    g_h[1][HIST - 1] = g_m.mem;
    g_h[2][HIST - 1] = g_m.disk;
    g_h[3][HIST - 1] = (float)g_m.down;
    g_hUp[HIST - 1] = (float)g_m.up;
    if (g_hn < HIST)
        g_hn++;
}

static void FmtRate(double bps, wchar_t *out, int cch)
{
    double kb = bps / 1024.0;
    if (bps < 1024.0)
        _snwprintf(out, cch, L"%d B/s", (int)bps);
    else if (kb < 10.0)
        _snwprintf(out, cch, L"%.1f KB/s", kb);
    else if (kb < 1000.0)
        _snwprintf(out, cch, L"%.0f KB/s", kb);
    else if (kb / 1024.0 < 100.0)
        _snwprintf(out, cch, L"%.1f MB/s", kb / 1024.0);
    else
        _snwprintf(out, cch, L"%.0f MB/s", kb / 1024.0);
    out[cch - 1] = 0;
}

/* ------------------------------------------------------------------ */
/* フォント / 文字 (GDI で白文字をマスクに描いて合成。時計と同じ方式)           */
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
    for (int i = 0; i < (int)(sizeof(SM_FONTS) / sizeof(SM_FONTS[0])); i++)
    {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        wcsncpy(lf.lfFaceName, SM_FONTS[i], LF_FACESIZE - 1);
        BOOL ok = FALSE;
        EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)FontFoundProc, (LPARAM)&ok, 0);
        if (ok)
        {
            found = SM_FONTS[i];
            break;
        }
    }
    ReleaseDC(NULL, dc);
    return found;
}

static Mask MakeMask(HFONT font, const wchar_t *s, int n)
{
    Mask m = {NULL, 0, 0};
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

static HFONT MkFont(int px, int weight, const wchar_t *name)
{
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
}

static void FreeFonts(void)
{
    if (g_mdc)
        SelectObject(g_mdc, GetStockObject(SYSTEM_FONT));
    HFONT *fs[] = {&g_fBold, &g_fNorm, &g_fSmall, &g_fBig};
    for (int i = 0; i < 4; i++)
        if (*fs[i])
        {
            DeleteObject(*fs[i]);
            *fs[i] = NULL;
        }
}

static int TextH(HFONT f)
{
    TEXTMETRICW tm;
    SelectObject(g_mdc, f);
    GetTextMetricsW(g_mdc, &tm);
    return tm.tmHeight;
}

static void RebuildFonts(void)
{
    FreeFonts();
    g_sc = g_size / 100.0f * g_dpi;
    const wchar_t *name = PickFontName();
    int px = (int)(12.0f * g_sc + 0.5f), small = (int)(11.0f * g_sc + 0.5f), big = (int)(15.0f * g_sc + 0.5f);
    g_fBold = MkFont(px < 8 ? 8 : px, FW_SEMIBOLD, name);
    g_fNorm = MkFont(px < 8 ? 8 : px, FW_NORMAL, name);
    g_fSmall = MkFont(small < 8 ? 8 : small, FW_NORMAL, name);
    g_fBig = MkFont(big < 9 ? 9 : big, FW_BOLD, name);
}

/* ------------------------------------------------------------------ */
/* 描画プリミティブ                                                      */
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

static void FillRRect(float L, float T, float R, float B, float rad, float r, float g, float bl, float a)
{
    if (R <= L || B <= T)
        return;
    int x0 = ClampI((int)L - 1, 0, g_W), x1 = ClampI((int)R + 2, 0, g_W);
    int y0 = ClampI((int)T - 1, 0, g_H), y1 = ClampI((int)B + 2, 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float cov = Clamp01(0.5f - RRectDist(x + 0.5f, y + 0.5f, L, T, R, B, rad));
            if (cov > 0)
                BlendPx(x, y, r, g, bl, a * cov);
        }
}

/* 背景: 枠線・グラデーションなしの単色チャコール半透明 (全面をつかめるよう、背景が薄くても alpha>=1 で塗る) */
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

static void HitFill(void)
{
    for (int i = 0; i < g_W * g_H; i++)
        g_bits[i] = 0x01000000u; /* 完全に透明な所はクリックが通り抜けるので、ほぼ透明で塗る */
}

/* 負荷に応じた色: 低 = 青 / 高 = 黄 / 危険 = 赤 */
static void LoadColor(float f, float *r, float *g, float *b)
{
    if (f < 0.70f)
    {
        *r = 96;
        *g = 168;
        *b = 255;
    }
    else if (f < 0.90f)
    {
        *r = 255;
        *g = 196;
        *b = 84;
    }
    else
    {
        *r = 255;
        *g = 98;
        *b = 98;
    }
}

/* al: 0 左 / 1 中央 / 2 右 */
static void Txt(HFONT f, const wchar_t *s, float x, float y, int al, float r, float g, float b, float a)
{
    Mask m = MakeMask(f, s, (int)wcslen(s));
    if (!m.p)
        return;
    int dx = (int)x;
    if (al == 1)
        dx -= m.w / 2;
    else if (al == 2)
        dx -= m.w;
    for (int yy = 0; yy < m.h; yy++)
    {
        int py = (int)y + yy;
        if (py < 0 || py >= g_H)
            continue;
        for (int xx = 0; xx < m.w; xx++)
        {
            int px = dx + xx;
            if (px < 0 || px >= g_W)
                continue;
            uint8_t v = m.p[yy * m.w + xx];
            if (v)
                BlendPx(px, py, r, g, b, v / 255.0f * a);
        }
    }
    FreeMask(&m);
}

/* リング: 上から時計回りに frac だけ塗る */
static void DrawGauge(float cx, float cy, float rad, float th, float frac, float r, float g, float b)
{
    int x0 = ClampI((int)(cx - rad - 2), 0, g_W), x1 = ClampI((int)(cx + rad + 3), 0, g_W);
    int y0 = ClampI((int)(cy - rad - 2), 0, g_H), y1 = ClampI((int)(cy + rad + 3), 0, g_H);
    float ang = Clamp01(frac) * 2.0f * PI_F;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = Clamp01(0.5f - (fabsf(d - (rad - th * 0.5f)) - th * 0.5f));
            if (cov <= 0)
                continue;
            BlendPx(x, y, 255, 255, 255, 0.16f * cov); /* 土台 */
            float a = atan2f(dx, -dy);                 /* 真上が0、時計回りに増える */
            if (a < 0)
                a += 2.0f * PI_F;
            if (a <= ang)
                BlendPx(x, y, r, g, b, cov);
        }
}

/* ------------------------------------------------------------------ */
/* 寸法 / 描画                                                           */
/* ------------------------------------------------------------------ */
static void ItemValue(int i, wchar_t *out, int cch)
{
    switch (i)
    {
    case 0:
        _snwprintf(out, cch, L"%d%%", (int)(g_m.cpu * 100.0f + 0.5f));
        break;
    case 1:
        _snwprintf(out, cch, L"%d%%  %.1f / %.1f GB", (int)(g_m.mem * 100.0f + 0.5f), g_m.memUsedGB, g_m.memTotGB);
        break;
    case 2:
        _snwprintf(out, cch, L"%d%%  空き %.0f GB", (int)(g_m.disk * 100.0f + 0.5f), g_m.diskFreeGB);
        break;
    default:
    {
        wchar_t a[24], b[24];
        FmtRate(g_m.down, a, 24);
        FmtRate(g_m.up, b, 24);
        _snwprintf(out, cch, L"\u2193 %s  \u2191 %s", a, b);
        break;
    }
    }
    out[cch - 1] = 0;
}

static float ItemFrac(int i)
{
    switch (i)
    {
    case 0:
        return g_m.cpu;
    case 1:
        return g_m.mem;
    case 2:
        return g_m.disk;
    default:
        return g_m.peak > 0 ? (float)Clamp01((float)(g_m.down / g_m.peak)) : 0.0f;
    }
}

static const float kPillW[N_ITEMS] = {96.0f, 112.0f, 112.0f, 184.0f}; /* ピルの幅 (倍率1のとき) */
#define GRAPH_H 34.0f

/* ウィンドウの大きさ (フォントを作ったあとで呼ぶ) */
static void CalcSize(int *w, int *h)
{
    int n = NumItems();
    float sc = g_sc, pad = 16.0f * sc;
    float lh = (float)TextH(g_fBold), sh = (float)TextH(g_fSmall), bh = (float)TextH(g_fBig);
    switch (g_var)
    {
    case 1: /* リング */
    {
        float cell = 86.0f * sc, R = 27.0f * sc;
        *w = (int)(pad * 2 + n * cell);
        *h = (int)(pad + R * 2 + 7.0f * sc + sh + pad);
        break;
    }
    case 2: /* コンパクト */
        *w = (int)(236.0f * sc);
        *h = (int)(pad * 2 + n * 24.0f * sc - 4.0f * sc);
        break;
    case 3: /* 線グラフ */
    case 4: /* エリアグラフ */
        *w = (int)(290.0f * sc);
        *h = (int)(pad * 2 + n * (lh + 4.0f * sc + GRAPH_H * sc) + (n - 1) * 10.0f * sc);
        break;
    case 6: /* 半円ゲージ */
    {
        float cell = 104.0f * sc, R = 40.0f * sc;
        *w = (int)(pad * 2 + n * cell);
        *h = (int)(pad + R + 5.0f * sc + sh + pad);
        (void)bh;
        break;
    }
    case 7: /* 縦バー */
    {
        float cell = 72.0f * sc;
        *w = (int)(pad * 2 + n * cell);
        *h = (int)(pad + sh + 4.0f * sc + 64.0f * sc + 5.0f * sc + sh + pad);
        break;
    }
    case 8: /* ピル */
    {
        float pd = 8.0f * sc, wsum = 0;
        for (int i = 0; i < N_ITEMS; i++)
            if (g_item[i])
                wsum += kPillW[i] * sc;
        *w = (int)(pd * 2 + wsum + (n - 1) * 8.0f * sc);
        *h = (int)(pd * 2 + lh + 10.0f * sc);
        break;
    }
    default: /* バー / セグメント */
        *w = (int)(290.0f * sc);
        *h = (int)(pad * 2 + n * 38.0f * sc - 10.0f * sc);
        break;
    }
}

/* ---- 線グラフ用 ---- */
static void DrawLine(float ax, float ay, float bx, float by, float th, float r, float g, float bl, float a)
{
    int x0 = ClampI((int)(fminf(ax, bx) - th - 1), 0, g_W), x1 = ClampI((int)(fmaxf(ax, bx) + th + 2), 0, g_W);
    int y0 = ClampI((int)(fminf(ay, by) - th - 1), 0, g_H), y1 = ClampI((int)(fmaxf(ay, by) + th + 2), 0, g_H);
    float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f, py = y + 0.5f;
            float t = len2 > 0 ? Clamp01(((px - ax) * dx + (py - ay) * dy) / len2) : 0.0f;
            float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
            float cov = Clamp01(th * 0.5f + 0.5f - sqrtf(ex * ex + ey * ey));
            if (cov > 0)
                BlendPx(x, y, r, g, bl, a * cov);
        }
}

/* ネットワークの縦軸: 履歴の最大値 (最小 1MB/s) */
static float NetScale(void)
{
    float m = 1048576.0f;
    for (int k = 0; k < HIST; k++)
    {
        m = fmaxf(m, g_h[3][k]);
        m = fmaxf(m, g_hUp[k]);
    }
    return m;
}

/* 履歴 k (0=最古) を 0..1 にして返す。up=TRUE はネットワークの送信 */
static float HistAt(int i, int k, BOOL up, float scale)
{
    if (i == 3)
        return Clamp01((up ? g_hUp[k] : g_h[3][k]) / scale);
    return Clamp01(g_h[i][k]);
}

/* 1項目の推移グラフ。fill=TRUE で線の下を塗る (エリアグラフ) */
static void DrawGraph(float L, float T, float R, float B, int item, BOOL fill)
{
    float sc = g_sc, w = R - L, h = B - T;
    FillRRect(L, T, R, B, 4.0f * sc, 255, 255, 255, 0.07f);
    FillRRect(L + 3.0f * sc, T + h * 0.5f - 0.5f, R - 3.0f * sc, T + h * 0.5f + 0.5f, 0, 255, 255, 255, 0.10f); /* 50% の目安線 */
    if (g_hn < 2)
        return;
    int start = HIST - g_hn;
    float stepx = w / (float)(HIST - 1), scale = item == 3 ? NetScale() : 1.0f;
    float top = T + 2.0f * sc, bot = B - 2.0f * sc;
    int series = item == 3 ? 2 : 1;
    for (int s = 0; s < series; s++)
    {
        BOOL up = s == 1;
        float r, g, b, last = HistAt(item, HIST - 1, up, scale);
        if (item == 3)
        {
            r = up ? 120.0f : 96.0f;
            g = up ? 220.0f : 168.0f;
            b = up ? 150.0f : 255.0f;
        }
        else
            LoadColor(last, &r, &g, &b);

        if (fill && !up)
        { /* 線の下をグラデーションで塗る */
            int xs = (int)(L + start * stepx);
            for (int x = xs; x < (int)R; x++)
            {
                float kf = (x + 0.5f - L) / stepx;
                int k0 = (int)kf;
                if (k0 < start || k0 + 1 >= HIST)
                    continue;
                float fr = kf - k0;
                float v = HistAt(item, k0, up, scale) * (1.0f - fr) + HistAt(item, k0 + 1, up, scale) * fr;
                float yl = bot - v * (bot - top);
                for (int y = (int)yl; y < (int)B - 1; y++)
                {
                    float depth = (y - yl) / fmaxf(bot - top, 1.0f);
                    BlendPx(x, y, r, g, b, 0.42f * (1.0f - Clamp01(depth) * 0.85f));
                }
            }
        }
        float px = 0, py = 0;
        for (int k = start; k < HIST; k++)
        {
            float x = L + k * stepx, y = bot - HistAt(item, k, up, scale) * (bot - top);
            if (k > start)
                DrawLine(px, py, x, y, 1.7f * sc, r, g, b, 1.0f);
            px = x;
            py = y;
        }
        if (!fill) /* 線グラフは、いまの値に点を打つ */
            FillRRect(px - 2.6f * sc, py - 2.6f * sc, px + 2.6f * sc, py + 2.6f * sc, 2.6f * sc, r, g, b, 1.0f);
    }
}

/* 半円 (左端から右端へ、上側を通る) */
static void DrawSemi(float cx, float cy, float rad, float th, float frac, float r, float g, float b)
{
    int x0 = ClampI((int)(cx - rad - 2), 0, g_W), x1 = ClampI((int)(cx + rad + 3), 0, g_W);
    int y0 = ClampI((int)(cy - rad - 2), 0, g_H), y1 = ClampI((int)(cy + 2), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dy > 0.5f)
                continue;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = Clamp01(0.5f - (fabsf(d - (rad - th * 0.5f)) - th * 0.5f));
            if (cov <= 0)
                continue;
            float t = (atan2f(dx, -dy) + PI_F * 0.5f) / PI_F; /* 左端 0 → 右端 1 */
            if (t < -0.01f || t > 1.01f)
                continue;
            BlendPx(x, y, 255, 255, 255, 0.16f * cov);
            if (t <= frac)
                BlendPx(x, y, r, g, b, cov);
        }
}

static void RenderBars(void)
{
    float sc = g_sc, pad = 16.0f * sc, rowH = 38.0f * sc;
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float y = pad + k * rowH;
        wchar_t v[96];
        ItemValue(i, v, 96);
        float lh = (float)TextH(g_fBold);
        Txt(g_fBold, kItemName[i], pad, y, 0, 255, 255, 255, 0.92f);
        Txt(g_fNorm, v, g_W - pad, y, 2, 255, 255, 255, 0.78f);
        float T = y + lh + 5.0f * sc, B = T + 6.0f * sc, L = pad, R = g_W - pad;
        FillRRect(L, T, R, B, 3.0f * sc, 255, 255, 255, 0.16f);
        float f = ItemFrac(i), r, g, b;
        LoadColor(i == 3 ? 0.0f : f, &r, &g, &b);
        float fw = (R - L) * f;
        if (fw > 0)
            FillRRect(L, T, L + (fw < 6.0f * sc ? 6.0f * sc : fw), B, 3.0f * sc, r, g, b, 1.0f);
        k++;
    }
}

static void RenderRings(void)
{
    float sc = g_sc, pad = 16.0f * sc, cell = 86.0f * sc, R = 27.0f * sc;
    float cy = pad + R;
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float cx = pad + k * cell + cell * 0.5f;
        if (i == 3)
        { /* ネットワークは輪ではなく、受信 / 送信の速さを2行で */
            wchar_t a[24], b[24], l1[40], l2[40];
            FmtRate(g_m.down, a, 24);
            FmtRate(g_m.up, b, 24);
            _snwprintf(l1, 40, L"\u2193 %s", a);
            _snwprintf(l2, 40, L"\u2191 %s", b);
            l1[39] = l2[39] = 0;
            float lh = (float)TextH(g_fSmall);
            Txt(g_fSmall, l1, cx, cy - lh - 1.0f * sc, 1, 255, 255, 255, 0.92f);
            Txt(g_fSmall, l2, cx, cy + 1.0f * sc, 1, 255, 255, 255, 0.78f);
        }
        else
        {
            float f = ItemFrac(i), r, g, b;
            LoadColor(f, &r, &g, &b);
            DrawGauge(cx, cy, R, 5.0f * sc, f, r, g, b);
            wchar_t v[16];
            _snwprintf(v, 16, L"%d%%", (int)(f * 100.0f + 0.5f));
            v[15] = 0;
            Txt(g_fBig, v, cx, cy - TextH(g_fBig) * 0.5f, 1, 255, 255, 255, 0.95f);
        }
        Txt(g_fSmall, kItemName[i], cx, cy + R + 7.0f * sc, 1, 255, 255, 255, 0.70f);
        k++;
    }
}

static void RenderCompact(void)
{
    float sc = g_sc, pad = 16.0f * sc, rowH = 24.0f * sc;
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float y = pad + k * rowH;
        wchar_t v[96];
        ItemValue(i, v, 96);
        float r = 255, g = 255, b = 255;
        if (i != 3)
            LoadColor(ItemFrac(i), &r, &g, &b);
        else
            r = g = b = 255;
        Txt(g_fBold, kItemName[i], pad, y, 0, 255, 255, 255, 0.80f);
        Txt(g_fNorm, v, g_W - pad, y, 2, r, g, b, 0.95f);
        k++;
    }
}

static void RenderGraphs(BOOL fill)
{
    float sc = g_sc, pad = 16.0f * sc, lh = (float)TextH(g_fBold), gh = GRAPH_H * sc;
    float rowH = lh + 4.0f * sc + gh + 10.0f * sc;
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float y = pad + k * rowH;
        wchar_t v[96];
        ItemValue(i, v, 96);
        Txt(g_fBold, kItemName[i], pad, y, 0, 255, 255, 255, 0.92f);
        Txt(g_fNorm, v, g_W - pad, y, 2, 255, 255, 255, 0.78f);
        DrawGraph(pad, y + lh + 4.0f * sc, g_W - pad, y + lh + 4.0f * sc + gh, i, fill);
        k++;
    }
}

/* セグメント: LED のメーター風 (低 = 緑 / 中 = 黄 / 高 = 赤) */
static void RenderSegments(void)
{
    float sc = g_sc, pad = 16.0f * sc, rowH = 38.0f * sc, lh = (float)TextH(g_fBold);
    const int NSEG = 20;
    float gap = 2.0f * sc, segW = ((g_W - 2 * pad) - (NSEG - 1) * gap) / NSEG;
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float y = pad + k * rowH;
        wchar_t v[96];
        ItemValue(i, v, 96);
        Txt(g_fBold, kItemName[i], pad, y, 0, 255, 255, 255, 0.92f);
        Txt(g_fNorm, v, g_W - pad, y, 2, 255, 255, 255, 0.78f);
        float T = y + lh + 5.0f * sc, H = 7.0f * sc;
        int lit = (int)(ItemFrac(i) * NSEG + 0.5f);
        for (int sidx = 0; sidx < NSEG; sidx++)
        {
            float x = pad + sidx * (segW + gap), pos = (sidx + 0.5f) / NSEG;
            float r = 110, g = 215, b = 130;
            if (pos >= 0.85f)
            {
                r = 255;
                g = 98;
                b = 98;
            }
            else if (pos >= 0.60f)
            {
                r = 255;
                g = 196;
                b = 84;
            }
            if (sidx < lit)
                FillRRect(x, T, x + segW, T + H, 1.5f * sc, r, g, b, 1.0f);
            else
                FillRRect(x, T, x + segW, T + H, 1.5f * sc, 255, 255, 255, 0.14f);
        }
        k++;
    }
}

static void RenderSemis(void)
{
    float sc = g_sc, pad = 16.0f * sc, cell = 104.0f * sc, R = 40.0f * sc;
    float cy = pad + R, sh = (float)TextH(g_fSmall);
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float cx = pad + k * cell + cell * 0.5f, f = ItemFrac(i), r, g, b;
        if (i == 3)
        { /* ネットワーク: 輪の中に受信、下に送信 (目盛りは受信) */
            wchar_t a[24], u[24], l1[40], l2[40];
            FmtRate(g_m.down, a, 24);
            FmtRate(g_m.up, u, 24);
            _snwprintf(l1, 40, L"\u2193 %s", a);
            _snwprintf(l2, 40, L"\u2191 %s", u);
            l1[39] = l2[39] = 0;
            r = 96;
            g = 168;
            b = 255;
            DrawSemi(cx, cy, R, 6.0f * sc, f, r, g, b);
            Txt(g_fSmall, l1, cx, cy - sh - 3.0f * sc, 1, 255, 255, 255, 0.92f);
            Txt(g_fSmall, l2, cx, cy + 5.0f * sc, 1, 255, 255, 255, 0.78f);
        }
        else
        {
            LoadColor(f, &r, &g, &b);
            DrawSemi(cx, cy, R, 6.0f * sc, f, r, g, b);
            wchar_t v[16];
            _snwprintf(v, 16, L"%d%%", (int)(f * 100.0f + 0.5f));
            v[15] = 0;
            Txt(g_fBig, v, cx, cy - TextH(g_fBig) - 1.0f * sc, 1, 255, 255, 255, 0.95f);
            Txt(g_fSmall, kItemName[i], cx, cy + 5.0f * sc, 1, 255, 255, 255, 0.70f);
        }
        k++;
    }
}

static void RenderVBars(void)
{
    float sc = g_sc, pad = 16.0f * sc, cell = 72.0f * sc, bw = 20.0f * sc, bh = 64.0f * sc;
    float sh = (float)TextH(g_fSmall), barT = pad + sh + 4.0f * sc, barB = barT + bh;
    int k = 0;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float cx = pad + k * cell + cell * 0.5f, f = ItemFrac(i), r, g, b;
        wchar_t v[24];
        if (i == 3)
            FmtRate(g_m.down, v, 24); /* 受信の速さ (棒もこれ) */
        else
            _snwprintf(v, 24, L"%d%%", (int)(f * 100.0f + 0.5f));
        v[23] = 0;
        Txt(g_fSmall, v, cx, pad, 1, 255, 255, 255, 0.90f);
        FillRRect(cx - bw * 0.5f, barT, cx + bw * 0.5f, barB, 6.0f * sc, 255, 255, 255, 0.16f);
        LoadColor(i == 3 ? 0.0f : f, &r, &g, &b);
        float fh = bh * f;
        if (fh > 0)
        {
            if (fh < 4.0f * sc)
                fh = 4.0f * sc;
            FillRRect(cx - bw * 0.5f, barB - fh, cx + bw * 0.5f, barB, fminf(6.0f * sc, fh * 0.5f), r, g, b, 1.0f);
        }
        Txt(g_fSmall, kItemName[i], cx, barB + 5.0f * sc, 1, 255, 255, 255, 0.70f);
        k++;
    }
}

static void RenderPills(void)
{
    float sc = g_sc, pd = 8.0f * sc, lh = (float)TextH(g_fBold), ph = lh + 10.0f * sc;
    float x = pd;
    for (int i = 0; i < N_ITEMS; i++)
    {
        if (!g_item[i])
            continue;
        float w = kPillW[i] * sc, f = ItemFrac(i), r, g, b;
        FillRRect(x, pd, x + w, pd + ph, ph * 0.5f, 28, 28, 32, g_opa / 100.0f);
        LoadColor(i == 3 ? 0.0f : f, &r, &g, &b);
        FillRRect(x + 10.0f * sc, pd + ph * 0.5f - 4.0f * sc, x + 18.0f * sc, pd + ph * 0.5f + 4.0f * sc, 4.0f * sc, r, g, b, 1.0f);
        wchar_t t[64];
        if (i == 3)
        {
            wchar_t a[24], u[24];
            FmtRate(g_m.down, a, 24);
            FmtRate(g_m.up, u, 24);
            _snwprintf(t, 64, L"\u2193 %s  \u2191 %s", a, u);
        }
        else
            _snwprintf(t, 64, L"%s %d%%", kItemName[i], (int)(f * 100.0f + 0.5f));
        t[63] = 0;
        Txt(g_fNorm, t, x + 24.0f * sc, pd + 5.0f * sc, 0, 255, 255, 255, 0.92f);
        x += w + 8.0f * sc;
    }
}

static void SmRender(void)
{
    if (!g_hwnd || !g_bits)
        return;
    memset(g_bits, 0, (size_t)g_W * g_H * 4);
    HitFill();
    if (g_var != 8) /* ピルは1つずつ背景を持つので、全体の背景は無し */
        DrawBg(14.0f * g_sc, g_opa / 100.0f);
    switch (g_var)
    {
    case 1:
        RenderRings();
        break;
    case 2:
        RenderCompact();
        break;
    case 3:
        RenderGraphs(FALSE);
        break;
    case 4:
        RenderGraphs(TRUE);
        break;
    case 5:
        RenderSegments();
        break;
    case 6:
        RenderSemis();
        break;
    case 7:
        RenderVBars();
        break;
    case 8:
        RenderPills();
        break;
    default:
        RenderBars();
        break;
    }

    POINT src = {0, 0};
    SIZE sz = {g_W, g_H};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, NULL, NULL, &sz, g_dc, &src, 0, &bf, ULW_ALPHA);
}

/* ------------------------------------------------------------------ */
/* 最奥への配置: デスクトップ本体 (Progman / WorkerW) の直上                 */
/* ------------------------------------------------------------------ */
static BOOL ClassIs(HWND w, const wchar_t *name)
{
    wchar_t c[32];
    return GetClassNameW(w, c, 32) && wcscmp(c, name) == 0;
}

static BOOL IsDesktopHost(HWND w) { return ClassIs(w, L"Progman") || ClassIs(w, L"WorkerW"); }

static BOOL IsOurs(HWND w)
{
    return ClassIs(w, L"PromSysMonWnd") || ClassIs(w, L"PromClockWnd") || ClassIs(w, L"PromPhotoFrameWnd") ||
           ClassIs(w, L"PromSearchBarWnd") || ClassIs(w, L"PromWeatherWnd");
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
    const int keep = 40; /* はみ出しても構わないが、つかめる部分は必ず画面内に残す */
    g_x = ClampI(g_x, mi.rcWork.left - g_W + keep, mi.rcWork.right - keep);
    g_y = ClampI(g_y, mi.rcWork.top - g_H + keep, mi.rcWork.bottom - keep);
}

static void AutoPos(void)
{ /* 初期位置: 右上 (時計の下あたり) */
    POINT o = {0, 0};
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
    g_x = mi.rcWork.right - g_W - (int)(40 * g_dpi);
    g_y = mi.rcWork.top + (int)(260 * g_dpi);
}

static LRESULT CALLBACK SmProc(HWND h, UINT m, WPARAM w, LPARAM l);

static void SetTick(UINT ms)
{
    if (g_hwnd)
        SetTimer(g_hwnd, TIMER_TICK, ms, NULL);
}

static void SmCreate(void)
{
    if (g_hwnd || !g_on || !g_ready)
        return;
    RebuildFonts();
    CalcSize(&g_W, &g_H);
    if (g_x == POS_AUTO || g_y == POS_AUTO)
        AutoPos();
    ClampToWork();
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"PromSysMonWnd", L"PromSysMon", WS_POPUP, g_x, g_y, g_W,
                             g_H, NULL, NULL, g_hInst, NULL);
    if (!g_hwnd)
        return;
    if (!MakeBuffers())
    {
        DestroyWindow(g_hwnd);
        return;
    }
    g_cpuInit = g_netInit = FALSE;
    SampleAll(); /* 基準値 (CPU・通信量は差分で求めるので、2回目から値が出る) */
    SmRender();
    ZPlace();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    ZPlace();
    g_warm = TRUE;
    SetTick(500); /* 最初だけ早めに1回読んで、すぐ値を出す */
    SetTimer(g_hwnd, TIMER_Z, 1000, NULL);
}

static void SmDestroyWin(void)
{
    if (g_hwnd)
        DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    FreeBuffers();
}

/* 大きさ・デザイン・項目が変わったとき: 窓の大きさごと作り直す (位置は左上を保つ) */
static void SmRelayout(void)
{
    if (!g_hwnd)
        return;
    FreeBuffers();
    RebuildFonts();
    CalcSize(&g_W, &g_H);
    SetWindowPos(g_hwnd, NULL, 0, 0, g_W, g_H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (MakeBuffers())
    {
        SampleAll();
        SmRender();
    }
}

/* ------------------------------------------------------------------ */
/* 右クリックメニュー / ウィンドウ                                          */
/* ------------------------------------------------------------------ */
static void ShowSmMenu(void)
{
    HMENU menu = CreatePopupMenu();
    {
        HMENU sub = CreatePopupMenu();
        for (int i = 0; i < N_VARIANTS; i++)
            AppendMenuW(sub, MF_STRING | (g_var == i ? MF_CHECKED : 0), MENU_VAR + i, kVarName[i]);
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"デザイン");
    }
    {
        HMENU sub = CreatePopupMenu();
        for (int i = 0; i < N_ITEMS; i++)
            AppendMenuW(sub, MF_STRING | (g_item[i] ? MF_CHECKED : 0), MENU_ITEM + i, kItemName[i]);
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"表示する項目");
    }
    {
        HMENU sub = CreatePopupMenu();
        for (int i = 0; i < 3; i++)
            AppendMenuW(sub, MF_STRING | (g_intv == i ? MF_CHECKED : 0), MENU_INT + i, kIntName[i]);
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"更新間隔");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_movable ? MF_CHECKED : 0), MENU_MOVABLE, L"ドラッグで移動できるようにする");
    AppendMenuW(menu, MF_STRING, MENU_HIDE, L"システムモニタを消す");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"アプリ設定…");
    POINT pt;
    GetCursorPos(&pt);
    HWND h = g_hwnd;
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
    DestroyMenu(menu);
    PostMessageW(h, WM_NULL, 0, 0);
    if (cmd >= MENU_VAR && cmd < MENU_VAR + N_VARIANTS)
        SysMon_SetVariant(cmd - MENU_VAR);
    else if (cmd >= MENU_ITEM && cmd < MENU_ITEM + N_ITEMS)
        SysMon_SetItem(cmd - MENU_ITEM, !g_item[cmd - MENU_ITEM]);
    else if (cmd >= MENU_INT && cmd < MENU_INT + 3)
        SysMon_SetInterval(cmd - MENU_INT);
    else if (cmd == MENU_MOVABLE)
        SysMon_SetMovable(!g_movable);
    else if (cmd == MENU_HIDE)
        SysMon_SetEnabled(FALSE); /* 窓が破棄されるので、これ以降触らない */
    else if (cmd == MENU_SETTINGS)
    {
        Settings_OpenPage(8);
        return;
    }
    else
        return;
    Settings_RefreshSysMon();
}

static LRESULT CALLBACK SmProc(HWND h, UINT m, WPARAM w, LPARAM l)
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
        SaveSysMon();
        return 0;
    }

    case WM_TIMER:
        if (w == TIMER_TICK)
        {
            if (g_warm)
            { /* 最初の早い1回が済んだら、設定した間隔に切り替える */
                g_warm = FALSE;
                SetTick(kIntMs[g_intv]);
            }
            SampleAll();
            PushHist();
            SmRender();
        }
        else if (w == TIMER_Z)
            ZPlace();
        return 0;

    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP: /* 移動できる状態では全面が HTCAPTION なので、ここで右クリックを受ける */
    case WM_CONTEXTMENU:
        ShowSmMenu();
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
BOOL SysMon_Enabled(void)
{
    EnsureLoaded();
    return g_on;
}

void SysMon_SetEnabled(BOOL on)
{
    EnsureLoaded();
    on = on ? TRUE : FALSE;
    if (on == g_on)
        return;
    g_on = on;
    SaveSysMon();
    if (!g_ready)
        return;
    if (on)
        SmCreate();
    else
        SmDestroyWin();
}

int SysMon_VariantCount(void) { return N_VARIANTS; }

const wchar_t *SysMon_VariantName(int i) { return kVarName[ClampI(i, 0, N_VARIANTS - 1)]; }

int SysMon_GetVariant(void)
{
    EnsureLoaded();
    return g_var;
}

void SysMon_SetVariant(int v)
{
    EnsureLoaded();
    v = ClampI(v, 0, N_VARIANTS - 1);
    if (v == g_var)
        return;
    g_var = v;
    SaveSysMon();
    SmRelayout();
}

BOOL SysMon_GetItem(int i)
{
    EnsureLoaded();
    return (i >= 0 && i < N_ITEMS) ? g_item[i] : FALSE;
}

void SysMon_SetItem(int i, BOOL on)
{
    EnsureLoaded();
    if (i < 0 || i >= N_ITEMS)
        return;
    on = on ? TRUE : FALSE;
    if (on == g_item[i])
        return;
    if (!on && NumItems() <= 1)
        return; /* 最後の1つは消せない (空のモニタになるため) */
    g_item[i] = on;
    if (on)
    { /* 消えていた間の古い値は捨てる */
        memset(g_h[i], 0, sizeof(g_h[i]));
        if (i == 3)
            memset(g_hUp, 0, sizeof(g_hUp));
    }
    SaveSysMon();
    SmRelayout();
}

int SysMon_GetInterval(void)
{
    EnsureLoaded();
    return g_intv;
}

void SysMon_SetInterval(int i)
{
    EnsureLoaded();
    i = ClampI(i, 0, 2);
    if (i == g_intv)
        return;
    g_intv = i;
    SaveSysMon();
    if (g_hwnd && !g_warm)
        SetTick(kIntMs[g_intv]);
}

int SysMon_GetSize(void)
{
    EnsureLoaded();
    return g_size;
}

void SysMon_SetSize(int pct)
{
    EnsureLoaded();
    pct = ClampI(pct, 60, 200);
    if (pct == g_size)
        return;
    g_size = pct;
    SaveSysMon();
    SmRelayout();
}

int SysMon_GetOpacity(void)
{
    EnsureLoaded();
    return g_opa;
}

void SysMon_SetOpacity(int pct)
{
    EnsureLoaded();
    pct = ClampI(pct, 30, 100);
    if (pct == g_opa)
        return;
    g_opa = pct;
    SaveSysMon();
    SmRender();
}

BOOL SysMon_Movable(void)
{
    EnsureLoaded();
    return g_movable;
}

void SysMon_SetMovable(BOOL on)
{
    EnsureLoaded();
    g_movable = on ? TRUE : FALSE;
    SaveSysMon();
}

void SysMon_ResetPosition(void)
{
    EnsureLoaded();
    g_x = g_y = POS_AUTO;
    if (g_hwnd)
    {
        AutoPos();
        ClampToWork();
        SetWindowPos(g_hwnd, NULL, g_x, g_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SaveSysMon();
}

void SysMon_Init(HINSTANCE hInst)
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
    wc.lpfnWndProc = SmProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromSysMonWnd";
    RegisterClassExW(&wc);

    g_ready = TRUE;
    SmCreate();
    if (g_hwnd)
        SaveSysMon(); /* 自動配置した位置を記録 */
}

void SysMon_Shutdown(void)
{
    g_ready = FALSE;
    SmDestroyWin();
    FreeFonts();
    if (g_mdc)
        DeleteDC(g_mdc);
    g_mdc = NULL;
}