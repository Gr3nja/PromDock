/*
 * wallpaper.c - 動く壁紙 (Lively Wallpaper 風の軽量版)。promdock.c / setting.c などと同じ exe にビルドします。
 *
 *   デスクトップのアイコンの「後ろ」(= 壁紙の位置) に専用ウィンドウを置いて、アニメーションを描きます。
 *   (Progman に 0x052C を送って WorkerW を作り、その子にする定番の方法。Windows 11 24H2 にも対応)
 *
 *   種類
 *     内蔵アニメ : オーロラ / 星空 / デジタルレイン / ウェーブ / バブル  (すべて CPU 描画・追加ファイル不要)
 *     画像・GIF  : 好きな画像 / GIF アニメを壁紙にする (画面いっぱいに「拡大して切り抜き」)
 *     動画 (mp4) : Media Foundation で別スレッドにデコードして描く (音なし・ループ再生・画面いっぱいに「拡大して切り抜き」)
 *                  H.264 の mp4 / m4v / mov / wmv / avi など Windows が標準で再生できる形式。HEVC は Windows の拡張機能が必要
 *   設定
 *     フレームレート (15 / 30 / 60) と 速さ (ゆっくり / ふつう / はやい) を選べます
 *     フルスクリーンのアプリ (ゲーム・動画・プレゼン) が前面にあるときは自動で一時停止して、CPU を使いません
 *     保存先: exe と同じフォルダの wallpaper.ini
 *       [Wallpaper]
 *       enabled=0  kind=0  fps=1 (0:15 1:30 2:60)  speed=1 (0:遅 1:普 2:速)  file=C:\...\xxx.gif  video=C:\...\xxx.mp4
 *
 * 外部から呼ぶ関数 (setting.c が呼びます。promdock.c の変更は不要):
 *     Wallpaper_Init(hInst) / Wallpaper_Shutdown()
 *     Wallpaper_Enabled / SetEnabled / GetKind / SetKind / KindCount / KindName / KindIsFile
 *     Wallpaper_GetFps / SetFps / GetSpeed / SetSpeed / HasFile / FileLabel / PickFile
 *     Wallpaper_KindIsVideo / HasVideo / VideoLabel / PickVideo
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
#include <shellapi.h>
#include <commdlg.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#endif

/* ---- GDI+ フラット API (photoframe.c と同じ方式。ヘッダ不要) ---- */
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
int WINAPI GdipLoadImageFromFile(const WCHAR *filename, void **image);
int WINAPI GdipDisposeImage(void *image);
int WINAPI GdipGetImageWidth(void *image, UINT *width);
int WINAPI GdipGetImageHeight(void *image, UINT *height);
int WINAPI GdipImageGetFrameCount(void *image, const GUID *dimensionID, UINT *count);
int WINAPI GdipImageSelectActiveFrame(void *image, const GUID *dimensionID, UINT frameIndex);
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
#define GP_INTERP_HQ_BILINEAR 6
#define GP_PIXELOFFSET_HALF 4
#define GP_TAG_FRAMEDELAY 0x5100
static const GUID kDimTime = {0x6aedbd6d, 0x3fb5, 0x418a, {0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72}};

void Settings_OpenPage(int page); /* 7: 壁紙のページ */

/* ------------------------------------------------------------------ */
/* 種類・設定                                                            */
/* ------------------------------------------------------------------ */
enum
{
    WK_AURORA = 0,
    WK_STARS,
    WK_RAIN,
    WK_WAVE,
    WK_BUBBLE,
    WK_IMAGE,
    WK_VIDEO,
    WK_COUNT
};
static const wchar_t *const kKindName[WK_COUNT] = {L"オーロラ", L"星空", L"デジタルレイン", L"ウェーブ",
                                                   L"バブル", L"画像・GIF", L"動画 (mp4)"};
/* 内部バッファの横幅の上限 (これを画面いっぱいに引き伸ばす = 軽い)。画像は高画質のため大きめ */
static const int kBufCap[WK_COUNT] = {640, 1280, 1280, 640, 1280, 1920, 640};
static const int kFpsVal[3] = {15, 30, 60};
static const float kSpeedMul[3] = {0.5f, 1.0f, 2.0f};

#define TM_FRAME 1
#define TM_WATCH 2

static HINSTANCE g_hInst;
static wchar_t g_ini[MAX_PATH];
static BOOL g_on, g_inited;
static int g_kind = WK_AURORA, g_fps = 1, g_speed = 1;
static wchar_t g_file[MAX_PATH];  /* 画像・GIF */
static wchar_t g_vfile[MAX_PATH]; /* 動画 */

static HWND g_host, g_wnd;
static int g_vx, g_vy, g_vw, g_vh; /* 仮想スクリーン (全モニター) */
static uint32_t *g_bits;           /* 内部バッファ (0x00RRGGBB, 上から下) */
static int g_bw, g_bh;
static BITMAPINFO g_bmi;
static float g_t; /* アニメの時間 (秒 x 速さ) */
static ULONGLONG g_last, g_lastChk;
static BOOL g_pause;

/* 画像・GIF */
static ULONG_PTR g_gdipToken;
static BOOL g_gdipOn;
static void *g_img;
static UINT g_iw, g_ih, g_frames, g_frame;
static long *g_delays;
static ULONGLONG g_nextFrame;
static BOOL g_dirty;

/* ------------------------------------------------------------------ */
/* 保存                                                                  */
/* ------------------------------------------------------------------ */
static int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void SaveIni(void)
{
    wchar_t b[32];
    WritePrivateProfileStringW(L"Wallpaper", L"enabled", g_on ? L"1" : L"0", g_ini);
    _snwprintf(b, 32, L"%d", g_kind);
    WritePrivateProfileStringW(L"Wallpaper", L"kind", b, g_ini);
    _snwprintf(b, 32, L"%d", g_fps);
    WritePrivateProfileStringW(L"Wallpaper", L"fps", b, g_ini);
    _snwprintf(b, 32, L"%d", g_speed);
    WritePrivateProfileStringW(L"Wallpaper", L"speed", b, g_ini);
    WritePrivateProfileStringW(L"Wallpaper", L"file", g_file, g_ini);
    WritePrivateProfileStringW(L"Wallpaper", L"video", g_vfile, g_ini);
}

static void LoadIni(void)
{
    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    wchar_t *sl = wcsrchr(g_ini, L'\\');
    if (sl)
        wcscpy(sl + 1, L"wallpaper.ini");
    else
        wcscpy(g_ini, L"wallpaper.ini");
    g_on = GetPrivateProfileIntW(L"Wallpaper", L"enabled", 0, g_ini) != 0;
    g_kind = Clampi((int)GetPrivateProfileIntW(L"Wallpaper", L"kind", 0, g_ini), 0, WK_COUNT - 1);
    g_fps = Clampi((int)GetPrivateProfileIntW(L"Wallpaper", L"fps", 1, g_ini), 0, 2);
    g_speed = Clampi((int)GetPrivateProfileIntW(L"Wallpaper", L"speed", 1, g_ini), 0, 2);
    GetPrivateProfileStringW(L"Wallpaper", L"file", L"", g_file, MAX_PATH, g_ini);
    GetPrivateProfileStringW(L"Wallpaper", L"video", L"", g_vfile, MAX_PATH, g_ini);
}

/* ------------------------------------------------------------------ */
/* 小物                                                                  */
/* ------------------------------------------------------------------ */
static uint32_t Hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static float Rnd(uint32_t seed) { return (Hash(seed) & 0xFFFFFF) / 16777216.0f; } /* 0..1 */

#define RGBX(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* d に c を a/256 の割合で混ぜる */
static uint32_t Mix(uint32_t d, uint32_t c, int a)
{
    int ia = 256 - a;
    uint32_t r = (((d >> 16) & 255) * ia + ((c >> 16) & 255) * a) >> 8;
    uint32_t g = (((d >> 8) & 255) * ia + ((c >> 8) & 255) * a) >> 8;
    uint32_t b = ((d & 255) * ia + (c & 255) * a) >> 8;
    return (r << 16) | (g << 8) | b;
}

static uint32_t HueRGB(float h, float s, float v)
{
    h -= floorf(h);
    float r, g, b, f = h * 6.0f;
    int i = (int)f;
    float ff = f - i, p = v * (1 - s), q = v * (1 - s * ff), t = v * (1 - s * (1 - ff));
    switch (i % 6)
    {
    case 0:
        r = v, g = t, b = p;
        break;
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    default:
        r = v, g = p, b = q;
        break;
    }
    return RGBX((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

static void FillRows(uint32_t top, uint32_t bot)
{
    for (int y = 0; y < g_bh; y++)
    {
        int a = y * 256 / (g_bh > 1 ? g_bh - 1 : 1);
        uint32_t c = Mix(top, bot, a);
        uint32_t *p = g_bits + (size_t)y * g_bw;
        for (int x = 0; x < g_bw; x++)
            p[x] = c;
    }
}

static void AddPx(int x, int y, int v) /* 加算 (明るく) */
{
    if (x < 0 || y < 0 || x >= g_bw || y >= g_bh)
        return;
    uint32_t *p = g_bits + (size_t)y * g_bw + x;
    int r = (int)((*p >> 16) & 255) + v, g = (int)((*p >> 8) & 255) + v, b = (int)(*p & 255) + v;
    *p = RGBX(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

/* ------------------------------------------------------------------ */
/* 内蔵アニメ                                                            */
/* ------------------------------------------------------------------ */
static void RenderAurora(float t)
{
    enum
    {
        MAXW = 2048
    };
    static float yc[3][MAXW], amp[3][MAXW];
    static const int col[3][3] = {{50, 255, 130}, {40, 190, 255}, {170, 80, 255}};
    int W = g_bw, H = g_bh;
    if (W > MAXW)
        W = MAXW;
    for (int k = 0; k < 3; k++)
        for (int x = 0; x < W; x++)
        {
            float nx = x / (float)W;
            yc[k][x] = H * (0.30f + 0.11f * k) + H * 0.10f * sinf(nx * (3.0f + k * 1.7f) + t * (0.25f + 0.1f * k) + k * 2.0f) +
                       H * 0.04f * sinf(nx * 9.0f - t * 0.6f + k);
            float a = 0.55f + 0.45f * sinf(nx * 5.0f + t * 0.4f + k * 1.3f);
            amp[k][x] = a < 0.12f ? 0.12f : a;
        }
    float up = H * 0.17f, dn = H * 0.035f;
    for (int y = 0; y < H; y++)
    {
        int br = 4 + y * 8 / H, bg = 8 + y * 18 / H, bb = 26 + y * 22 / H;
        uint32_t *p = g_bits + (size_t)y * g_bw;
        for (int x = 0; x < W; x++)
        {
            float r = (float)br, g = (float)bg, b = (float)bb;
            for (int k = 0; k < 3; k++)
            {
                float d = y - yc[k][x];
                d /= (d < 0 ? up : dn);
                float I = amp[k][x] / (1.0f + d * d);
                r += col[k][0] * I;
                g += col[k][1] * I;
                b += col[k][2] * I;
            }
            p[x] = RGBX(r > 255 ? 255 : (int)r, g > 255 ? 255 : (int)g, b > 255 ? 255 : (int)b);
        }
    }
}

enum
{
    NSTAR = 240
};
static void RenderStars(float t)
{
    int W = g_bw, H = g_bh;
    FillRows(RGBX(2, 3, 14), RGBX(18, 12, 44));
    for (int i = 0; i < NSTAR; i++)
    {
        float z = 0.25f + 0.75f * Rnd(i * 4 + 1);
        float x0 = Rnd(i * 4 + 2) * W, y = Rnd(i * 4 + 3) * H;
        float x = fmodf(x0 - t * 6.0f * z * (W / 640.0f), (float)W);
        if (x < 0)
            x += W;
        float tw = 0.65f + 0.35f * sinf(t * (1.0f + 2.0f * Rnd(i * 4)) + i);
        int v = (int)(255 * (0.30f + 0.70f * z) * tw);
        int xi = (int)x, yi = (int)y;
        AddPx(xi, yi, v);
        if (z > 0.8f)
        {
            AddPx(xi + 1, yi, v / 2);
            AddPx(xi - 1, yi, v / 2);
            AddPx(xi, yi + 1, v / 2);
            AddPx(xi, yi - 1, v / 2);
        }
    }
    /* 流れ星 (7秒ごとに1つ) */
    float cyc = t / 7.0f, ph = cyc - floorf(cyc);
    if (ph < 0.12f)
    {
        uint32_t id = (uint32_t)floorf(cyc);
        float sx = Rnd(id * 3 + 11) * W * 0.7f, sy = Rnd(id * 3 + 12) * H * 0.4f;
        float p = ph / 0.12f, L = W * 0.35f;
        for (int s = 0; s < 40; s++)
        {
            float q = p - s * 0.012f;
            if (q < 0)
                break;
            AddPx((int)(sx + q * L), (int)(sy + q * L * 0.45f), 255 - s * 6);
        }
    }
}

static void RenderRain(float t)
{
    enum
    {
        CW = 10,
        CH = 14
    };
    int W = g_bw, H = g_bh;
    int cols = W / CW + 1, rows = H / CH + 1;
    FillRows(RGBX(0, 4, 0), RGBX(0, 10, 4));
    for (int c = 0; c < cols; c++)
    {
        float spd = 5.0f + 11.0f * Rnd(c * 5 + 1);
        int len = 8 + (int)(Rnd(c * 5 + 2) * 14);
        float period = (float)(rows + len + 4 + (int)(Rnd(c * 5 + 3) * 14));
        float head = fmodf(t * spd + Rnd(c * 5 + 4) * period, period);
        int hr = (int)head;
        for (int idx = 0; idx < len; idx++)
        {
            int r = hr - idx;
            if (r < 0 || r >= rows)
                continue;
            float f = 1.0f - idx / (float)len;
            uint32_t color = idx == 0 ? RGBX(210, 255, 220) : RGBX(0, (int)(40 + 215 * f * f), (int)(10 + 50 * f * f));
            uint32_t bits = Hash((uint32_t)(c * 7919 + r * 104729 + (int)(t * (idx == 0 ? 10 : 2.5f)) * 31));
            int ox = c * CW, oy = r * CH;
            for (int gy = 0; gy < 6; gy++)
                for (int gx = 0; gx < 4; gx++)
                {
                    if (!((bits >> (gy * 4 + gx)) & 1))
                        continue;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                        {
                            int px = ox + 1 + gx * 2 + dx, py = oy + 1 + gy * 2 + dy;
                            if (px < W && py < H)
                                g_bits[(size_t)py * g_bw + px] = color;
                        }
                }
        }
    }
}

static void RenderWave(float t)
{
    int W = g_bw, H = g_bh;
    float h0 = 0.58f + t * 0.012f;
    FillRows(HueRGB(h0, 0.65f, 0.30f), HueRGB(h0 + 0.12f, 0.70f, 0.65f));
    for (int k = 0; k < 4; k++)
    {
        uint32_t c = HueRGB(h0 + 0.06f * k + 0.05f, 0.45f, 0.95f);
        int a = 60 + k * 28;
        for (int x = 0; x < W; x++)
        {
            float nx = x / (float)W;
            float yf = H * (0.50f + 0.10f * k) + H * 0.05f * sinf(nx * (5.0f + 2.0f * k) + t * (0.6f + 0.25f * k) + k * 1.7f) +
                       H * 0.02f * sinf(nx * 17.0f - t * 0.9f);
            int y0 = (int)yf;
            if (y0 < 0)
                y0 = 0;
            for (int y = y0; y < H; y++)
            {
                uint32_t *p = g_bits + (size_t)y * g_bw + x;
                *p = Mix(*p, c, a);
            }
        }
    }
}

enum
{
    NBUB = 36
};
static void RenderBubbles(float t)
{
    int W = g_bw, H = g_bh;
    float sc = W / 1280.0f;
    if (sc < 0.5f)
        sc = 0.5f;
    FillRows(RGBX(8, 28, 72), RGBX(4, 76, 118));
    for (int i = 0; i < NBUB; i++)
    {
        float r = (8.0f + 50.0f * Rnd(i * 6 + 1) * Rnd(i * 6 + 1)) * sc + 4.0f;
        float v = (18.0f + 34.0f * Rnd(i * 6 + 2)) * (H / 720.0f) * (0.6f + 20.0f / (r + 10.0f));
        float span = H + 2 * r;
        float cy = H + r - fmodf(t * v + Rnd(i * 6 + 3) * span, span);
        float cx = Rnd(i * 6 + 4) * W + sinf(t * (0.5f + Rnd(i * 6 + 5)) + i) * 14.0f * sc;
        int x0 = (int)(cx - r - 2), x1 = (int)(cx + r + 2), y0 = (int)(cy - r - 2), y1 = (int)(cy + r + 2);
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        if (x1 >= W)
            x1 = W - 1;
        if (y1 >= H)
            y1 = H - 1;
        float hx = cx - 0.38f * r, hy = cy - 0.38f * r, hr2 = (0.17f * r) * (0.17f * r);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
            {
                float dx = x - cx, dy = y - cy;
                float d2 = dx * dx + dy * dy;
                if (d2 > (r + 2) * (r + 2))
                    continue;
                float d = sqrtf(d2), e = fabsf(d - r);
                uint32_t *p = g_bits + (size_t)y * g_bw + x;
                if ((x - hx) * (x - hx) + (y - hy) * (y - hy) < hr2)
                    *p = Mix(*p, RGBX(255, 255, 255), 150);
                else if (e < 1.6f)
                    *p = Mix(*p, RGBX(200, 240, 255), (int)((1.6f - e) / 1.6f * 200));
                else if (d < r)
                    *p = Mix(*p, RGBX(150, 220, 255), 22);
            }
    }
}

/* ------------------------------------------------------------------ */
/* 画像・GIF                                                             */
/* ------------------------------------------------------------------ */
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

static void FreeImage(void)
{
    if (g_img)
        GdipDisposeImage(g_img);
    g_img = NULL;
    free(g_delays);
    g_delays = NULL;
    g_iw = g_ih = 0;
    g_frames = 1;
    g_frame = 0;
}

static void LoadImageFile(void)
{
    FreeImage();
    if (!g_file[0] || !GdipOn())
        return;
    void *img = NULL;
    if (GdipLoadImageFromFile(g_file, &img) != 0 || !img)
        return;
    g_img = img;
    GdipGetImageWidth(img, &g_iw);
    GdipGetImageHeight(img, &g_ih);
    UINT n = 1;
    if (GdipImageGetFrameCount(img, &kDimTime, &n) != 0 || n < 1)
        n = 1;
    g_frames = n;
    if (n > 1)
    {
        UINT sz = 0;
        if (GdipGetPropertyItemSize(img, GP_TAG_FRAMEDELAY, &sz) == 0 && sz >= sizeof(GpPropItem))
        {
            GpPropItem *it = (GpPropItem *)malloc(sz);
            if (it && GdipGetPropertyItem(img, GP_TAG_FRAMEDELAY, sz, it) == 0 && it->length >= n * 4)
            {
                g_delays = (long *)malloc(n * sizeof(long));
                if (g_delays)
                    memcpy(g_delays, it->value, n * sizeof(long));
            }
            free(it);
        }
        if (!g_delays)
            g_frames = 1; /* 遅延が読めない = 動かさない */
    }
    g_nextFrame = 0;
    g_dirty = TRUE;
}

static void RenderImage(void)
{
    memset(g_bits, 0, (size_t)g_bw * g_bh * 4);
    if (!g_img || !g_iw || !g_ih)
        return;
    void *bmp = NULL, *gfx = NULL;
    if (GdipCreateBitmapFromScan0(g_bw, g_bh, g_bw * 4, GP_PIXFMT_32PARGB, (BYTE *)g_bits, &bmp) != 0 || !bmp)
        return;
    if (GdipGetImageGraphicsContext(bmp, &gfx) == 0 && gfx)
    {
        float s = fmaxf(g_bw / (float)g_iw, g_bh / (float)g_ih); /* 画面いっぱい (cover) */
        int dw = (int)(g_iw * s + 0.5f), dh = (int)(g_ih * s + 0.5f);
        GdipGraphicsClear(gfx, 0xFF000000);
        GdipSetInterpolationMode(gfx, GP_INTERP_HQ_BILINEAR);
        GdipSetPixelOffsetMode(gfx, GP_PIXELOFFSET_HALF);
        GdipDrawImageRectI(gfx, g_img, (g_bw - dw) / 2, (g_bh - dh) / 2, dw, dh);
        GdipDeleteGraphics(gfx);
    }
    GdipDisposeImage(bmp);
}

/* ------------------------------------------------------------------ */
/* 動画 (mp4): Media Foundation の SourceReader で別スレッドがデコード        */
/*   RGB32 に変換されたフレームを共有バッファへ入れ、UI スレッドが画面へ引き伸ばして描く。*/
/*   音声ストリームは選ばない (= 音なし)。終わりまで来たら先頭へ戻ってループ。      */
/* ------------------------------------------------------------------ */
#define WM_WALLVIDEO (WM_APP + 41)

static CRITICAL_SECTION g_mfCs;
static uint32_t *g_mfBuf; /* 最新フレーム (幅 g_mfW x 高さ g_mfH, 隙間なし) */
static int g_mfW, g_mfH;
static BOOL g_mfTop; /* TRUE: 上から下 / FALSE: 下から上 */
static HANDLE g_mfThread, g_mfStop;
static volatile LONG g_mfPause, g_mfNew;
static wchar_t g_mfPath[MAX_PATH];
static const GUID kGuidNull = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

#define MF_VSTREAM ((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM)

/* 現在の出力タイプから 幅・高さ・ストライドを読む */
static BOOL MfReadType(IMFSourceReader *rd, int *w, int *h, int *stride)
{
    IMFMediaType *cur = NULL;
    UINT64 fs = 0;
    UINT32 st = 0;
    if (FAILED(IMFSourceReader_GetCurrentMediaType(rd, MF_VSTREAM, &cur)) || !cur)
        return FALSE;
    BOOL ok = SUCCEEDED(IMFMediaType_GetUINT64(cur, &MF_MT_FRAME_SIZE, &fs));
    *w = (int)(fs >> 32);
    *h = (int)(fs & 0xFFFFFFFFu);
    if (FAILED(IMFMediaType_GetUINT32(cur, &MF_MT_DEFAULT_STRIDE, &st)))
        *stride = -(*w) * 4; /* 取れないときは下から上 (RGB の標準) とみなす */
    else
        *stride = (int)st;
    IMFMediaType_Release(cur);
    return ok && *w > 0 && *h > 0;
}

static BOOL MfSetupReader(IMFSourceReader *rd)
{
    IMFMediaType *nt = NULL, *mt = NULL;
    UINT64 fs = 0;
    BOOL ok = FALSE;
    UINT64 want = 0;
    IMFSourceReader_SetStreamSelection(rd, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(IMFSourceReader_SetStreamSelection(rd, MF_VSTREAM, TRUE)))
        return FALSE;
    /* 4K などは 1920 幅に縮めて受け取る (CPU を節約) */
    if (SUCCEEDED(IMFSourceReader_GetNativeMediaType(rd, MF_VSTREAM, 0, &nt)) && nt)
    {
        if (SUCCEEDED(IMFMediaType_GetUINT64(nt, &MF_MT_FRAME_SIZE, &fs)))
        {
            UINT32 w = (UINT32)(fs >> 32), h = (UINT32)(fs & 0xFFFFFFFFu);
            if (w > 1920 && h > 0)
            {
                UINT32 nw = 1920, nh = (UINT32)((UINT64)h * 1920 / w) & ~1u;
                want = ((UINT64)nw << 32) | nh;
            }
        }
        IMFMediaType_Release(nt);
    }
    for (int pass = 0; pass < 2 && !ok; pass++)
    {
        if (FAILED(MFCreateMediaType(&mt)) || !mt)
            return FALSE;
        IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
        if (pass == 0 && want)
            IMFMediaType_SetUINT64(mt, &MF_MT_FRAME_SIZE, want);
        ok = SUCCEEDED(IMFSourceReader_SetCurrentMediaType(rd, MF_VSTREAM, NULL, mt));
        IMFMediaType_Release(mt);
        mt = NULL;
        if (pass == 0 && !want)
            break;
    }
    return ok;
}

static void MfDeliver(IMFSample *smp, int w, int h, int stride)
{
    IMFMediaBuffer *buf = NULL;
    BYTE *p = NULL;
    DWORD mx = 0, cur = 0;
    if (FAILED(IMFSample_ConvertToContiguousBuffer(smp, &buf)) || !buf)
        return;
    if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &p, &mx, &cur)) && p)
    {
        int pitch = stride < 0 ? -stride : stride;
        if (pitch >= w * 4 && (size_t)cur >= (size_t)pitch * (h - 1) + (size_t)w * 4)
        {
            EnterCriticalSection(&g_mfCs);
            if (!g_mfBuf || g_mfW != w || g_mfH != h)
            {
                free(g_mfBuf);
                g_mfBuf = (uint32_t *)malloc((size_t)w * h * 4);
                g_mfW = w;
                g_mfH = h;
            }
            if (g_mfBuf)
            {
                if (pitch == w * 4)
                    memcpy(g_mfBuf, p, (size_t)w * h * 4);
                else
                    for (int y = 0; y < h; y++)
                        memcpy((BYTE *)g_mfBuf + (size_t)y * w * 4, p + (size_t)y * pitch, (size_t)w * 4);
                g_mfTop = stride > 0;
            }
            LeaveCriticalSection(&g_mfCs);
            if (g_mfBuf && InterlockedExchange(&g_mfNew, 1) == 0 && g_host)
                PostMessageW(g_host, WM_WALLVIDEO, 0, 0);
        }
        IMFMediaBuffer_Unlock(buf);
    }
    IMFMediaBuffer_Release(buf);
}

static DWORD WINAPI VideoThread(LPVOID arg)
{
    (void)arg;
    BOOL co = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
        IMFAttributes *at = NULL;
        IMFSourceReader *rd = NULL;
        if (SUCCEEDED(MFCreateAttributes(&at, 2)) && at)
        {
            IMFAttributes_SetUINT32(at, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); /* YUV -> RGB32 / 拡縮 */
            IMFAttributes_SetUINT32(at, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);  /* ハードウェアデコーダーも使う */
            MFCreateSourceReaderFromURL(g_mfPath, at, &rd);
            IMFAttributes_Release(at);
        }
        int w = 0, h = 0, stride = 0;
        if (rd && MfSetupReader(rd) && MfReadType(rd, &w, &h, &stride))
        {
            ULONGLONG baseTick = 0;
            BOOL haveBase = FALSE;
            while (WaitForSingleObject(g_mfStop, 0) != WAIT_OBJECT_0)
            {
                if (g_mfPause)
                { /* フルスクリーンのアプリがあるあいだは止める */
                    WaitForSingleObject(g_mfStop, 200);
                    haveBase = FALSE;
                    continue;
                }
                DWORD si = 0, fl = 0;
                LONGLONG ts = 0;
                IMFSample *smp = NULL;
                if (FAILED(IMFSourceReader_ReadSample(rd, MF_VSTREAM, 0, &si, &fl, &ts, &smp)))
                    break;
                if (fl & MF_SOURCE_READERF_ERROR)
                {
                    if (smp)
                        IMFSample_Release(smp);
                    break;
                }
                if (fl & MF_SOURCE_READERF_ENDOFSTREAM)
                { /* 最後まで来た -> 先頭へ戻ってループ */
                    PROPVARIANT pv;
                    memset(&pv, 0, sizeof(pv));
                    pv.vt = VT_I8;
                    pv.hVal.QuadPart = 0;
                    if (smp)
                        IMFSample_Release(smp);
                    if (FAILED(IMFSourceReader_SetCurrentPosition(rd, &kGuidNull, &pv)))
                        break;
                    haveBase = FALSE;
                    continue;
                }
                if (fl & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                    MfReadType(rd, &w, &h, &stride);
                if (!smp)
                    continue;
                ULONGLONG now = GetTickCount64();
                LONGLONG ms = ts / 10000;
                if (!haveBase)
                {
                    baseTick = now - (ULONGLONG)ms;
                    haveBase = TRUE;
                }
                LONGLONG due = (LONGLONG)baseTick + ms;
                if (due > (LONGLONG)now)
                {
                    LONGLONG wait = due - (LONGLONG)now;
                    if (WaitForSingleObject(g_mfStop, (DWORD)(wait > 1000 ? 1000 : wait)) == WAIT_OBJECT_0)
                    {
                        IMFSample_Release(smp);
                        break;
                    }
                }
                else if ((LONGLONG)now - due > 250)
                    baseTick = now - (ULONGLONG)ms; /* 遅れすぎたら時間を合わせ直す */
                MfDeliver(smp, w, h, stride);
                IMFSample_Release(smp);
            }
        }
        if (rd)
            IMFSourceReader_Release(rd);
        MFShutdown();
    }
    if (co)
        CoUninitialize();
    return 0;
}

static void StartVideo(void)
{
    if (g_mfThread || !g_vfile[0] || !g_mfStop)
        return;
    wcsncpy(g_mfPath, g_vfile, MAX_PATH - 1);
    g_mfPath[MAX_PATH - 1] = 0;
    ResetEvent(g_mfStop);
    g_mfNew = 0;
    g_mfPause = 0;
    g_mfThread = CreateThread(NULL, 0, VideoThread, NULL, 0, NULL);
}

static void StopVideo(void)
{
    if (g_mfThread)
    {
        SetEvent(g_mfStop);
        WaitForSingleObject(g_mfThread, 3000);
        CloseHandle(g_mfThread);
        g_mfThread = NULL;
    }
    EnterCriticalSection(&g_mfCs);
    free(g_mfBuf);
    g_mfBuf = NULL;
    g_mfW = g_mfH = 0;
    LeaveCriticalSection(&g_mfCs);
    g_mfNew = 0;
}

/* 最新の動画フレームを dc へ (画面いっぱい / 拡大して切り抜き) */
static BOOL DrawVideoDC(HDC dc)
{
    BOOL ok = FALSE;
    EnterCriticalSection(&g_mfCs);
    if (g_mfBuf && g_mfW > 0 && g_mfH > 0 && g_vw > 0 && g_vh > 0)
    {
        int fw = g_mfW, fh = g_mfH, sx, sy, sw, sh;
        if ((long long)fw * g_vh > (long long)g_vw * fh)
        {
            sh = fh;
            sw = (int)((long long)fh * g_vw / g_vh);
            sx = (fw - sw) / 2;
            sy = 0;
        }
        else
        {
            sw = fw;
            sh = (int)((long long)fw * g_vh / g_vw);
            sx = 0;
            sy = (fh - sh) / 2;
        }
        if (sw < 1)
            sw = 1;
        if (sh < 1)
            sh = 1;
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = fw;
        bi.bmiHeader.biHeight = g_mfTop ? -fh : fh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, g_vw, g_vh, sx, sy, sw, sh, g_mfBuf, &bi, DIB_RGB_COLORS, SRCCOPY);
        ok = TRUE;
    }
    LeaveCriticalSection(&g_mfCs);
    return ok;
}

/* ------------------------------------------------------------------ */
/* 描画先ウィンドウ (デスクトップの背面に貼る)                              */
/* ------------------------------------------------------------------ */
static void DrawNow(void)
{
    if (!g_wnd || !g_bits)
        return;
    HDC dc = GetDC(g_wnd);
    if (!dc)
        return;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, g_vw, g_vh, 0, 0, g_bw, g_bh, g_bits, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_wnd, dc);
}

static LRESULT CALLBACK WallProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_kind == WK_VIDEO)
            DrawVideoDC(dc);
        else if (g_bits)
        {
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, 0, 0, g_vw, g_vh, 0, 0, g_bw, g_bh, g_bits, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumWorker(HWND h, LPARAM lp)
{
    if (FindWindowExW(h, NULL, L"SHELLDLL_DefView", NULL))
    {
        *(HWND *)lp = FindWindowExW(NULL, h, L"WorkerW", NULL); /* アイコンの裏側の WorkerW */
        return FALSE;
    }
    return TRUE;
}

static void Layout(void)
{
    g_vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_vw < 1)
        g_vw = 1;
    if (g_vh < 1)
        g_vh = 1;
    int cap = kBufCap[g_kind];
    int bw = g_vw < cap ? g_vw : cap;
    int bh = (int)((long long)g_vh * bw / g_vw);
    if (bh < 1)
        bh = 1;
    if (bw != g_bw || bh != g_bh || !g_bits)
    {
        free(g_bits);
        g_bits = (uint32_t *)calloc((size_t)bw * bh, 4);
        g_bw = bw;
        g_bh = bh;
    }
    memset(&g_bmi, 0, sizeof(g_bmi));
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    g_bmi.bmiHeader.biWidth = g_bw;
    g_bmi.bmiHeader.biHeight = -g_bh; /* 上から下 */
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
    g_dirty = TRUE;
}

static BOOL CreateWall(void)
{
    HWND prog = FindWindowW(L"Progman", NULL);
    if (!prog)
        return FALSE;
    DWORD_PTR res;
    SendMessageTimeoutW(prog, 0x052C, 0xD, 1, SMTO_NORMAL, 1000, &res); /* WorkerW を作らせる */
    SendMessageTimeoutW(prog, 0x052C, 0, 0, SMTO_NORMAL, 1000, &res);

    HWND parent = NULL, after = HWND_TOP;
    EnumWindows(EnumWorker, (LPARAM)&parent);
    if (!parent)
    { /* Windows 11 24H2 以降: WorkerW は Progman の子。アイコン(DefView)のすぐ下に入れる */
        parent = prog;
        HWND dv = FindWindowExW(prog, NULL, L"SHELLDLL_DefView", NULL);
        if (dv)
            after = dv;
    }
    g_wnd = CreateWindowExW(WS_EX_NOACTIVATE, L"PromDockWallpaper", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 100, 100, parent,
                            NULL, g_hInst, NULL);
    if (!g_wnd)
        return FALSE;
    POINT p = {g_vx, g_vy};
    ScreenToClient(parent, &p);
    SetWindowPos(g_wnd, after, p.x, p.y, g_vw, g_vh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_dirty = TRUE;
    return TRUE;
}

static void DestroyWall(void)
{
    if (g_wnd && IsWindow(g_wnd))
        DestroyWindow(g_wnd);
    g_wnd = NULL;
}

/* ------------------------------------------------------------------ */
/* 1フレーム                                                             */
/* ------------------------------------------------------------------ */
static void Tick(void)
{
    if (!g_wnd || !IsWindow(g_wnd) || !g_bits)
        return;
    ULONGLONG now = GetTickCount64();
    float dt = (now - g_last) / 1000.0f;
    g_last = now;
    if (dt > 0.25f)
        dt = 0.25f;
    g_t += dt * kSpeedMul[g_speed];

    if (now - g_lastChk > 1000)
    { /* フルスクリーンのアプリ (ゲーム・動画・プレゼン) があるときは止める */
        g_lastChk = now;
        QUERY_USER_NOTIFICATION_STATE s;
        g_pause = SUCCEEDED(SHQueryUserNotificationState(&s)) &&
                  (s == QUNS_BUSY || s == QUNS_RUNNING_D3D_FULL_SCREEN || s == QUNS_PRESENTATION_MODE);
    }
    InterlockedExchange(&g_mfPause, g_pause ? 1 : 0);
    if (g_pause)
        return;
    if (g_kind == WK_VIDEO)
        return; /* 動画は VideoThread が届けるフレームを WM_WALLVIDEO で描く */

    if (g_kind == WK_IMAGE)
    {
        if (g_frames > 1 && g_delays && now >= g_nextFrame)
        {
            g_frame = (g_frame + 1) % g_frames;
            GdipImageSelectActiveFrame(g_img, &kDimTime, g_frame);
            long d = g_delays[g_frame] * 10;
            g_nextFrame = now + (d < 20 ? 100 : (ULONGLONG)d);
            g_dirty = TRUE;
        }
        if (!g_dirty)
            return;
        RenderImage();
        g_dirty = FALSE;
    }
    else
    {
        switch (g_kind)
        {
        case WK_AURORA:
            RenderAurora(g_t);
            break;
        case WK_STARS:
            RenderStars(g_t);
            break;
        case WK_RAIN:
            RenderRain(g_t);
            break;
        case WK_WAVE:
            RenderWave(g_t);
            break;
        default:
            RenderBubbles(g_t);
            break;
        }
    }
    DrawNow();
}

static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_TIMER:
        if (w == TM_FRAME)
            Tick();
        else if (w == TM_WATCH && g_on)
        { /* エクスプローラーが再起動して壁紙ウィンドウが消えたら作り直す */
            if (!g_wnd || !IsWindow(g_wnd))
            {
                g_wnd = NULL;
                Layout();
                CreateWall();
            }
        }
        return 0;
    case WM_WALLVIDEO:
        InterlockedExchange(&g_mfNew, 0);
        if (g_on && g_kind == WK_VIDEO && g_wnd && !g_pause)
        {
            HDC dc = GetDC(g_wnd);
            if (dc)
            {
                DrawVideoDC(dc);
                ReleaseDC(g_wnd, dc);
            }
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (g_on)
        {
            Layout();
            DestroyWall();
            CreateWall();
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void Start(void)
{
    if (!g_host)
        g_host = CreateWindowExW(0, L"PromDockWallHost", L"PromDockWallHost", WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_hInst, NULL);
    if (g_kind == WK_IMAGE)
        LoadImageFile();
    Layout();
    if (!g_wnd || !IsWindow(g_wnd))
    {
        g_wnd = NULL;
        CreateWall();
    }
    if (g_kind == WK_VIDEO)
        StartVideo();
    g_last = GetTickCount64();
    g_lastChk = 0;
    g_pause = FALSE;
    SetTimer(g_host, TM_FRAME, (UINT)(1000 / kFpsVal[g_fps]), NULL);
    SetTimer(g_host, TM_WATCH, 2000, NULL);
}

static void Stop(void)
{
    if (g_host)
    {
        KillTimer(g_host, TM_FRAME);
        KillTimer(g_host, TM_WATCH);
    }
    StopVideo();
    DestroyWall();
    FreeImage();
    free(g_bits);
    g_bits = NULL;
    g_bw = g_bh = 0;
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, NULL, 0); /* 元の壁紙を描き直させる */
}

/* ------------------------------------------------------------------ */
/* 公開関数                                                              */
/* ------------------------------------------------------------------ */
/* 初回起動では Settings_Init の途中 (設定ウィンドウを開いている間) に呼ばれるので、
   どの公開関数からでも遅延初期化できるようにしてある */
static void EnsureInit(HINSTANCE hInst)
{
    if (g_inited)
        return;
    g_inited = TRUE;
    g_hInst = hInst ? hInst : GetModuleHandleW(NULL);
    InitializeCriticalSection(&g_mfCs);
    g_mfStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    LoadIni();
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WallProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"PromDockWallpaper";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = HostProc;
    wc.lpszClassName = L"PromDockWallHost";
    RegisterClassExW(&wc);
    if (g_on)
        Start();
}

void Wallpaper_Init(HINSTANCE hInst) { EnsureInit(hInst); }

void Wallpaper_Shutdown(void)
{
    if (!g_inited)
        return;
    if (g_on || g_wnd)
        Stop();
    if (g_host)
        DestroyWindow(g_host);
    g_host = NULL;
    if (g_gdipOn)
        GdiplusShutdown(g_gdipToken);
    g_gdipOn = FALSE;
    if (g_mfStop)
        CloseHandle(g_mfStop);
    g_mfStop = NULL;
    DeleteCriticalSection(&g_mfCs);
    g_inited = FALSE;
}

BOOL Wallpaper_Enabled(void)
{
    EnsureInit(NULL);
    return g_on;
}

void Wallpaper_SetEnabled(BOOL on)
{
    EnsureInit(NULL);
    on = on ? TRUE : FALSE;
    if (on == g_on)
        return;
    g_on = on;
    if (on)
        Start();
    else
        Stop();
    SaveIni();
}

int Wallpaper_GetKind(void)
{
    EnsureInit(NULL);
    return g_kind;
}
int Wallpaper_KindCount(void) { return WK_COUNT; }
const wchar_t *Wallpaper_KindName(int i) { return kKindName[Clampi(i, 0, WK_COUNT - 1)]; }
BOOL Wallpaper_KindIsFile(int i) { return i == WK_IMAGE; }
BOOL Wallpaper_KindIsVideo(int i) { return i == WK_VIDEO; }
BOOL Wallpaper_HasFile(void)
{
    EnsureInit(NULL);
    return g_file[0] != 0 && GetFileAttributesW(g_file) != INVALID_FILE_ATTRIBUTES;
}

void Wallpaper_SetKind(int k)
{
    EnsureInit(NULL);
    k = Clampi(k, 0, WK_COUNT - 1);
    if (k == g_kind)
        return;
    int old = g_kind;
    g_kind = k;
    if (g_on)
    {
        if (old == WK_VIDEO)
            StopVideo();
        if (k == WK_IMAGE)
            LoadImageFile();
        Layout(); /* 種類ごとに内部バッファの大きさが違う */
        if (k == WK_VIDEO)
            StartVideo();
    }
    SaveIni();
}

int Wallpaper_GetFps(void)
{
    EnsureInit(NULL);
    return g_fps;
}
void Wallpaper_SetFps(int i)
{
    EnsureInit(NULL);
    g_fps = Clampi(i, 0, 2);
    if (g_on && g_host)
        SetTimer(g_host, TM_FRAME, (UINT)(1000 / kFpsVal[g_fps]), NULL);
    SaveIni();
}

int Wallpaper_GetSpeed(void)
{
    EnsureInit(NULL);
    return g_speed;
}
void Wallpaper_SetSpeed(int i)
{
    EnsureInit(NULL);
    g_speed = Clampi(i, 0, 2);
    SaveIni();
}

void Wallpaper_FileLabel(wchar_t *out, int cch)
{
    EnsureInit(NULL);
    if (!g_file[0])
    {
        _snwprintf(out, cch, L"まだ選んでいません");
    }
    else
    {
        const wchar_t *n = wcsrchr(g_file, L'\\');
        _snwprintf(out, cch, L"選択中: %s", n ? n + 1 : g_file);
    }
    out[cch - 1] = 0;
}

/* 画像・GIF を選んで壁紙にする (種類も「画像・GIF」に切り替える) */
BOOL Wallpaper_PickFile(HWND owner)
{
    static const wchar_t filter[] = L"画像・GIF (*.png; *.jpg; *.gif; *.bmp)\0"
                                    L"*.png;*.jpg;*.jpeg;*.jpe;*.jfif;*.gif;*.bmp;*.dib;*.tif;*.tiff\0"
                                    L"すべてのファイル (*.*)\0*.*\0";
    wchar_t path[MAX_PATH];
    OPENFILENAMEW o;
    EnsureInit(NULL);
    memset(&o, 0, sizeof(o));
    path[0] = 0;
    o.lStructSize = sizeof(o);
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = path;
    o.nMaxFile = MAX_PATH;
    o.lpstrTitle = L"壁紙にする画像・GIF を選択";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&o))
        return FALSE;
    wcsncpy(g_file, path, MAX_PATH - 1);
    g_file[MAX_PATH - 1] = 0;
    if (g_on && g_kind == WK_VIDEO)
        StopVideo();
    g_kind = WK_IMAGE;
    if (g_on)
    {
        LoadImageFile();
        Layout();
    }
    SaveIni();
    return TRUE;
}

BOOL Wallpaper_HasVideo(void)
{
    EnsureInit(NULL);
    return g_vfile[0] != 0 && GetFileAttributesW(g_vfile) != INVALID_FILE_ATTRIBUTES;
}

void Wallpaper_VideoLabel(wchar_t *out, int cch)
{
    EnsureInit(NULL);
    if (!g_vfile[0])
        _snwprintf(out, cch, L"まだ選んでいません");
    else
    {
        const wchar_t *n = wcsrchr(g_vfile, L'\\');
        _snwprintf(out, cch, L"選択中: %s", n ? n + 1 : g_vfile);
    }
    out[cch - 1] = 0;
}

/* 動画 (mp4 など) を選んで壁紙にする (種類も「動画」に切り替える) */
BOOL Wallpaper_PickVideo(HWND owner)
{
    static const wchar_t filter[] = L"動画 (*.mp4; *.m4v; *.mov; *.wmv; *.avi)\0"
                                    L"*.mp4;*.m4v;*.mov;*.wmv;*.avi\0"
                                    L"すべてのファイル (*.*)\0*.*\0";
    wchar_t path[MAX_PATH];
    OPENFILENAMEW o;
    EnsureInit(NULL);
    memset(&o, 0, sizeof(o));
    path[0] = 0;
    o.lStructSize = sizeof(o);
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.lpstrFile = path;
    o.nMaxFile = MAX_PATH;
    o.lpstrTitle = L"壁紙にする動画を選択";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&o))
        return FALSE;
    wcsncpy(g_vfile, path, MAX_PATH - 1);
    g_vfile[MAX_PATH - 1] = 0;
    if (g_on)
        StopVideo();
    g_kind = WK_VIDEO;
    if (g_on)
    {
        Layout();
        StartVideo();
    }
    SaveIni();
    return TRUE;
}