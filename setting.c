/*
 * setting.c - PromDock のアプリ設定 (promdock.c / launcher.c / searchbar.c と同じ exe にビルド)
 *
 *   左のサイドバーで「全般 / ドック / ランチャー / 検索バー / 画像・GIF追加 / 時計」を切り替えるモダンな設定画面。
 *   部品 (トグル・セグメント・スライダー・ステッパー・ドロップダウン・ボタン) はすべて自前で描画します。
 *   配色は Windows のライト/ダークに自動で合わせます (全般 > テーマ で固定も可能)。
 *
 *   - 初回起動時: 最初に設定ウィンドウを開き、使う機能を選んで「開始」を押すと起動する
 *   - 2回目以降: タスクトレイのアイコン / ドック・ランチャー・検索バー・画像・時計の右クリックから開く
 *       (どの機能から開いたかで、その機能のページが最初に表示される)
 *   - 動作中に PromDock.exe をもう一度起動すると、動作中のアプリの設定ウィンドウが開く
 *   - 初回以降の変更は「すぐに反映・保存」されます (OK ボタンは不要)
 *
 *   項目ごとの設定
 *     全般      : ドック/ランチャーの有効・無効 / テーマ / Windows 起動時に自動実行 / フォルダを開く / アプリ終了
 *     ドック    : 表示位置 (下・上・左・右) / 位置のリセット / 自動的に隠す + 時間 /
 *                 アイコンサイズ / ホバー時の拡大率 / 背景の濃さ            (保存先: promdock.txt)
 *     ランチャー: ショートカットキー / 表示スタイル (通常・画面端に隠す) と端 / 列数・行数 (保存先: launcher.txt)
 *     検索バー  : ブラウザと検索エンジンを選んで追加・削除 / 位置の移動        (保存先: searchbar.ini)
 *     画像・GIF追加: 画像・GIF と大きさを選んで追加・削除 / 大きさの変更 / ドラッグ移動の許可 (保存先: photoframe.ini)
 *     時計      : デスクトップの時計 (デジタル/アナログ・デザイン・24時間・秒・日付・大きさ・背景の濃さ・位置)  (保存先: clock.ini)
 *     システムモニタ: CPU・メモリ・ディスク・ネットワークの使用状況 (デザイン・項目・更新間隔・大きさ・背景の濃さ・位置)  (保存先: sysmon.ini)
 *     壁紙      : 動く壁紙 (内蔵アニメ / 画像・GIF / 動画 mp4・フレームレート・速さ)  (保存先: wallpaper.ini / 実体は wallpaper.c)
 *
 *   - アプリ全体の設定は exe と同じフォルダの settings.ini に保存
 *       [PromDock]
 *       dock=1
 *       launcher=1
 *       launcher_style=0   (0:通常 / 1:画面の端に隠しておき、クリックでスライド表示)
 *       launcher_edge=0    (隠すとき: 0:左 1:右 2:上 3:下)
 *       theme=0            (0:自動 1:ライト 2:ダーク)
 *
 * 外部から呼ぶ関数:
 *     Settings_Init(hInst)        ... 起動直後に1回。FALSE が返ったらそのまま終了すること
 *     Settings_DockEnabled()      ... ドックが有効か
 *     Settings_LauncherEnabled()  ... ランチャーが有効か
 *     Settings_Open()             ... 設定ウィンドウを開く (最後に見ていたページ)
 *     Settings_OpenPage(page)     ... 指定ページで開く (0:全般 1:ドック 2:ランチャー 3:検索バー 4:画像・GIF追加 5:時計 6:天気 7:壁紙 8:システムモニタ)
 *     Settings_PreTranslate(msg)  ... メッセージループで TranslateMessage の前に呼ぶ
 *     Settings_RefreshSearchBar() ... 検索バーの一覧・ボタン表示を更新 (searchbar.c から呼ばれる)
 *     Settings_RefreshPhotoFrame() ... 画像・GIF追加の一覧・ボタン表示を更新 (photoframe.c から呼ばれる)
 *     Settings_RefreshClock()      ... 時計ページの表示を更新 (clock.c から呼ばれる)
 *     Settings_RefreshWeather()    ... 天気ページの表示を更新 (weather.c から呼ばれる)
 *     Settings_RefreshSysMon()     ... システムモニタページの表示を更新 (sysmon.c から呼ばれる)
 *     Settings_Shutdown()         ... 終了時に1回
 */
#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* ---- 他のモジュールの関数 ---- */
/* promdock.c */
void Dock_SetEnabled(BOOL on);
void Dock_Quit(void);
void Dock_SaveSettings(void);
int Dock_GetEdge(void);
void Dock_SetEdge(int e);
void Dock_ResetPosition(void);
BOOL Dock_GetAutoHide(void);
void Dock_SetAutoHide(BOOL on);
int Dock_GetHideMs(void);
void Dock_SetHideMs(int ms);
int Dock_GetIconSize(void);
void Dock_SetIconSize(int px);
int Dock_GetZoom(void);
void Dock_SetZoom(int pct);
int Dock_GetOpacity(void);
void Dock_SetOpacity(int pct);
int Dock_GetStyle(void);
void Dock_SetStyle(int s);
int Dock_StyleCount(void);
const wchar_t *Dock_StyleName(int i);
int Dock_Count(void);
int Dock_GetEdit(void);
void Dock_SetEdit(int i);
BOOL Dock_Add(void);
BOOL Dock_RemoveAt(int i);
int Dock_GetZOrder(void);
void Dock_SetZOrder(int z);
int Dock_GetType(void);
void Dock_SetType(int t);
int Dock_TypeCount(void);
const wchar_t *Dock_TypeName(int i);
BOOL Notice_Enabled(void); /* 通知バッジ (notice.c) */
void Notice_SetEnabled(BOOL on);
/* launcher.c */
void Launcher_SetEnabled(BOOL on);
void Launcher_SetStyle(int style, int edge);
void Launcher_Toggle(void);
int Launcher_GetHotkey(void);
BOOL Launcher_SetHotkey(int vk);
int Launcher_GetCols(void);
int Launcher_GetRows(void);
void Launcher_SetGrid(int cols, int rows);
/* searchbar.c (デスクトップ検索バー) */
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
/* photoframe.c (デスクトップに置ける画像・GIF) */
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
/* clock.c (デスクトップ時計) */
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
/* sysmon.c */
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

/* weather.c (デスクトップ天気) */
BOOL Weather_Enabled(void);
void Weather_SetEnabled(BOOL on);
int Weather_GetVariant(void);
void Weather_SetVariant(int v);
int Weather_VariantCount(void);
const wchar_t *Weather_VariantName(int i);
int Weather_CityCount(void);
const wchar_t *Weather_CityName(int i);
int Weather_GetCity(void);
void Weather_SetCity(int i);
void Weather_LocLabel(wchar_t *out, int cch);
void Weather_GetLatLon(double *lat, double *lon);
BOOL Weather_PickCoords(HWND owner);
int Weather_GetUnit(void);
void Weather_SetUnit(int unit);
int Weather_GetSize(void);
void Weather_SetSize(int pct);
int Weather_GetOpacity(void);
void Weather_SetOpacity(int pct);
BOOL Weather_Movable(void);
void Weather_SetMovable(BOOL on);
void Weather_ResetPosition(void);
void Weather_Refresh(void);
void Weather_Status(wchar_t *out, int cch);

/* wallpaper.c (動く壁紙) */
void Wallpaper_Init(HINSTANCE hInst);
void Wallpaper_Shutdown(void);
BOOL Wallpaper_Enabled(void);
void Wallpaper_SetEnabled(BOOL on);
int Wallpaper_GetKind(void);
void Wallpaper_SetKind(int k);
int Wallpaper_KindCount(void);
const wchar_t *Wallpaper_KindName(int i);
BOOL Wallpaper_KindIsFile(int i);
BOOL Wallpaper_KindIsVideo(int i);
BOOL Wallpaper_HasVideo(void);
void Wallpaper_VideoLabel(wchar_t *out, int cch);
BOOL Wallpaper_PickVideo(HWND owner);
BOOL Wallpaper_HasFile(void);
int Wallpaper_GetFps(void);
void Wallpaper_SetFps(int i);
int Wallpaper_GetSpeed(void);
void Wallpaper_SetSpeed(int i);
void Wallpaper_FileLabel(wchar_t *out, int cch);
BOOL Wallpaper_PickFile(HWND owner);

/* このファイルが公開する関数 */
BOOL Settings_Init(HINSTANCE hInst);
void Settings_Open(void);
void Settings_OpenPage(int page);
BOOL Settings_DockEnabled(void);
BOOL Settings_LauncherEnabled(void);
int Settings_LauncherStyle(void);
int Settings_LauncherEdge(void);
BOOL Settings_PreTranslate(MSG *m);
void Settings_Shutdown(void);
void Settings_RefreshSearchBar(void);
void Settings_RefreshPhotoFrame(void);
void Settings_RefreshClock(void);
void Settings_RefreshWeather(void);
void Settings_RefreshSysMon(void);

#define WM_TRAY (WM_APP + 10)
#define WM_SHOWSETTINGS (WM_APP + 11)

#define TIMER_ANIM 1
#define TIMER_NOTICE 2
#define SB_LIMIT 8 /* searchbar.c の SB_MAX と同じ */

/* ---- レイアウト (96DPI 基準の論理ピクセル) ---- */
#define WIN_W 880
#define WIN_H 640
#define SIDE_W 224
#define HEAD_H 104
#define FOOT_H 68
#define CX (SIDE_W + 36)
#define CW (WIN_W - SIDE_W - 72)
#define RH 64

/* ---- 部品の ID (アニメーション配列の添字にもなるので 64 未満) ---- */
enum
{
    W_NONE = 0,
    W_NAV,
    W_GOTO,
    W_TG_DOCK,
    W_TG_LN,
    W_TG_AUTOSTART,
    W_TG_AUTOHIDE,
    W_TG_NOTICE, /* ドック: 通知バッジ */
    W_SEG_THEME,
    W_SEG_EDGE,
    W_SEG_LNSTYLE,
    W_SEG_LNEDGE,
    W_SEG_DKSTYLE, /* ドック: デザイン */
    W_SEG_DKTYPE,  /* ドック: 形 (バー / サークル) */
    W_SEG_DKZ,     /* ドック: 重なり順 (最前列 / 最奥) */
    W_SEG_DKSEL,   /* ドック: 編集するドックの選択 (ドック1〜3) */
    W_SL_HIDEMS,
    W_SL_ICON,
    W_SL_ZOOM,
    W_SL_OPACITY,
    W_SL_PFSIZE, /* 画像・GIF追加: 追加するときの大きさ (W_SL_OPACITY の直後に置くこと: スライダーの範囲判定に使う) */
    W_SL_CLSIZE, /* 時計: 大きさ */
    W_SL_CLOPA,  /* 時計: 背景の濃さ */
    W_SL_WSIZE,  /* 天気: 大きさ */
    W_SL_WOPA,   /* 天気: 背景の濃さ */
    W_SL_SMSIZE, /* システムモニタ: 大きさ */
    W_SL_SMOPA,  /* システムモニタ: 背景の濃さ (スライダーはここまで) */
    W_STP_COLS,
    W_STP_ROWS,
    W_DD_HOTKEY,
    W_DD_BROWSER,
    W_DD_ENGINE,
    W_BTN_RESETPOS,
    W_BTN_DKADD, /* ドックを追加 */
    W_BTN_DKDEL, /* 選択中のドックを削除 */
    W_BTN_OPENDIR,
    W_BTN_QUIT,
    W_BTN_ADD,
    W_BTN_MOVE,
    W_BTN_DEL,
    W_TG_PFMOVE,
    W_STP_PFSIZE,
    W_BTN_PFADD,
    W_BTN_PFDEL,
    W_SEG_CLSTYLE,
    W_SEG_CLVAR, /* 時計: デザイン (スタイルごと) */
    W_TG_CLOCK,
    W_TG_CL24,
    W_TG_CLSEC,
    W_TG_CLDATE,
    W_TG_CLMOVE,
    W_BTN_CLRESET,
    W_TG_WEATHER,    /* 天気: 表示 */
    W_SEG_WVAR,      /* 天気: デザイン */
    W_SEG_WUNIT,     /* 天気: 単位 (℃ / ℉) */
    W_DD_WCITY,      /* 天気: 場所 */
    W_DD_WVAR,       /* 天気: デザイン */
    W_BTN_WCOORD,    /* 天気: 緯度・経度を入力 */
    W_BTN_WREFRESH,  /* 天気: 今すぐ更新 */
    W_TG_WMOVE,      /* 天気: ドラッグ移動 */
    W_BTN_WRESET,    /* 天気: 位置のリセット */
    W_TG_WALL,       /* 壁紙: 表示 */
    W_DD_WALLKIND,   /* 壁紙: 種類 */
    W_BTN_WALLFILE,  /* 壁紙: 画像・GIF を選ぶ */
    W_BTN_WALLVIDEO, /* 壁紙: 動画を選ぶ */
    W_SEG_WALLFPS,   /* 壁紙: フレームレート */
    W_SEG_WALLSPD,   /* 壁紙: 速さ */
    W_TG_SMON,       /* システムモニタ: 表示 */
    W_DD_SMVAR,      /* システムモニタ: デザイン (ドロップダウン) */
    W_SEG_SMINT,     /* システムモニタ: 更新間隔 */
    W_TG_SMCPU,      /* システムモニタ: CPU */
    W_TG_SMMEM,      /* システムモニタ: メモリ */
    W_TG_SMDISK,     /* システムモニタ: ディスク */
    W_TG_SMNET,      /* システムモニタ: ネットワーク */
    W_TG_SMMOVE,     /* システムモニタ: ドラッグ移動 */
    W_BTN_SMRESET,   /* システムモニタ: 位置のリセット */
    W_BTN_OK,
    W_BTN_EXIT,
    W_COUNT
};

enum
{
    PAGE_GENERAL = 0,
    PAGE_DOCK,
    PAGE_LAUNCHER,
    PAGE_SEARCH,
    PAGE_PHOTO,
    PAGE_CLOCK,
    PAGE_WEATHER,
    PAGE_WALLPAPER,
    PAGE_SYSMON,
    PAGE_COUNT
};

typedef struct
{
    uint32_t bg, side, card, border, text, sub, accent, field, fieldHover, track, sep, danger, knob;
} Pal;

static const Pal PAL_LIGHT = {0xF3F4F7, 0xE9ECF1, 0xFFFFFF, 0xDFE3EA, 0x1B1D22, 0x6A707C, 0x3B6CF0,
                              0xF0F2F6, 0xE4E8EF, 0xCDD3DE, 0xECEEF3, 0xD13438, 0xFFFFFF};
static const Pal PAL_DARK = {0x1C1D21, 0x16171A, 0x27282D, 0x373940, 0xF2F3F5, 0x9CA1AD, 0x5B8CFF,
                             0x33353C, 0x3E414A, 0x4A4E59, 0x31333A, 0xFF7B7B, 0xFFFFFF};

typedef struct
{
    RECT r;       /* 画面上の矩形 (デバイスpx) */
    int id, arg;  /* 部品 ID と引数 (セグメント番号・ボタンの対象など) */
    BOOL en;      /* 有効か */
    BOOL content; /* スクロール領域内の部品か */
    int x0, x1;   /* スライダーのトラック左右 (デバイスpx) */
} Hit;

/* ---- 状態 ---- */
static HINSTANCE g_hInst;
static HWND g_host, g_set;
static HFONT g_fBody, g_fSmall, g_fSec, g_fHead, g_fApp, g_fBtn;
static HICON g_icon;
static HICON g_iconSm; /* 小さいアイコン (タイトルバー・トレイ用)。g_icon は大きいアイコン (タスクバー等) */
static HANDLE g_mutex;
static UINT g_msgTaskbar;
static BOOL g_dockOn = TRUE, g_lnOn = TRUE, g_first, g_accepted, g_trayAdded;
static int g_lnStyle = 0, g_lnEdge = 0; /* ランチャー表示方法 0:通常 1:画面端に隠す / 端 0:左 1:右 2:上 3:下 */
static int g_theme = 0;                 /* 0:自動 1:ライト 2:ダーク */
static BOOL g_autoStart;
static float g_dpi = 1.0f;
static wchar_t g_ini[MAX_PATH];

static const Pal *g_pal = &PAL_LIGHT;
static BOOL g_dark;

/* 描画バッファ */
static HDC g_mem;
static HBITMAP g_dib, g_oldBmp;
static uint32_t *g_px;
static int g_pw, g_ph;
static RECT g_clip;
static int g_oy; /* 描画時に y へ加える論理オフセット (スクロール) */

/* UI の状態 */
static int g_page = PAGE_GENERAL;
static int g_scroll[PAGE_COUNT];
static int g_contentH;
static Hit g_hit[160];
static int g_nhit;
static int g_hoverId, g_hoverArg, g_pressId, g_pressArg, g_focusId, g_focusArg;
static BOOL g_kbd, g_tracking, g_dragSlider;
static int g_dragId;
static int g_sbBrowser, g_sbEngine; /* 検索バー追加フォームの選択 */
static int g_pfNewSize = 400;       /* 画像・GIF を追加するときの大きさ (長い辺 px) */
static wchar_t g_notice[160];
static DWORD g_noticeTick;
static BOOL g_noticeWarn;

/* アニメーション (トグルのノブ) */
static float g_av[W_COUNT + 8], g_at[W_COUNT + 8];
static BOOL g_ainit[W_COUNT + 8];

static int S(int v) { return (int)(v * g_dpi + 0.5f); }
static float F(int v) { return v * g_dpi; }
static float FY(int y) { return (y + g_oy) * g_dpi; }
static int DY(int y) { return (int)((y + g_oy) * g_dpi + 0.5f); }
static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ------------------------------------------------------------------ */
/* アプリのアイコン (3x3 のグリッド) をコードで生成                          */
/* ------------------------------------------------------------------ */
static float RRectCov(float x, float y, float L, float T, float R, float B, float rad)
{
    float cx = (L + R) * 0.5f, cy = (T + B) * 0.5f, hw = (R - L) * 0.5f, hh = (B - T) * 0.5f;
    if (rad > hw)
        rad = hw;
    if (rad > hh)
        rad = hh;
    float qx = fabsf(x - cx) - hw + rad, qy = fabsf(y - cy) - hh + rad;
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;
    float c = 0.5f - d;
    return c < 0 ? 0 : (c > 1 ? 1 : c);
}

/* exe に埋め込んだ promdock.ico (リソースID 1) を読み込む。埋め込まれていなければ NULL */
static HICON LoadAppIco(int px)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, px, px, LR_DEFAULTCOLOR);
}

static HICON MakeAppIcon(void)
{
    const int N = 32;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = N;
    bi.bmiHeader.biHeight = -N;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    HBITMAP mask = CreateBitmap(N, N, 1, 1, NULL);
    if (!color || !mask || !bits)
    {
        if (color)
            DeleteObject(color);
        if (mask)
            DeleteObject(mask);
        return NULL;
    }
    uint32_t *px = (uint32_t *)bits;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
        {
            float fx = x + 0.5f, fy = y + 0.5f;
            float a = RRectCov(fx, fy, 1, 1, 31, 31, 8);
            float t = fy / N;
            float r = 70 - 30 * t, g = 120 - 50 * t, b = 235 - 45 * t; /* 上から下へ濃くなる青 */
            float w = 0;                                               /* 白い 3x3 の角丸四角 */
            for (int cy = 0; cy < 3; cy++)
                for (int cx = 0; cx < 3; cx++)
                {
                    float L = 5.0f + cx * 8.5f, T = 5.0f + cy * 8.5f;
                    float c = RRectCov(fx, fy, L, T, L + 6, T + 6, 1.8f);
                    if (c > w)
                        w = c;
                }
            r = r + (255 - r) * w;
            g = g + (255 - g) * w;
            b = b + (255 - b) * w;
            px[y * N + x] = ((uint32_t)(a * 255) << 24) | ((uint32_t)(r * a) << 16) | ((uint32_t)(g * a) << 8) |
                            (uint32_t)(b * a);
        }
    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return h;
}

/* ------------------------------------------------------------------ */
/* 設定の読み書き・反映                                                   */
/* ------------------------------------------------------------------ */
static void LoadSettings(void)
{
    g_dockOn = GetPrivateProfileIntW(L"PromDock", L"dock", 1, g_ini) != 0;
    g_lnOn = GetPrivateProfileIntW(L"PromDock", L"launcher", 1, g_ini) != 0;
    g_lnStyle = GetPrivateProfileIntW(L"PromDock", L"launcher_style", 0, g_ini) ? 1 : 0;
    g_lnEdge = ClampInt((int)GetPrivateProfileIntW(L"PromDock", L"launcher_edge", 0, g_ini), 0, 3);
    g_theme = ClampInt((int)GetPrivateProfileIntW(L"PromDock", L"theme", 0, g_ini), 0, 2);
}

static void SaveSettings(void)
{
    wchar_t b[8];
    WritePrivateProfileStringW(L"PromDock", L"dock", g_dockOn ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"PromDock", L"launcher", g_lnOn ? L"1" : L"0", g_ini);
    WritePrivateProfileStringW(L"PromDock", L"launcher_style", g_lnStyle ? L"1" : L"0", g_ini);
    _snwprintf(b, 8, L"%d", g_lnEdge);
    b[7] = 0;
    WritePrivateProfileStringW(L"PromDock", L"launcher_edge", b, g_ini);
    _snwprintf(b, 8, L"%d", g_theme);
    b[7] = 0;
    WritePrivateProfileStringW(L"PromDock", L"theme", b, g_ini);
}

/* 初回設定の「開始」: すべてを確定して反映する */
static void ApplyAll(void)
{
    SaveSettings();
    Dock_SetEnabled(g_dockOn); /* 起動前(初回設定中)は何もしない */
    Launcher_SetStyle(g_lnStyle, g_lnEdge);
    Launcher_SetEnabled(g_lnOn);
    Dock_SaveSettings();
}

BOOL Settings_DockEnabled(void) { return g_dockOn; }
BOOL Settings_LauncherEnabled(void) { return g_lnOn; }
int Settings_LauncherStyle(void) { return g_lnStyle; }
int Settings_LauncherEdge(void) { return g_lnEdge; }

/* ---- Windows 起動時の自動実行 (HKCU\...\Run) ---- */
#define RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
static BOOL AutoStartGet(void)
{
    HKEY k;
    BOOL on = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD type = 0, n = 0;
        on = RegQueryValueExW(k, L"PromDock", NULL, &type, NULL, &n) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}

static void AutoStartSet(BOOL on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (on)
    {
        wchar_t path[MAX_PATH], v[MAX_PATH + 4];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        _snwprintf(v, MAX_PATH + 4, L"\"%s\"", path);
        v[MAX_PATH + 3] = 0;
        RegSetValueExW(k, L"PromDock", 0, REG_SZ, (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
    }
    else
        RegDeleteValueW(k, L"PromDock");
    RegCloseKey(k);
}

/* ---- テーマ ---- */
static BOOL SystemIsDark(void)
{
    HKEY k;
    DWORD v = 1, n = sizeof(v), type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_READ, &k) == ERROR_SUCCESS)
    {
        RegQueryValueExW(k, L"AppsUseLightTheme", NULL, &type, (BYTE *)&v, &n);
        RegCloseKey(k);
    }
    return v == 0;
}

static void ApplyTheme(void)
{
    g_dark = g_theme == 2 ? TRUE : (g_theme == 1 ? FALSE : SystemIsDark());
    g_pal = g_dark ? &PAL_DARK : &PAL_LIGHT;
    if (g_set)
    {
        BOOL v = g_dark;
        DwmSetWindowAttribute(g_set, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
        InvalidateRect(g_set, NULL, FALSE);
    }
}

static void Notice(const wchar_t *s, BOOL warn)
{
    wcsncpy(g_notice, s, 159);
    g_notice[159] = 0;
    g_noticeTick = GetTickCount();
    g_noticeWarn = warn;
    if (g_set)
    {
        SetTimer(g_set, TIMER_NOTICE, 500, NULL);
        InvalidateRect(g_set, NULL, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* 描画プリミティブ (アンチエイリアス付きの角丸四角・円・線)                  */
/* ------------------------------------------------------------------ */
static uint32_t Mix(uint32_t a, uint32_t b, float t)
{
    t = Clampf(t, 0, 1);
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return ((uint32_t)(ar + (br - ar) * t + 0.5f) << 16) | ((uint32_t)(ag + (bg - ag) * t + 0.5f) << 8) |
           (uint32_t)(ab + (bb - ab) * t + 0.5f);
}

static void PutPx(int x, int y, uint32_t c, float a)
{
    if (x < g_clip.left || x >= g_clip.right || y < g_clip.top || y >= g_clip.bottom || a <= 0.002f)
        return;
    uint32_t *p = &g_px[y * g_pw + x];
    if (a >= 0.998f)
    {
        *p = c;
        return;
    }
    *p = Mix(*p, c, a);
}

/* デバイス座標 (float) の角丸四角 */
static void FillRRd(float L, float T, float R, float B, float rad, uint32_t c, float alpha)
{
    int x0 = (int)floorf(L) - 1, x1 = (int)ceilf(R) + 1, y0 = (int)floorf(T) - 1, y1 = (int)ceilf(B) + 1;
    if (x0 < g_clip.left)
        x0 = g_clip.left;
    if (y0 < g_clip.top)
        y0 = g_clip.top;
    if (x1 > g_clip.right)
        x1 = g_clip.right;
    if (y1 > g_clip.bottom)
        y1 = g_clip.bottom;
    float rr = fminf(rad, fminf((R - L) * 0.5f, (B - T) * 0.5f));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f, py = y + 0.5f;
            /* 内側は計算を省く (十字形の領域は完全に塗られている) */
            if ((px >= L + rr && px <= R - rr && py >= T + 1 && py <= B - 1) ||
                (py >= T + rr && py <= B - rr && px >= L + 1 && px <= R - 1))
                PutPx(x, y, c, alpha);
            else
                PutPx(x, y, c, RRectCov(px, py, L, T, R, B, rad) * alpha);
        }
}

/* 内側に太さ t の枠線 */
static void StrokeRRd(float L, float T, float R, float B, float rad, float t, uint32_t c, float alpha)
{
    int x0 = (int)floorf(L) - 1, x1 = (int)ceilf(R) + 1, y0 = (int)floorf(T) - 1, y1 = (int)ceilf(B) + 1;
    if (x0 < g_clip.left)
        x0 = g_clip.left;
    if (y0 < g_clip.top)
        y0 = g_clip.top;
    if (x1 > g_clip.right)
        x1 = g_clip.right;
    if (y1 > g_clip.bottom)
        y1 = g_clip.bottom;
    float ir = rad - t > 0 ? rad - t : 0;
    float rr = fminf(rad, fminf((R - L) * 0.5f, (B - T) * 0.5f));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float qx = x + 0.5f, qy = y + 0.5f;
            if ((qx >= L + rr && qx <= R - rr && qy >= T + t + 1 && qy <= B - t - 1) ||
                (qy >= T + rr && qy <= B - rr && qx >= L + t + 1 && qx <= R - t - 1))
                continue; /* 枠線から十分内側 */
            float o = RRectCov(x + 0.5f, y + 0.5f, L, T, R, B, rad);
            if (o <= 0)
                continue;
            float i = RRectCov(x + 0.5f, y + 0.5f, L + t, T + t, R - t, B - t, ir);
            PutPx(x, y, c, (o - i) * alpha);
        }
}

/* 太さ th の線 (両端は丸) */
static void LineD(float ax, float ay, float bx, float by, float th, uint32_t c, float alpha)
{
    float r = th * 0.5f;
    int x0 = (int)floorf(fminf(ax, bx) - r) - 1, x1 = (int)ceilf(fmaxf(ax, bx) + r) + 1;
    int y0 = (int)floorf(fminf(ay, by) - r) - 1, y1 = (int)ceilf(fmaxf(ay, by) + r) + 1;
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = l2 > 0 ? Clampf((px * dx + py * dy) / l2, 0, 1) : 0;
            float ex = px - dx * t, ey = py - dy * t;
            float d = sqrtf(ex * ex + ey * ey) - r;
            PutPx(x, y, c, Clampf(0.5f - d, 0, 1) * alpha);
        }
}

/* 論理座標のラッパー */
static void RR(int x, int y, int w, int h, int rad, uint32_t c, float a)
{
    FillRRd(F(x), FY(y), F(x + w), FY(y + h), F(rad), c, a);
}
static void RRs(int x, int y, int w, int h, int rad, float t, uint32_t c, float a)
{
    StrokeRRd(F(x), FY(y), F(x + w), FY(y + h), F(rad), t * g_dpi, c, a);
}
static void Dot(float cx, float cy, float r, uint32_t c, float a)
{
    FillRRd((cx - r) * g_dpi, FY(0) + (cy - r) * g_dpi, (cx + r) * g_dpi, FY(0) + (cy + r) * g_dpi, r * g_dpi, c, a);
}
static void Ln(float ax, float ay, float bx, float by, float th, uint32_t c, float a)
{
    LineD(ax * g_dpi, FY(0) + ay * g_dpi, bx * g_dpi, FY(0) + by * g_dpi, th * g_dpi, c, a);
}

static void Txt(const wchar_t *s, int x, int y, int w, int h, HFONT f, uint32_t col, UINT fmt)
{
    RECT r = {S(x), DY(y), S(x + w), DY(y + h)};
    RECT vis;
    if (!IntersectRect(&vis, &r, &g_clip))
        return;
    HRGN rg = CreateRectRgn(g_clip.left, g_clip.top, g_clip.right, g_clip.bottom);
    SelectClipRgn(g_mem, rg);
    SelectObject(g_mem, f);
    SetBkMode(g_mem, TRANSPARENT);
    SetTextColor(g_mem, RGB((col >> 16) & 255, (col >> 8) & 255, col & 255));
    DrawTextW(g_mem, s, -1, &r, fmt);
    SelectClipRgn(g_mem, NULL);
    DeleteObject(rg);
}
#define FL_L (DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX)
#define FL_C (DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX)
#define FL_R (DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX)
#define FL_W (DT_LEFT | DT_WORDBREAK | DT_NOPREFIX)

/* ------------------------------------------------------------------ */
/* ヒットテスト / フォーカス / ホバー                                       */
/* ------------------------------------------------------------------ */
static BOOL IsHover(int id, int arg) { return g_hoverId == id && g_hoverArg == arg; }
static BOOL IsFocus(int id, int arg) { return g_kbd && g_focusId == id && g_focusArg == arg; }

static void AddHit(int x, int y, int w, int h, int id, int arg, BOOL en, BOOL content, int rad)
{
    if (g_nhit >= (int)(sizeof(g_hit) / sizeof(g_hit[0])))
        return;
    Hit *t = &g_hit[g_nhit++];
    t->r.left = S(x);
    t->r.top = DY(y);
    t->r.right = S(x + w);
    t->r.bottom = DY(y + h);
    t->id = id;
    t->arg = arg;
    t->en = en;
    t->content = content;
    t->x0 = t->x1 = 0;
    if (IsFocus(id, arg) && en)
        RRs(x - 3, y - 3, w + 6, h + 6, rad + 3, 2.0f, g_pal->accent, 0.9f);
}

static Hit *HitAt(int px, int py)
{
    int vTop = S(HEAD_H), vBot = S(WIN_H - FOOT_H);
    for (int i = g_nhit - 1; i >= 0; i--)
    {
        Hit *t = &g_hit[i];
        if (px < t->r.left || px >= t->r.right || py < t->r.top || py >= t->r.bottom)
            continue;
        if (t->content && (py < vTop || py >= vBot || px < S(SIDE_W)))
            continue;
        return t;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* 部品                                                                  */
/* ------------------------------------------------------------------ */
static BOOL g_inContent; /* 描画中の部品がスクロール領域内か */

static float AnimVal(int id, float target)
{
    if (!g_ainit[id])
    {
        g_av[id] = target;
        g_ainit[id] = TRUE;
    }
    g_at[id] = target;
    return g_av[id];
}

static void Toggle(int id, int x, int y, BOOL on, BOOL en)
{
    float t = AnimVal(id, on ? 1.0f : 0.0f);
    float a = en ? 1.0f : 0.45f;
    uint32_t col = Mix(g_pal->track, g_pal->accent, t);
    RR(x, y, 44, 24, 12, col, a);
    if (t < 0.99f)
        RRs(x, y, 44, 24, 12, 1.0f, Mix(g_pal->sub, g_pal->accent, t), 0.5f * a * (1 - t));
    float kx = 12.0f + 20.0f * t;
    float kr = 7.0f + 1.0f * t;
    Dot(x + kx, y + 12.0f, kr, g_pal->knob, a);
    AddHit(x, y, 44, 24, id, 0, en, g_inContent, 12);
}

static void Segmented(int id, int x, int y, int w, const wchar_t *const *labels, int n, int sel, BOOL en)
{
    float a = en ? 1.0f : 0.45f;
    RR(x, y, w, 34, 9, g_pal->field, a);
    int sw = (w - 6) / n;
    for (int i = 0; i < n; i++)
    {
        int sx = x + 3 + i * sw;
        BOOL s = i == sel;
        if (s)
            RR(sx, y + 3, sw, 28, 7, g_pal->accent, a);
        else if (IsHover(id, i) && en)
            RR(sx, y + 3, sw, 28, 7, g_pal->fieldHover, 1.0f);
        Txt(labels[i], sx, y + 3, sw, 28, g_fBtn, s ? 0xFFFFFF : (en ? g_pal->text : g_pal->sub), FL_C);
        AddHit(sx, y + 3, sw, 28, id, i, en, g_inContent, 7);
    }
}

static void Button(int id, int arg, int x, int y, int w, int h, const wchar_t *label, int style, BOOL en)
{
    /* style 0:通常 1:主要(アクセント) 2:注意(赤文字) */
    float a = en ? 1.0f : 0.45f;
    BOOL hv = IsHover(id, arg) && en;
    uint32_t fill, tx;
    if (style == 1)
    {
        fill = hv ? Mix(g_pal->accent, 0x000000, 0.14f) : g_pal->accent;
        tx = 0xFFFFFF;
    }
    else
    {
        fill = hv ? g_pal->fieldHover : g_pal->field;
        tx = style == 2 ? g_pal->danger : g_pal->text;
    }
    RR(x, y, w, h, 9, fill, a);
    if (style != 1)
        RRs(x, y, w, h, 9, 1.0f, g_pal->border, 0.8f * a);
    Txt(label, x, y, w, h, g_fBtn, tx, FL_C);
    AddHit(x, y, w, h, id, arg, en, g_inContent, 9);
}

static void Dropdown(int id, int x, int y, int w, const wchar_t *text, BOOL en)
{
    float a = en ? 1.0f : 0.45f;
    BOOL hv = IsHover(id, 0) && en;
    RR(x, y, w, 34, 9, hv ? g_pal->fieldHover : g_pal->field, a);
    RRs(x, y, w, 34, 9, 1.0f, g_pal->border, 0.8f * a);
    Txt(text, x + 14, y, w - 44, 34, g_fBtn, en ? g_pal->text : g_pal->sub, FL_L);
    float cx = (float)(x + w - 20), cy = (float)(y + 17);
    Ln(cx - 4.5f, cy - 2.0f, cx, cy + 2.5f, 1.8f, g_pal->sub, a);
    Ln(cx, cy + 2.5f, cx + 4.5f, cy - 2.0f, 1.8f, g_pal->sub, a);
    AddHit(x, y, w, 34, id, 0, en, g_inContent, 9);
}

static void StepperA(int id, int argBase, int x, int y, const wchar_t *text, BOOL en, BOOL canMinus, BOOL canPlus)
{
    float a = en ? 1.0f : 0.45f;
    for (int i = 0; i < 2; i++)
    {
        int bx = i == 0 ? x : x + 36 + 76;
        BOOL ok = en && (i == 0 ? canMinus : canPlus);
        BOOL hv = IsHover(id, argBase + i) && ok;
        RR(bx, y, 36, 34, 9, hv ? g_pal->fieldHover : g_pal->field, en ? (ok ? 1.0f : 0.55f) : 0.45f);
        RRs(bx, y, 36, 34, 9, 1.0f, g_pal->border, 0.8f * a);
        float cx = bx + 18.0f, cy = y + 17.0f;
        float ia = ok ? 1.0f : 0.4f;
        Ln(cx - 5, cy, cx + 5, cy, 1.8f, g_pal->text, ia);
        if (i == 1)
            Ln(cx, cy - 5, cx, cy + 5, 1.8f, g_pal->text, ia);
        AddHit(bx, y, 36, 34, id, argBase + i, ok, g_inContent, 9);
    }
    Txt(text, x + 36, y, 76, 34, g_fBtn, en ? g_pal->text : g_pal->sub, FL_C);
}

static void Stepper(int id, int x, int y, const wchar_t *text, BOOL en, BOOL canMinus, BOOL canPlus)
{
    StepperA(id, 0, x, y, text, en, canMinus, canPlus);
}

/* スライダー: 値の表示 + 左のトラック。範囲は SliderRange() で決まる */
static void SliderRange(int id, int *mn, int *mx, int *step)
{
    switch (id)
    {
    case W_SL_HIDEMS:
        *mn = 100, *mx = 5000, *step = 100;
        break;
    case W_SL_ICON:
        *mn = 32, *mx = 96, *step = 2;
        break;
    case W_SL_ZOOM:
        *mn = 100, *mx = 200, *step = 5;
        break;
    case W_SL_PFSIZE:
        *mn = 100, *mx = 1200, *step = 20;
        break;
    case W_SL_CLSIZE:
        *mn = 60, *mx = 200, *step = 10;
        break;
    case W_SL_CLOPA:
        *mn = 30, *mx = 100, *step = 5;
        break;
    case W_SL_WSIZE:
        *mn = 60, *mx = 200, *step = 10;
        break;
    case W_SL_WOPA:
        *mn = 30, *mx = 100, *step = 5;
        break;
    case W_SL_SMSIZE:
        *mn = 60, *mx = 200, *step = 10;
        break;
    case W_SL_SMOPA:
        *mn = 30, *mx = 100, *step = 5;
        break;
    default: /* W_SL_OPACITY */
        *mn = 30, *mx = 100, *step = 5;
        break;
    }
}

static void Slider(int id, int x, int y, int w, int value, const wchar_t *label, BOOL en)
{
    int mn, mx, st;
    SliderRange(id, &mn, &mx, &st);
    float a = en ? 1.0f : 0.45f;
    int tw = w - 70;
    float t = Clampf((float)(value - mn) / (float)(mx - mn), 0, 1);
    int ty = y + 16;
    RR(x, ty - 2, tw, 4, 2, g_pal->track, a);
    RR(x, ty - 2, (int)(tw * t + 0.5f), 4, 2, g_pal->accent, a);
    float kx = x + tw * t;
    BOOL hv = (IsHover(id, 0) || (g_dragSlider && g_dragId == id)) && en;
    Dot(kx, (float)ty, hv ? 10.0f : 9.0f, g_pal->knob, a);
    Dot(kx, (float)ty, hv ? 6.5f : 5.5f, g_pal->accent, a);
    Txt(label, x + tw + 8, y, 62, 32, g_fBtn, en ? g_pal->text : g_pal->sub, FL_R);
    if (g_nhit < (int)(sizeof(g_hit) / sizeof(g_hit[0])))
    {
        AddHit(x - 10, y, tw + 20, 32, id, 0, en, g_inContent, 10);
        g_hit[g_nhit - 1].x0 = S(x);
        g_hit[g_nhit - 1].x1 = S(x + tw);
    }
}

/* ------------------------------------------------------------------ */
/* ページ共通の部品 (セクション / カード / 行)                              */
/* ------------------------------------------------------------------ */
static int g_y; /* コンテンツのカーソル (論理) */
static int g_cardTop, g_cardH;

static void Section(const wchar_t *t)
{
    Txt(t, CX + 4, g_y + 2, CW, 24, g_fSec, g_pal->text, FL_L);
    g_y += 32;
}

static void CardBegin(int h)
{
    g_cardTop = g_y;
    g_cardH = h;
    RR(CX, g_y, CW, h, 12, g_pal->card, 1.0f);
    RRs(CX, g_y, CW, h, 12, 1.0f, g_pal->border, 1.0f);
}
static void CardEnd(void) { g_y = g_cardTop + g_cardH + 22; }

/* 行 i の左側 (タイトル + 説明)。reserve は右側の部品が使う幅 */
static void RowText(int i, int rh, const wchar_t *title, const wchar_t *desc, int reserve, BOOL en)
{
    int top = g_cardTop + i * rh;
    if (i > 0)
        RR(CX + 18, top, CW - 36, 1, 0, g_pal->sep, 1.0f);
    int w = CW - 36 - reserve - 12;
    uint32_t tc = en ? g_pal->text : g_pal->sub;
    if (desc && desc[0])
    {
        Txt(title, CX + 18, top + 11, w, 22, g_fBody, tc, FL_L);
        Txt(desc, CX + 18, top + 33, w, 20, g_fSmall, g_pal->sub, FL_L);
    }
    else
        Txt(title, CX + 18, top, w, rh, g_fBody, tc, FL_L);
}
static int RowTop(int i, int rh) { return g_cardTop + i * rh; }
static int RightX(int w) { return CX + CW - 18 - w; }

static void Hint(const wchar_t *t)
{
    Txt(t, CX + 4, g_y, CW - 8, 36, g_fSmall, g_pal->sub, FL_W);
    g_y += 40;
}

/* ------------------------------------------------------------------ */
/* ページの中身                                                          */
/* ------------------------------------------------------------------ */
static void VkName(int vk, wchar_t *out, int cch)
{
    if (vk >= VK_F1 && vk <= VK_F24)
        _snwprintf(out, cch, L"F%d", vk - VK_F1 + 1);
    else if (vk == VK_PAUSE)
        wcsncpy(out, L"Pause", cch);
    else if (vk == VK_SCROLL)
        wcsncpy(out, L"Scroll Lock", cch);
    else
    {
        LONG sc = (LONG)MapVirtualKeyW((UINT)vk, 0) << 16;
        if (!sc || !GetKeyNameTextW(sc, out, cch))
            _snwprintf(out, cch, L"キー %d", vk);
    }
    out[cch - 1] = 0;
}

static void PageGeneral(void)
{
    if (g_first)
    {
        CardBegin(64);
        RR(CX, g_y, CW, 64, 12, g_pal->accent, g_dark ? 0.22f : 0.12f);
        RRs(CX, g_y, CW, 64, 12, 1.0f, g_pal->accent, 0.5f);
        Txt(L"ようこそ！使う機能を選んで「開始」を押してください。", CX + 18, g_y + 8, CW - 36, 26,
            g_fBody, g_pal->text, FL_L);
        Txt(L"ここで選んだ内容は、あとからいつでも変更できます。", CX + 18, g_y + 33, CW - 36, 22, g_fSmall, g_pal->sub,
            FL_L);
        CardEnd();
    }

    Section(L"機能");
    CardBegin(RH * 8);
    {
        RowText(0, RH, L"ドック", L"画面の端にアプリを並べて、ワンクリックで起動・切り替え", 150, TRUE);
        int t = RowTop(0, RH);
        Toggle(W_TG_DOCK, RightX(44), t + (RH - 24) / 2, g_dockOn, TRUE);
        Button(W_GOTO, PAGE_DOCK, RightX(44) - 12 - 88, t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        RowText(1, RH, L"ランチャー", L"ショートカットキーで開く、全画面のアプリ一覧", 150, TRUE);
        t = RowTop(1, RH);
        Toggle(W_TG_LN, RightX(44), t + (RH - 24) / 2, g_lnOn, TRUE);
        Button(W_GOTO, PAGE_LAUNCHER, RightX(44) - 12 - 88, t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        wchar_t d[64];
        _snwprintf(d, 64, L"デスクトップに置いている検索バー: %d 個", SearchBar_Count());
        d[63] = 0;
        RowText(2, RH, L"デスクトップ検索バー", d, 110, TRUE);
        t = RowTop(2, RH);
        Button(W_GOTO, PAGE_SEARCH, RightX(88), t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        _snwprintf(d, 64, L"デスクトップに置いている画像・GIF: %d 個", PhotoFrame_Count());
        d[63] = 0;
        RowText(3, RH, L"画像・GIF追加", d, 110, TRUE);
        t = RowTop(3, RH);
        Button(W_GOTO, PAGE_PHOTO, RightX(88), t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        RowText(4, RH, L"時計", L"デスクトップに置く、デジタル / アナログの時計", 150, TRUE);
        t = RowTop(4, RH);
        Toggle(W_TG_CLOCK, RightX(44), t + (RH - 24) / 2, Clock_Enabled(), TRUE);
        Button(W_GOTO, PAGE_CLOCK, RightX(44) - 12 - 88, t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        RowText(5, RH, L"天気", L"デスクトップに置く、天気と気温 (場所は選べます)", 150, TRUE);
        t = RowTop(5, RH);
        Toggle(W_TG_WEATHER, RightX(44), t + (RH - 24) / 2, Weather_Enabled(), TRUE);
        Button(W_GOTO, PAGE_WEATHER, RightX(44) - 12 - 88, t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        RowText(6, RH, L"壁紙（試験的）", L"オーロラ・星空などの内蔵アニメや、GIF を壁紙にする", 150, TRUE);
        t = RowTop(6, RH);
        Toggle(W_TG_WALL, RightX(44), t + (RH - 24) / 2, Wallpaper_Enabled(), TRUE);
        Button(W_GOTO, PAGE_WALLPAPER, RightX(44) - 12 - 88, t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);

        RowText(7, RH, L"システムモニタ", L"デスクトップに置く、CPU・メモリ・ディスク・通信速度の表示", 150, TRUE);
        t = RowTop(7, RH);
        Toggle(W_TG_SMON, RightX(44), t + (RH - 24) / 2, SysMon_Enabled(), TRUE);
        Button(W_GOTO, PAGE_SYSMON, RightX(44) - 12 - 88, t + (RH - 34) / 2, 88, 34, L"詳細設定", 0, TRUE);
    }
    CardEnd();

    Section(L"外観");
    CardBegin(RH);
    {
        static const wchar_t *const th[] = {L"自動", L"ライト", L"ダーク"};
        RowText(0, RH, L"テーマ", L"自動にすると Windows の設定に合わせます", 250, TRUE);
        Segmented(W_SEG_THEME, RightX(246), RowTop(0, RH) + (RH - 34) / 2, 246, th, 3, g_theme, TRUE);
    }
    CardEnd();

    Section(L"システム");
    int rows = g_first ? 2 : 3;
    CardBegin(RH * rows);
    {
        RowText(0, RH, L"Windows の起動時に自動で開始", L"ログイン時に PromDock をバックグラウンドで起動します", 60, TRUE);
        Toggle(W_TG_AUTOSTART, RightX(44), RowTop(0, RH) + (RH - 24) / 2, g_autoStart, TRUE);

        RowText(1, RH, L"設定ファイルのフォルダ", L"PromDock.exe と同じ場所に、設定が保存されています", 110, TRUE);
        Button(W_BTN_OPENDIR, 0, RightX(100), RowTop(1, RH) + (RH - 34) / 2, 100, 34, L"フォルダを開く", 0, TRUE);

        if (!g_first)
        {
            RowText(2, RH, L"PromDock を終了", L"ドック・ランチャー・検索バー・画像・時計をすべて閉じます", 110, TRUE);
            Button(W_BTN_QUIT, 0, RightX(100), RowTop(2, RH) + (RH - 34) / 2, 100, 34, L"終了する", 2, TRUE);
        }
    }
    CardEnd();
}

static void PageDock(void)
{
    BOOL on = g_dockOn;
    Section(L"ドックの数 (最大3つ)");
    CardBegin(RH * 2);
    {
        static const wchar_t *const dk[] = {L"ドック1", L"ドック2", L"ドック3"};
        int nd = Dock_Count();
        if (nd > 3)
            nd = 3;
        RowText(0, RH, L"編集するドック", L"この下の設定は、選んだドックに適用されます。アイコン一覧は各ドックで独立しています", 90 * nd + 20, on);
        Segmented(W_SEG_DKSEL, RightX(90 * nd), RowTop(0, RH) + (RH - 34) / 2, 90 * nd, dk, nd, Dock_GetEdit(), on);

        RowText(1, RH, L"ドックの追加と削除", L"追加すると、まだ使っていない画面の端に空のドックが置かれます", 220, on);
        Button(W_BTN_DKDEL, 0, RightX(210), RowTop(1, RH) + (RH - 34) / 2, 100, 34, L"削除", 0, on && nd > 1);
        Button(W_BTN_DKADD, 0, RightX(100), RowTop(1, RH) + (RH - 34) / 2, 100, 34, L"追加", 0, on && nd < 3);
    }
    CardEnd();

    Section(L"表示");
    CardBegin(RH * 4);
    {
        static const wchar_t *const pos[] = {L"下", L"上", L"左", L"右"};
        static const wchar_t *const zo[] = {L"最前列", L"最奥"};
        RowText(0, RH, L"ドックを有効にする", L"オフにすると、ドックを非表示にします", 60, TRUE);
        Toggle(W_TG_DOCK, RightX(44), RowTop(0, RH) + (RH - 24) / 2, g_dockOn, TRUE);

        RowText(1, RH, L"表示位置", L"ドックを置く画面の端", 260, on);
        Segmented(W_SEG_EDGE, RightX(240), RowTop(1, RH) + (RH - 34) / 2, 240, pos, 4, Dock_GetEdge(), on);

        RowText(2, RH, L"端に沿った位置", L"中央に戻します。Ctrl + ドラッグで自由に動かせます", 110, on);
        Button(W_BTN_RESETPOS, 0, RightX(100), RowTop(2, RH) + (RH - 34) / 2, 100, 34, L"中央に戻す", 0, on);

        RowText(3, RH, L"重なり順", L"最前列: 常に他のウィンドウの手前 / 最奥: デスクトップの上・他のウィンドウの背面", 200, on);
        Segmented(W_SEG_DKZ, RightX(180), RowTop(3, RH) + (RH - 34) / 2, 180, zo, 2, Dock_GetZOrder(), on);
    }
    CardEnd();

    Section(L"動作");
    BOOL ah = Dock_GetAutoHide();
    CardBegin(RH * 3);
    {
        RowText(0, RH, L"自動的に隠す", L"マウスを離すと、画面の端へ隠れます", 60, on);
        Toggle(W_TG_AUTOHIDE, RightX(44), RowTop(0, RH) + (RH - 24) / 2, ah, on);

        wchar_t lb[24];
        int ms = Dock_GetHideMs();
        _snwprintf(lb, 24, L"%.1f 秒", ms / 1000.0f);
        lb[23] = 0;
        RowText(1, RH, L"隠れるまでの時間", L"マウスを離してから隠れるまでの待ち時間", 290, on && ah);
        Slider(W_SL_HIDEMS, RightX(280), RowTop(1, RH) + (RH - 32) / 2, 280, ms, lb, on && ah);

        RowText(2, RH, L"通知バッジ", L"アプリの未読数 (タイトルの「(3)」など) やタスクバーの点滅を、アイコンの右上に赤いバッジで表示します", 60, on);
        Toggle(W_TG_NOTICE, RightX(44), RowTop(2, RH) + (RH - 24) / 2, Notice_Enabled(), on);
    }
    CardEnd();

    Section(L"外観");
    CardBegin(RH * 5);
    {
        const wchar_t *tn[4];
        int tc = Dock_TypeCount();
        if (tc > 4)
            tc = 4;
        for (int i = 0; i < tc; i++)
            tn[i] = Dock_TypeName(i);
        RowText(0, RH, L"ドックの形", L"バー: 横長 / 半円: 端を中心に円弧状 / 全円: 端に接する円環状", 250, on);
        Segmented(W_SEG_DKTYPE, RightX(240), RowTop(0, RH) + (RH - 34) / 2, 240, tn, tc, Dock_GetType(), on);

        const wchar_t *dn[8];
        int dc = Dock_StyleCount();
        if (dc > 8)
            dc = 8;
        for (int i = 0; i < dc; i++)
            dn[i] = Dock_StyleName(i);
        RowText(1, RH, L"デザイン", L"バー・サークルのどちらにも適用されます", 466, on);
        Segmented(W_SEG_DKSTYLE, RightX(460), RowTop(1, RH) + (RH - 34) / 2, 460, dn, dc, Dock_GetStyle(), on);

        wchar_t lb[24];
        int v = Dock_GetIconSize();
        _snwprintf(lb, 24, L"%d px", v);
        lb[23] = 0;
        RowText(2, RH, L"アイコンのサイズ", L"ドックに並ぶアイコンの大きさ", 290, on);
        Slider(W_SL_ICON, RightX(280), RowTop(2, RH) + (RH - 32) / 2, 280, v, lb, on);

        v = Dock_GetZoom();
        _snwprintf(lb, 24, L"%d %%", v);
        lb[23] = 0;
        RowText(3, RH, L"ホバー時の拡大率", L"100% にすると拡大しません", 290, on);
        Slider(W_SL_ZOOM, RightX(280), RowTop(3, RH) + (RH - 32) / 2, 280, v, lb, on);

        v = Dock_GetOpacity();
        _snwprintf(lb, 24, L"%d %%", v);
        lb[23] = 0;
        RowText(4, RH, L"背景の濃さ", L"小さいほど背景が透けます", 290, on);
        Slider(W_SL_OPACITY, RightX(280), RowTop(4, RH) + (RH - 32) / 2, 280, v, lb, on);
    }
    CardEnd();
    Hint(L"アプリの追加はファイルやフォルダをドックへドラッグ&ドロップ、削除はドックの外へドラッグするか右クリックから行います。");
}

static void PageLauncher(void)
{
    BOOL on = g_lnOn;
    Section(L"基本");
    CardBegin(RH * 2);
    {
        wchar_t kn[32];
        VkName(Launcher_GetHotkey(), kn, 32);
        RowText(0, RH, L"ランチャーを有効にする", L"オフにすると、ショートカットキーも無効になります", 60, TRUE);
        Toggle(W_TG_LN, RightX(44), RowTop(0, RH) + (RH - 24) / 2, g_lnOn, TRUE);

        RowText(1, RH, L"開閉のショートカットキー", L"どのアプリを使っていても、このキーで開閉できます", 150, on);
        Dropdown(W_DD_HOTKEY, RightX(140), RowTop(1, RH) + (RH - 34) / 2, 140, kn, on);
    }
    CardEnd();

    Section(L"表示方法");
    CardBegin(RH * 2);
    {
        static const wchar_t *const st[] = {L"通常", L"端に隠す"};
        static const wchar_t *const ed[] = {L"左", L"右", L"上", L"下"};
        RowText(0, RH, L"表示スタイル", L"端に隠すと、タブをクリックしてスライド表示", 250, on);
        Segmented(W_SEG_LNSTYLE, RightX(240), RowTop(0, RH) + (RH - 34) / 2, 240, st, 2, g_lnStyle, on);

        RowText(1, RH, L"タブを置く端", L"「端に隠す」を選んだときに使います", 260, on && g_lnStyle == 1);
        Segmented(W_SEG_LNEDGE, RightX(240), RowTop(1, RH) + (RH - 34) / 2, 240, ed, 4, g_lnEdge,
                  on && g_lnStyle == 1);
    }
    CardEnd();

    Section(L"グリッド");
    CardBegin(RH * 2);
    {
        int cols = Launcher_GetCols(), rows = Launcher_GetRows();
        wchar_t t[16];
        if (cols > 0)
            _snwprintf(t, 16, L"%d 列", cols);
        else
            wcscpy(t, L"自動");
        RowText(0, RH, L"列数", L"「自動」にすると画面の大きさに合わせます", 170, on);
        Stepper(W_STP_COLS, RightX(148), RowTop(0, RH) + (RH - 34) / 2, t, on, cols > 0, cols < 20);

        if (rows > 0)
            _snwprintf(t, 16, L"%d 行", rows);
        else
            wcscpy(t, L"自動");
        RowText(1, RH, L"行数", L"1 ページに並べる行数です", 170, on);
        Stepper(W_STP_ROWS, RightX(148), RowTop(1, RH) + (RH - 34) / 2, t, on, rows > 0, rows < 20);
    }
    CardEnd();
    Hint(L"アプリの追加・削除・並べ替えは、ランチャーを開いて右クリックメニューから行います。");
}

static void PageSearch(void)
{
    int n = SearchBar_Count();
    Section(L"検索バーを追加");
    CardBegin(RH * 3);
    {
        wchar_t t[96];
        SearchBar_BrowserLabel(g_sbBrowser, t, 96);
        RowText(0, RH, L"ブラウザ", L"検索結果を開くブラウザ", 230, TRUE);
        Dropdown(W_DD_BROWSER, RightX(220), RowTop(0, RH) + (RH - 34) / 2, 220, t, TRUE);

        RowText(1, RH, L"検索エンジン", L"検索に使うサービス", 230, TRUE);
        Dropdown(W_DD_ENGINE, RightX(220), RowTop(1, RH) + (RH - 34) / 2, 220, SearchBar_EngineName(g_sbEngine), TRUE);

        wchar_t d[48];
        _snwprintf(d, 48, L"追加済み %d / %d 個", n, SB_LIMIT);
        d[47] = 0;
        RowText(2, RH, L"この組み合わせで追加", d, 110, TRUE);
        Button(W_BTN_ADD, 0, RightX(100), RowTop(2, RH) + (RH - 34) / 2, 100, 34, L"追加", 1, n < SB_LIMIT);
    }
    CardEnd();

    Section(L"デスクトップにある検索バー");
    int lh = 52;
    CardBegin(n > 0 ? n * lh : 64);
    if (n == 0)
        Txt(L"検索バーはまだありません。上のフォームから追加できます。", CX, g_cardTop, CW, 64, g_fBody, g_pal->sub, FL_C);
    for (int i = 0; i < n; i++)
    {
        int top = g_cardTop + i * lh;
        wchar_t d[160];
        SearchBar_Describe(i, d, 160);
        if (i > 0)
            RR(CX + 18, top, CW - 36, 1, 0, g_pal->sep, 1.0f);
        RR(CX + 18, top + 12, 28, 28, 8, g_pal->accent, g_dark ? 0.28f : 0.14f);
        /* 虫眼鏡 */
        float mx = CX + 30.5f, my = (float)(top + 25.0f);
        Dot(mx, my, 5.2f, g_pal->accent, 1.0f);
        Dot(mx, my, 3.4f, Mix(g_pal->card, g_pal->accent, g_dark ? 0.28f : 0.14f), 1.0f);
        Ln(mx + 3.8f, my + 3.8f, mx + 7.5f, my + 7.5f, 1.8f, g_pal->accent, 1.0f);
        Txt(d, CX + 58, top, CW - 58 - 100, lh, g_fBody, g_pal->text, FL_L);
        Button(W_BTN_DEL, i, RightX(72), top + (lh - 32) / 2, 72, 32, L"削除", 2, TRUE);
    }
    CardEnd();

    Section(L"配置");
    CardBegin(RH);
    {
        BOOL mv = SearchBar_MoveMode();
        RowText(0, RH, L"検索バーの位置を動かす", mv ? L"移動中です。検索バーをドラッグして好きな場所へ置いてください" : L"オンにすると、検索バーをドラッグで移動できます",
                140, n > 0);
        Button(W_BTN_MOVE, 0, RightX(130), RowTop(0, RH) + (RH - 34) / 2, 130, 34, mv ? L"移動を終了" : L"移動を開始",
               mv ? 1 : 0, n > 0);
    }
    CardEnd();
}

/* 額縁に山と太陽の小さな絵 (サイドバーと一覧で使う) */
static void PicGlyph(float cx, float cy, uint32_t c)
{
    RRs((int)(cx - 9), (int)(cy - 8), 18, 16, 3, 1.6f, c, 1.0f);
    Dot(cx + 3.6f, cy - 3.2f, 1.9f, c, 1.0f);
    Ln(cx - 6.5f, cy + 4.5f, cx - 2.0f, cy - 0.5f, 1.6f, c, 1.0f);
    Ln(cx - 2.0f, cy - 0.5f, cx + 1.5f, cy + 3.5f, 1.6f, c, 1.0f);
    Ln(cx + 0.0f, cy + 2.0f, cx + 3.0f, cy - 0.5f, 1.6f, c, 1.0f);
    Ln(cx + 3.0f, cy - 0.5f, cx + 6.5f, cy + 4.5f, 1.6f, c, 1.0f);
}

static void PagePhoto(void)
{
    int n = PhotoFrame_Count(), lim = PhotoFrame_Limit();
    Section(L"画像・GIF を追加");
    CardBegin(RH * 2);
    {
        wchar_t lb[24];
        _snwprintf(lb, 24, L"%d px", g_pfNewSize);
        lb[23] = 0;
        RowText(0, RH, L"大きさ", L"長い辺の長さ (縦横比はそのまま)", 300, TRUE);
        Slider(W_SL_PFSIZE, RightX(290), RowTop(0, RH) + (RH - 32) / 2, 290, g_pfNewSize, lb, TRUE);

        wchar_t d[96];
        _snwprintf(d, 96, L"追加済み %d / %d 個  ·  PNG・JPEG・BMP・GIF など", n, lim);
        d[95] = 0;
        RowText(1, RH, L"画像・GIF を選んで追加", d, 140, TRUE);
        Button(W_BTN_PFADD, 0, RightX(130), RowTop(1, RH) + (RH - 34) / 2, 130, 34, L"ファイルを選ぶ", 1, n < lim);
    }
    CardEnd();

    Section(L"デスクトップにある画像・GIF");
    int lh = 60;
    CardBegin(n > 0 ? n * lh : 64);
    if (n == 0)
        Txt(L"画像はまだありません。上の「ファイルを選ぶ」から追加できます。", CX, g_cardTop, CW, 64, g_fBody, g_pal->sub, FL_C);
    for (int i = 0; i < n; i++)
    {
        int top = g_cardTop + i * lh;
        wchar_t nm[MAX_PATH], info[160];
        PhotoFrame_Name(i, nm, MAX_PATH);
        PhotoFrame_Info(i, info, 160);
        if (i > 0)
            RR(CX + 18, top, CW - 36, 1, 0, g_pal->sep, 1.0f);
        RR(CX + 18, top + 16, 28, 28, 8, g_pal->accent, g_dark ? 0.28f : 0.14f);
        PicGlyph(CX + 32.0f, top + 30.0f, g_pal->accent);

        int sx = RightX(72) - 12 - 148; /* 大きさの [-] [px] [+] */
        int tw = sx - 12 - (CX + 58);
        Txt(nm, CX + 58, top + 8, tw, 24, g_fBody, g_pal->text, FL_L);
        Txt(info, CX + 58, top + 31, tw, 20, g_fSmall, g_pal->sub, FL_L);

        int sz = PhotoFrame_GetSize(i);
        wchar_t st[24];
        _snwprintf(st, 24, L"%d px", sz);
        st[23] = 0;
        StepperA(W_STP_PFSIZE, i * 2, sx, top + (lh - 34) / 2, st, TRUE, sz > 100, sz < 1200);
        Button(W_BTN_PFDEL, i, RightX(72), top + (lh - 32) / 2, 72, 32, L"削除", 2, TRUE);
    }
    CardEnd();

    Section(L"配置");
    CardBegin(RH);
    {
        BOOL mv = PhotoFrame_Movable();
        RowText(0, RH, L"ドラッグで移動できるようにする",
                mv ? L"オフにすると、画像を誤って動かさなくなります" : L"オンにすると、画像をドラッグで好きな場所へ置けます", 60, TRUE);
        Toggle(W_TG_PFMOVE, RightX(44), RowTop(0, RH) + (RH - 24) / 2, mv, TRUE);
    }
    CardEnd();
    Hint(L"デスクトップの画像を右クリックすると、大きさの変更や削除もできます。画像は他のウィンドウの後ろ (デスクトップの上) に表示されます。");
}

/* 時計のアイコン (文字盤 + 針)。bg は文字盤の中を抜くための背景色 */
static void ClockGlyph(float cx, float cy, uint32_t c, uint32_t bg)
{
    Dot(cx, cy, 9.0f, c, 1.0f);
    Dot(cx, cy, 7.2f, bg, 1.0f);
    Ln(cx, cy, cx, cy - 5.0f, 1.7f, c, 1.0f);
    Ln(cx, cy, cx + 3.6f, cy + 2.0f, 1.7f, c, 1.0f);
}

static void PageClock(void)
{
    BOOL on = Clock_Enabled();
    BOOL dig = Clock_GetStyle() == 0;
    Section(L"時計");
    CardBegin(RH);
    {
        RowText(0, RH, L"デスクトップに時計を表示", L"オンにすると、デスクトップに時計が現れます", 60, TRUE);
        Toggle(W_TG_CLOCK, RightX(44), RowTop(0, RH) + (RH - 24) / 2, on, TRUE);
    }
    CardEnd();

    Section(L"デザイン");
    CardBegin(RH * 7);
    {
        static const wchar_t *const st[] = {L"デジタル", L"アナログ"};
        static const wchar_t *const vd[] = {L"カード", L"細字", L"縦並び", L"分小", L"7セグ"};
        static const wchar_t *const va[] = {L"カード", L"リング", L"波・白", L"波・黒", L"波・枠", L"石・明", L"石・暗"};
        RowText(0, RH, L"スタイル", L"数字で表示するか、文字盤と針で表示するか", 220, on);
        Segmented(W_SEG_CLSTYLE, RightX(210), RowTop(0, RH) + (RH - 34) / 2, 210, st, 2, Clock_GetStyle(), on);

        RowText(1, RH, L"デザイン", dig ? L"カード・細字・縦並び・分小・7セグ" : L"カード・リング・波形・コンクリート", 400, on);
        Segmented(W_SEG_CLVAR, RightX(390), RowTop(1, RH) + (RH - 34) / 2, 390, dig ? vd : va, dig ? 5 : 7,
                  Clock_GetVariant(), on);

        RowText(2, RH, L"24時間表示",
                dig ? L"オフにすると、午前 / 午後 (AM / PM) の12時間表示になります" : L"アナログ時計では使いません", 60,
                on && dig);
        Toggle(W_TG_CL24, RightX(44), RowTop(2, RH) + (RH - 24) / 2, Clock_Get24h(), on && dig);

        RowText(3, RH, L"秒を表示", L"秒まで表示します (アナログでは秒針。縦並び・分小・7セグでは使いません)", 60, on);
        Toggle(W_TG_CLSEC, RightX(44), RowTop(3, RH) + (RH - 24) / 2, Clock_GetSeconds(), on);

        RowText(4, RH, L"日付を表示", L"月日と曜日を表示します (細字・7セグ・コンクリートでは使いません)", 60, on);
        Toggle(W_TG_CLDATE, RightX(44), RowTop(4, RH) + (RH - 24) / 2, Clock_GetDate(), on);

        wchar_t lb[16];
        _snwprintf(lb, 16, L"%d%%", Clock_GetSize());
        lb[15] = 0;
        RowText(5, RH, L"大きさ", L"時計全体の大きさ", 300, on);
        Slider(W_SL_CLSIZE, RightX(290), RowTop(5, RH) + (RH - 32) / 2, 290, Clock_GetSize(), lb, on);

        _snwprintf(lb, 16, L"%d%%", Clock_GetOpacity());
        lb[15] = 0;
        RowText(6, RH, L"背景の濃さ", L"背景のあるデザインで有効です (薄くすると透けます)", 300, on);
        Slider(W_SL_CLOPA, RightX(290), RowTop(6, RH) + (RH - 32) / 2, 290, Clock_GetOpacity(), lb, on);
    }
    CardEnd();

    Section(L"配置");
    CardBegin(RH * 2);
    {
        BOOL mv = Clock_Movable();
        RowText(0, RH, L"ドラッグで移動できるようにする",
                mv ? L"オフにすると、時計を誤って動かさなくなります" : L"オンにすると、時計をドラッグで好きな場所へ置けます", 60,
                TRUE);
        Toggle(W_TG_CLMOVE, RightX(44), RowTop(0, RH) + (RH - 24) / 2, mv, TRUE);

        RowText(1, RH, L"位置をリセット", L"時計を画面の右上に戻します", 140, on);
        Button(W_BTN_CLRESET, 0, RightX(130), RowTop(1, RH) + (RH - 34) / 2, 130, 34, L"右上に戻す", 0, on);
    }
    CardEnd();
    Hint(L"デスクトップの時計を右クリックしても、デザインの変更や非表示ができます。");
}

/* システムモニタのアイコン (棒グラフ) */
static void SysMonGlyph(float cx, float cy, uint32_t c, uint32_t bg)
{
    (void)bg;
    static const float hs[3] = {7.0f, 13.0f, 10.0f};
    for (int i = 0; i < 3; i++)
    {
        float x = cx - 8.0f + i * 6.0f;
        FillRRd(x * g_dpi, FY(0) + (cy + 8.0f - hs[i]) * g_dpi, (x + 4.0f) * g_dpi, FY(0) + (cy + 8.0f) * g_dpi,
                1.2f * g_dpi, c, 1.0f);
    }
}

static void PageSysMon(void)
{
    BOOL on = SysMon_Enabled();
    Section(L"システムモニタ");
    CardBegin(RH);
    {
        RowText(0, RH, L"デスクトップにシステムモニタを表示", L"オンにすると、CPU・メモリなどの使用状況がデスクトップに現れます", 60, TRUE);
        Toggle(W_TG_SMON, RightX(44), RowTop(0, RH) + (RH - 24) / 2, on, TRUE);
    }
    CardEnd();

    Section(L"デザイン");
    CardBegin(RH * 4);
    {
        static const wchar_t *const vd[] = {
            L"項目ごとに、横長の棒グラフで使用率を表示します",
            L"項目を横に並べて、円形のメーターで表示します",
            L"文字だけで、1行ずつ表示します",
            L"直近1分ほどの推移を、項目ごとの線グラフで表示します (ネットワークは受信・送信の2本線)",
            L"推移の線の下を塗りつぶした、エリアグラフで表示します",
            L"LED のような細かい区切りのメーターで表示します",
            L"半円のメーター (スピードメーター風) を横に並べて表示します",
            L"縦長の棒を横に並べて表示します",
            L"小さな丸い帯を横一列に並べて、コンパクトに表示します"};
        int var = SysMon_GetVariant();
        RowText(0, RH, L"デザイン", L"9種類から選べます", 230, on);
        Dropdown(W_DD_SMVAR, RightX(220), RowTop(0, RH) + (RH - 34) / 2, 220, SysMon_VariantName(var), on);

        RowText(1, RH, L"このデザインの表示内容", vd[ClampInt(var, 0, 8)], 0, on);

        wchar_t lb[16];
        _snwprintf(lb, 16, L"%d%%", SysMon_GetSize());
        lb[15] = 0;
        RowText(2, RH, L"大きさ", L"モニタ全体の大きさ", 300, on);
        Slider(W_SL_SMSIZE, RightX(290), RowTop(2, RH) + (RH - 32) / 2, 290, SysMon_GetSize(), lb, on);

        _snwprintf(lb, 16, L"%d%%", SysMon_GetOpacity());
        lb[15] = 0;
        RowText(3, RH, L"背景の濃さ", L"薄くすると透けます (ピルは1つずつの背景の濃さ)", 300, on);
        Slider(W_SL_SMOPA, RightX(290), RowTop(3, RH) + (RH - 32) / 2, 290, SysMon_GetOpacity(), lb, on);
    }
    CardEnd();

    Section(L"表示する項目");
    CardBegin(RH * 5);
    {
        RowText(0, RH, L"CPU", L"CPU の使用率", 60, on);
        Toggle(W_TG_SMCPU, RightX(44), RowTop(0, RH) + (RH - 24) / 2, SysMon_GetItem(0), on);
        RowText(1, RH, L"メモリ", L"メモリの使用率と使用量", 60, on);
        Toggle(W_TG_SMMEM, RightX(44), RowTop(1, RH) + (RH - 24) / 2, SysMon_GetItem(1), on);
        RowText(2, RH, L"ディスク", L"Windows があるドライブの使用率と空き容量", 60, on);
        Toggle(W_TG_SMDISK, RightX(44), RowTop(2, RH) + (RH - 24) / 2, SysMon_GetItem(2), on);
        RowText(3, RH, L"ネットワーク", L"受信 (↓) と送信 (↑) の速さ", 60, on);
        Toggle(W_TG_SMNET, RightX(44), RowTop(3, RH) + (RH - 24) / 2, SysMon_GetItem(3), on);

        static const wchar_t *const iv[] = {L"1秒", L"2秒", L"5秒"};
        RowText(4, RH, L"更新間隔", L"短いほど細かく動きますが、わずかに負荷が増えます", 260, on);
        Segmented(W_SEG_SMINT, RightX(180), RowTop(4, RH) + (RH - 34) / 2, 180, iv, 3, SysMon_GetInterval(), on);
    }
    CardEnd();

    Section(L"配置");
    CardBegin(RH * 2);
    {
        BOOL mv = SysMon_Movable();
        RowText(0, RH, L"ドラッグで移動できるようにする",
                mv ? L"オフにすると、モニタを誤って動かさなくなります" : L"オンにすると、モニタをドラッグで好きな場所へ置けます", 60,
                TRUE);
        Toggle(W_TG_SMMOVE, RightX(44), RowTop(0, RH) + (RH - 24) / 2, mv, TRUE);

        RowText(1, RH, L"位置をリセット", L"モニタを初期位置 (画面の右上) に戻します", 140, on);
        Button(W_BTN_SMRESET, 0, RightX(130), RowTop(1, RH) + (RH - 34) / 2, 130, 34, L"初期位置に戻す", 0, on);
    }
    CardEnd();
    Hint(L"最後の1項目は消せません。デスクトップのモニタを右クリックしても、設定を変えられます。");
}

/* 天気のアイコン (太陽 + 雲)。bg は雲の縁を抜くための背景色 */
static void WeatherGlyph(float cx, float cy, uint32_t c, uint32_t bg)
{
    Dot(cx - 4.5f, cy - 4.5f, 3.6f, c, 1.0f);
    Ln(cx - 4.5f, cy - 10.5f, cx - 4.5f, cy - 8.8f, 1.5f, c, 1.0f);
    Ln(cx - 10.5f, cy - 4.5f, cx - 8.8f, cy - 4.5f, 1.5f, c, 1.0f);
    Ln(cx - 8.8f, cy - 8.8f, cx - 7.6f, cy - 7.6f, 1.5f, c, 1.0f);
    Dot(cx + 1.0f, cy + 1.0f, 6.0f, bg, 1.0f); /* 雲の縁 (太陽を少し隠す) */
    Dot(cx - 1.5f, cy + 3.0f, 4.0f, c, 1.0f);
    Dot(cx + 2.5f, cy + 0.5f, 5.0f, c, 1.0f);
    Dot(cx + 7.0f, cy + 3.5f, 3.4f, c, 1.0f);
    Ln(cx - 1.5f, cy + 6.4f, cx + 7.0f, cy + 6.4f, 3.4f, c, 1.0f);
}

static void PageWeather(void)
{
    BOOL on = Weather_Enabled();
    int var = Weather_GetVariant();
    Section(L"天気");
    CardBegin(RH);
    {
        RowText(0, RH, L"デスクトップに天気を表示", L"オンにすると、デスクトップに天気と気温が現れます", 60, TRUE);
        Toggle(W_TG_WEATHER, RightX(44), RowTop(0, RH) + (RH - 24) / 2, on, TRUE);
    }
    CardEnd();

    Section(L"場所");
    CardBegin(RH * 2);
    {
        wchar_t lb[96];
        Weather_LocLabel(lb, 96);
        RowText(0, RH, L"場所", L"リストの最後で緯度・経度も指定できます", 250, TRUE);
        Dropdown(W_DD_WCITY, RightX(240), RowTop(0, RH) + (RH - 34) / 2, 240, lb, TRUE);

        double la, lo;
        Weather_GetLatLon(&la, &lo);
        wchar_t d[96];
        _snwprintf(d, 96, L"いまの場所: 緯度 %.4f  /  経度 %.4f", la, lo);
        d[95] = 0;
        RowText(1, RH, L"緯度・経度を入力", d, 140, TRUE);
        Button(W_BTN_WCOORD, 0, RightX(130), RowTop(1, RH) + (RH - 34) / 2, 130, 34, L"入力する…", 0, TRUE);
    }
    CardEnd();

    Section(L"デザイン");
    CardBegin(RH * 5);
    {
        static const wchar_t *const vd[] = {
            L"天気・気温・湿度・最高/最低気温を、カードにまとめて表示します",
            L"背景なしで、天気アイコンと大きな気温・場所・天気だけを表示します",
            L"いまの天気に加えて、今日から5日分の予報 (天気と最高/最低気温) を表示します",
            L"縦長のカードに、気温・天気・最高/最低・湿度・風を表示します",
            L"小さな丸い帯に、アイコンと気温・場所だけを表示します",
            L"いまの天気と、今日から5日分の予報を縦に並べて表示します",
            L"青い背景のカードに、天気・気温・湿度・最高/最低を表示します",
            L"背景なしで、大きな天気アイコンと気温・天気・場所を表示します",
            L"背景なしで、気温だけをとても大きく表示します"};
        static const wchar_t *const un[] = {L"℃", L"℉"};
        RowText(0, RH, L"デザイン", L"デザインごとに表示される内容が変わります", 230, on);
        Dropdown(W_DD_WVAR, RightX(220), RowTop(0, RH) + (RH - 34) / 2, 220, Weather_VariantName(var), on);

        RowText(1, RH, L"このデザインの表示内容", vd[ClampInt(var, 0, 8)], 0, on);

        RowText(2, RH, L"気温の単位", L"摂氏 (℃) か華氏 (℉) か", 150, on);
        Segmented(W_SEG_WUNIT, RightX(140), RowTop(2, RH) + (RH - 34) / 2, 140, un, 2, Weather_GetUnit(), on);

        wchar_t lb[16];
        _snwprintf(lb, 16, L"%d%%", Weather_GetSize());
        lb[15] = 0;
        RowText(3, RH, L"大きさ", L"天気ウィジェット全体の大きさ", 300, on);
        Slider(W_SL_WSIZE, RightX(290), RowTop(3, RH) + (RH - 32) / 2, 290, Weather_GetSize(), lb, on);

        _snwprintf(lb, 16, L"%d%%", Weather_GetOpacity());
        lb[15] = 0;
        RowText(4, RH, L"背景の濃さ", L"背景があるデザインで有効です", 300, on);
        Slider(W_SL_WOPA, RightX(290), RowTop(4, RH) + (RH - 32) / 2, 290, Weather_GetOpacity(), lb, on);
    }
    CardEnd();

    Section(L"更新");
    CardBegin(RH);
    {
        wchar_t st[96];
        Weather_Status(st, 96);
        RowText(0, RH, L"天気データ", st, 140, on);
        Button(W_BTN_WREFRESH, 0, RightX(130), RowTop(0, RH) + (RH - 34) / 2, 130, 34, L"今すぐ更新", 0, on);
    }
    CardEnd();

    Section(L"配置");
    CardBegin(RH * 2);
    {
        BOOL mv = Weather_Movable();
        RowText(0, RH, L"ドラッグで移動できるようにする",
                mv ? L"オフにすると、天気を誤って動かさなくなります" : L"オンにすると、天気をドラッグで好きな場所へ置けます", 60,
                TRUE);
        Toggle(W_TG_WMOVE, RightX(44), RowTop(0, RH) + (RH - 24) / 2, mv, TRUE);

        RowText(1, RH, L"位置をリセット", L"天気を画面の左上に戻します", 140, on);
        Button(W_BTN_WRESET, 0, RightX(130), RowTop(1, RH) + (RH - 34) / 2, 130, 34, L"左上に戻す", 0, on);
    }
    CardEnd();
    Hint(L"天気は Open-Meteo (open-meteo.com) からインターネット経由で取得し、30分ごとに更新します。デスクトップの天気を右クリックしても、デザイン・場所・単位を変えられます。");
}

/* 壁紙のアイコン (モニター + 波) */
static void WallpaperGlyph(float cx, float cy, uint32_t c, uint32_t bg)
{
    Dot(cx, cy - 1.0f, 9.0f, c, 1.0f);
    Dot(cx, cy - 1.0f, 7.4f, bg, 1.0f);
    Ln(cx - 5.0f, cy + 1.0f, cx - 2.0f, cy - 2.5f, 1.6f, c, 1.0f);
    Ln(cx - 2.0f, cy - 2.5f, cx + 1.5f, cy + 1.0f, 1.6f, c, 1.0f);
    Ln(cx + 1.5f, cy + 1.0f, cx + 5.0f, cy - 2.0f, 1.6f, c, 1.0f);
}

static void PageWallpaper(void)
{
    BOOL on = Wallpaper_Enabled();
    int kind = Wallpaper_GetKind();
    Section(L"壁紙（試験的）");
    CardBegin(RH);
    {
        RowText(0, RH, L"壁紙（試験的）を使う", L"オンにすると、デスクトップの壁紙が動きます", 60, TRUE);
        Toggle(W_TG_WALL, RightX(44), RowTop(0, RH) + (RH - 24) / 2, on, TRUE);
    }
    CardEnd();

    Section(L"種類");
    CardBegin(RH * 3);
    {
        wchar_t d[MAX_PATH + 16];
        RowText(0, RH, L"壁紙の種類", L"内蔵アニメ・画像・GIF・動画 (mp4) から選べます", 230, on);
        Dropdown(W_DD_WALLKIND, RightX(220), RowTop(0, RH) + (RH - 34) / 2, 220, Wallpaper_KindName(kind), on);

        Wallpaper_FileLabel(d, MAX_PATH + 16);
        RowText(1, RH, L"画像・GIF", d, 140, on);
        Button(W_BTN_WALLFILE, 0, RightX(130), RowTop(1, RH) + (RH - 34) / 2, 130, 34, L"ファイルを選ぶ", 1, on);

        Wallpaper_VideoLabel(d, MAX_PATH + 16);
        RowText(2, RH, L"動画 (mp4)", d, 140, on);
        Button(W_BTN_WALLVIDEO, 0, RightX(130), RowTop(2, RH) + (RH - 34) / 2, 130, 34, L"ファイルを選ぶ", 1, on);
    }
    CardEnd();

    Section(L"動き");
    CardBegin(RH * 2);
    {
        static const wchar_t *const fp[] = {L"15", L"30", L"60"};
        static const wchar_t *const sp[] = {L"ゆっくり", L"ふつう", L"はやい"};
        RowText(0, RH, L"フレームレート (fps)", L"大きいほどなめらかですが、CPU を多く使います", 230, on);
        Segmented(W_SEG_WALLFPS, RightX(210), RowTop(0, RH) + (RH - 34) / 2, 210, fp, 3, Wallpaper_GetFps(), on);
        RowText(1, RH, L"速さ", L"内蔵アニメの動く速さ (GIF・動画は元の速さのまま)", 230, on);
        Segmented(W_SEG_WALLSPD, RightX(210), RowTop(1, RH) + (RH - 34) / 2, 210, sp, 3, Wallpaper_GetSpeed(), on);
    }
    CardEnd();
    Hint(L"フルスクリーンのゲーム・動画・プレゼンが前面にあるときは、自動で一時停止します。オフにすると元の壁紙に戻ります。");
}

/* ------------------------------------------------------------------ */
/* サイドバー / ヘッダー / フッター / 全体                                  */
/* ------------------------------------------------------------------ */
static const wchar_t *const kNavName[PAGE_COUNT] = {L"全般", L"ドック", L"ランチャー", L"検索バー", L"画像・GIF追加", L"時計", L"天気", L"壁紙（試験的）", L"システムモニタ"};
static const wchar_t *const kPageSub[PAGE_COUNT] = {
    L"使う機能の切り替えと、アプリ全体の設定です。",
    L"画面の端に置く、アプリのランチバーの設定です。",
    L"全画面で開くアプリ一覧の設定です。",
    L"デスクトップに置ける検索バーを管理します。",
    L"デスクトップに置く画像・GIF アニメを管理します。",
    L"デスクトップに置く時計ウィジェットの設定です。",
    L"デスクトップに置く天気・気温ウィジェットの設定です。",
    L"デスクトップの壁紙を動かします (内蔵アニメ / 画像・GIF)。",
    L"CPU・メモリ・ディスク・ネットワークの使用状況を表示します。"};

static void NavIcon(int page, float cx, float cy, uint32_t c)
{
    switch (page)
    {
    case PAGE_GENERAL: /* スライダー3本 */
        for (int i = 0; i < 3; i++)
        {
            float y = cy - 6 + i * 6;
            Ln(cx - 8, y, cx + 8, y, 1.7f, c, 1.0f);
            Dot(cx + (i == 1 ? 3.0f : (i == 0 ? -3.0f : 1.0f)), y, 2.6f, c, 1.0f);
        }
        break;
    case PAGE_DOCK: /* 下にバー + 3つの四角 */
        FillRRd(F(0) + (cx - 9) * g_dpi, FY(0) + (cy + 2) * g_dpi, (cx + 9) * g_dpi, FY(0) + (cy + 8) * g_dpi,
                3 * g_dpi, c, 1.0f);
        for (int i = 0; i < 3; i++)
            FillRRd((cx - 7 + i * 5.6f) * g_dpi, FY(0) + (cy - 7) * g_dpi, (cx - 7 + i * 5.6f + 4) * g_dpi,
                    FY(0) + (cy - 3) * g_dpi, 1.2f * g_dpi, c, 1.0f);
        break;
    case PAGE_LAUNCHER: /* 3x3 */
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++)
                FillRRd((cx - 8 + i * 6.0f) * g_dpi, FY(0) + (cy - 8 + j * 6.0f) * g_dpi,
                        (cx - 8 + i * 6.0f + 4) * g_dpi, FY(0) + (cy - 8 + j * 6.0f + 4) * g_dpi, 1.3f * g_dpi, c,
                        1.0f);
        break;
    case PAGE_PHOTO:
        PicGlyph(cx, cy, c);
        break;
    case PAGE_CLOCK:
        ClockGlyph(cx, cy, c, g_pal->side);
        break;
    case PAGE_WEATHER:
        WeatherGlyph(cx, cy, c, g_pal->side);
        break;
    case PAGE_WALLPAPER:
        WallpaperGlyph(cx, cy, c, g_pal->side);
        break;
    case PAGE_SYSMON:
        SysMonGlyph(cx, cy, c, g_pal->side);
        break;
    default: /* 虫眼鏡 */
        Dot(cx - 2, cy - 2, 6.2f, c, 1.0f);
        Dot(cx - 2, cy - 2, 4.4f, g_pal->side, 1.0f);
        Ln(cx + 2.6f, cy + 2.6f, cx + 8, cy + 8, 2.0f, c, 1.0f);
        break;
    }
}

static void DrawSidebar(void)
{
    g_oy = 0;
    g_inContent = FALSE;
    RECT save = g_clip;
    g_clip.left = 0;
    g_clip.top = 0;
    g_clip.right = S(SIDE_W);
    g_clip.bottom = g_ph;
    FillRRd(0, 0, F(SIDE_W), (float)g_ph, 0, g_pal->side, 1.0f);
    RR(SIDE_W - 1, 0, 1, WIN_H, 0, g_pal->border, 0.8f);

    /* 左上のロゴ (青と白のマーク) は表示しない */
    Txt(L"PromDock", 24, 24, SIDE_W - 48, 34, g_fApp, g_pal->text, FL_L);

    for (int i = 0; i < PAGE_COUNT; i++)
    {
        int y = 84 + i * 48, x = 14, w = SIDE_W - 28, h = 40;
        BOOL sel = i == g_page;
        BOOL hv = IsHover(W_NAV, i);
        if (sel)
        {
            RR(x, y, w, h, 10, g_pal->card, 1.0f);
            RRs(x, y, w, h, 10, 1.0f, g_pal->border, 0.9f);
            RR(x + 1, y + 11, 4, 18, 2, g_pal->accent, 1.0f);
        }
        else if (hv)
            RR(x, y, w, h, 10, g_pal->fieldHover, 0.7f);
        NavIcon(i, (float)(x + 30), (float)(y + 20), sel ? g_pal->accent : g_pal->sub);
        Txt(kNavName[i], x + 54, y, w - 60, h, g_fBody, sel ? g_pal->text : g_pal->text, FL_L);
        AddHit(x, y, w, h, W_NAV, i, TRUE, FALSE, 10);
    }
    g_clip = save;
}

static void DrawHeader(void)
{
    g_oy = 0;
    g_inContent = FALSE;
    Txt(kNavName[g_page], CX, 26, CW, 40, g_fHead, g_pal->text, FL_L);
    Txt(kPageSub[g_page], CX + 2, 66, CW, 24, g_fSmall, g_pal->sub, FL_L);
}

static void DrawFooter(void)
{
    g_oy = 0;
    g_inContent = FALSE;
    int y = WIN_H - FOOT_H;
    FillRRd(F(SIDE_W), FY(y), F(WIN_W), F(WIN_H), 0, g_pal->bg, 1.0f);
    RR(SIDE_W, y, WIN_W - SIDE_W, 1, 0, g_pal->border, 0.8f);

    const wchar_t *msg = g_first ? L"「開始」を押すと設定を保存して、PromDock を起動します。"
                                 : L"変更はすぐに反映・保存されます。";
    uint32_t col = g_pal->sub;
    if (g_notice[0])
    {
        msg = g_notice;
        col = g_noticeWarn ? g_pal->danger : g_pal->accent;
    }
    Txt(msg, CX, y, 370, FOOT_H, g_fSmall, col, FL_L);

    if (g_first)
    {
        Button(W_BTN_EXIT, 0, WIN_W - 36 - 112 - 12 - 88, y + (FOOT_H - 38) / 2, 88, 38, L"終了", 0, TRUE);
        Button(W_BTN_OK, 0, WIN_W - 36 - 112, y + (FOOT_H - 38) / 2, 112, 38, L"開始", 1, TRUE);
    }
    else
        Button(W_BTN_OK, 0, WIN_W - 36 - 112, y + (FOOT_H - 38) / 2, 112, 38, L"閉じる", 1, TRUE);
}

static void DrawScrollbar(int vh)
{
    int mx = g_contentH - vh;
    if (mx <= 0)
        return;
    g_oy = 0;
    float trackT = (float)HEAD_H, trackH = (float)vh;
    float th = fmaxf(36.0f, trackH * vh / (float)g_contentH);
    float ty = trackT + (trackH - th) * g_scroll[g_page] / (float)mx;
    FillRRd(F(WIN_W - 12), ty * g_dpi, F(WIN_W - 8), (ty + th) * g_dpi, 2 * g_dpi, g_pal->sub, 0.45f);
}

static void DrawAll(void)
{
    g_nhit = 0;
    /* 背景 */
    g_clip.left = 0;
    g_clip.top = 0;
    g_clip.right = g_pw;
    g_clip.bottom = g_ph;
    uint32_t bg = g_pal->bg;
    for (int i = 0; i < g_pw * g_ph; i++)
        g_px[i] = bg;

    int vh = WIN_H - FOOT_H - HEAD_H;
    int maxScroll = g_contentH - vh;
    if (maxScroll < 0)
        maxScroll = 0;
    g_scroll[g_page] = ClampInt(g_scroll[g_page], 0, maxScroll);

    /* コンテンツ (スクロール領域) */
    g_clip.left = S(SIDE_W);
    g_clip.top = S(HEAD_H);
    g_clip.right = g_pw;
    g_clip.bottom = S(WIN_H - FOOT_H);
    g_oy = HEAD_H - g_scroll[g_page];
    g_inContent = TRUE;
    g_y = 4;
    switch (g_page)
    {
    case PAGE_GENERAL:
        PageGeneral();
        break;
    case PAGE_DOCK:
        PageDock();
        break;
    case PAGE_LAUNCHER:
        PageLauncher();
        break;
    case PAGE_PHOTO:
        PagePhoto();
        break;
    case PAGE_CLOCK:
        PageClock();
        break;
    case PAGE_WEATHER:
        PageWeather();
        break;
    case PAGE_WALLPAPER:
        PageWallpaper();
        break;
    case PAGE_SYSMON:
        PageSysMon();
        break;
    default:
        PageSearch();
        break;
    }
    g_contentH = g_y + 8;
    g_inContent = FALSE;

    /* 固定部分 */
    g_clip.left = 0;
    g_clip.top = 0;
    g_clip.right = g_pw;
    g_clip.bottom = g_ph;
    DrawHeader();
    DrawFooter();
    DrawScrollbar(vh);
    DrawSidebar();
}

/* ------------------------------------------------------------------ */
/* 操作                                                                  */
/* ------------------------------------------------------------------ */
static void Redraw(void)
{
    if (g_set)
        InvalidateRect(g_set, NULL, FALSE);
}

static void SetPage(int p)
{
    g_page = ClampInt(p, 0, PAGE_COUNT - 1);
    g_focusId = W_NONE;
    Redraw();
}

/* ドック/ランチャーの有効・無効 (両方無効でもよい。その場合もタスクトレイから設定を開ける) */
static void ToggleFeature(int which)
{
    BOOL d = g_dockOn, l = g_lnOn;
    if (which == 0)
        d = !d;
    else
        l = !l;
    g_dockOn = d;
    g_lnOn = l;
    if (!g_first)
    {
        SaveSettings();
        if (which == 0)
            Dock_SetEnabled(g_dockOn);
        else
            Launcher_SetEnabled(g_lnOn);
    }
    Redraw();
}

static void SetSlider(int id, int value, BOOL commit)
{
    int mn, mx, st;
    SliderRange(id, &mn, &mx, &st);
    value = mn + ((value - mn + st / 2) / st) * st;
    value = ClampInt(value, mn, mx);
    switch (id)
    {
    case W_SL_HIDEMS:
        Dock_SetHideMs(value);
        break;
    case W_SL_ICON:
        Dock_SetIconSize(value);
        break;
    case W_SL_ZOOM:
        Dock_SetZoom(value);
        break;
    case W_SL_OPACITY:
        Dock_SetOpacity(value);
        break;
    case W_SL_PFSIZE:
        g_pfNewSize = value;
        break;
    case W_SL_CLSIZE:
        Clock_SetSize(value);
        break;
    case W_SL_CLOPA:
        Clock_SetOpacity(value);
        break;
    case W_SL_WSIZE:
        Weather_SetSize(value);
        break;
    case W_SL_WOPA:
        Weather_SetOpacity(value);
        break;
    case W_SL_SMSIZE:
        SysMon_SetSize(value);
        break;
    case W_SL_SMOPA:
        SysMon_SetOpacity(value);
        break;
    }
    if (commit && id <= W_SL_OPACITY) /* ドックのスライダーだけ、ドックの設定を保存する */
        Dock_SaveSettings();
    Redraw();
}

static int SliderGet(int id)
{
    switch (id)
    {
    case W_SL_HIDEMS:
        return Dock_GetHideMs();
    case W_SL_ICON:
        return Dock_GetIconSize();
    case W_SL_ZOOM:
        return Dock_GetZoom();
    case W_SL_PFSIZE:
        return g_pfNewSize;
    case W_SL_CLSIZE:
        return Clock_GetSize();
    case W_SL_CLOPA:
        return Clock_GetOpacity();
    case W_SL_WSIZE:
        return Weather_GetSize();
    case W_SL_WOPA:
        return Weather_GetOpacity();
    case W_SL_SMSIZE:
        return SysMon_GetSize();
    case W_SL_SMOPA:
        return SysMon_GetOpacity();
    default:
        return Dock_GetOpacity();
    }
}

static void SliderFromX(const Hit *h, int px, BOOL commit)
{
    int mn, mx, st;
    SliderRange(h->id, &mn, &mx, &st);
    float t = h->x1 > h->x0 ? Clampf((float)(px - h->x0) / (float)(h->x1 - h->x0), 0, 1) : 0;
    SetSlider(h->id, mn + (int)(t * (mx - mn) + 0.5f), commit);
}

/* ランチャーのショートカットキーに選べるキー (単独で押せる、ふだん使わないキー) */
static const int kHotkeys[] = {VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7,
                               VK_F8, VK_F9, VK_F10, VK_F11, VK_F12, VK_PAUSE, VK_SCROLL};
#define N_HOTKEYS ((int)(sizeof(kHotkeys) / sizeof(kHotkeys[0])))

static void ShowDropdown(HWND hwnd, const Hit *h)
{
    HMENU m = CreatePopupMenu();
    int n = 0;
    if (h->id == W_DD_HOTKEY)
    {
        n = N_HOTKEYS;
        for (int i = 0; i < n; i++)
        {
            wchar_t t[32];
            VkName(kHotkeys[i], t, 32);
            AppendMenuW(m, MF_STRING | (kHotkeys[i] == Launcher_GetHotkey() ? MF_CHECKED : 0), (UINT_PTR)(i + 1), t);
        }
    }
    else if (h->id == W_DD_BROWSER)
    {
        n = SearchBar_BrowserCount();
        for (int i = 0; i < n; i++)
        {
            wchar_t t[96];
            SearchBar_BrowserLabel(i, t, 96);
            AppendMenuW(m, MF_STRING | (i == g_sbBrowser ? MF_CHECKED : 0), (UINT_PTR)(i + 1), t);
        }
    }
    else if (h->id == W_DD_WALLKIND)
    {
        n = Wallpaper_KindCount();
        for (int i = 0; i < n; i++)
            AppendMenuW(m, MF_STRING | (i == Wallpaper_GetKind() ? MF_CHECKED : 0), (UINT_PTR)(i + 1), Wallpaper_KindName(i));
    }
    else if (h->id == W_DD_SMVAR)
    {
        n = SysMon_VariantCount();
        for (int i = 0; i < n; i++)
            AppendMenuW(m, MF_STRING | (i == SysMon_GetVariant() ? MF_CHECKED : 0), (UINT_PTR)(i + 1), SysMon_VariantName(i));
    }
    else if (h->id == W_DD_WVAR)
    {
        n = Weather_VariantCount();
        for (int i = 0; i < n; i++)
            AppendMenuW(m, MF_STRING | (i == Weather_GetVariant() ? MF_CHECKED : 0), (UINT_PTR)(i + 1), Weather_VariantName(i));
    }
    else if (h->id == W_DD_WCITY)
    {
        n = Weather_CityCount();
        for (int i = 0; i < n; i++)
        {
            if (i == n - 1)
                AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            UINT fl = MF_STRING | (i == Weather_GetCity() ? MF_CHECKED : 0) | ((i > 0 && i < n - 1 && i % 15 == 0) ? MF_MENUBARBREAK : 0);
            AppendMenuW(m, fl, (UINT_PTR)(i + 1), Weather_CityName(i));
        }
    }
    else
    {
        n = SearchBar_EngineCount();
        for (int i = 0; i < n; i++)
            AppendMenuW(m, MF_STRING | (i == g_sbEngine ? MF_CHECKED : 0), (UINT_PTR)(i + 1), SearchBar_EngineName(i));
    }
    POINT pt = {h->r.left, h->r.bottom + S(4)};
    ClientToScreen(hwnd, &pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (cmd <= 0)
        return;
    int i = cmd - 1;
    if (h->id == W_DD_HOTKEY)
    {
        if (i >= 0 && i < N_HOTKEYS)
        {
            if (!Launcher_SetHotkey(kHotkeys[i]))
                Notice(L"そのキーは他のアプリが使用中のため、変更できませんでした。", TRUE);
        }
    }
    else if (h->id == W_DD_BROWSER)
        g_sbBrowser = i;
    else if (h->id == W_DD_WALLKIND)
    {
        Wallpaper_SetKind(i);
        if (Wallpaper_KindIsFile(i) && !Wallpaper_HasFile())
            Wallpaper_PickFile(hwnd);
        else if (Wallpaper_KindIsVideo(i) && !Wallpaper_HasVideo())
            Wallpaper_PickVideo(hwnd);
    }
    else if (h->id == W_DD_SMVAR)
        SysMon_SetVariant(i);
    else if (h->id == W_DD_WVAR)
        Weather_SetVariant(i);
    else if (h->id == W_DD_WCITY)
    {
        if (i == Weather_CityCount() - 1)
        {
            if (Weather_PickCoords(hwnd))
                Notice(L"指定した緯度・経度の天気を表示します。", FALSE);
        }
        else
            Weather_SetCity(i);
    }
    else
        g_sbEngine = i;
    Redraw();
}

static void Activate(HWND hwnd, const Hit *h)
{
    if (!h || !h->en)
        return;
    switch (h->id)
    {
    case W_NAV:
    case W_GOTO:
        SetPage(h->arg);
        break;
    case W_TG_DOCK:
        ToggleFeature(0);
        break;
    case W_TG_LN:
        ToggleFeature(1);
        break;
    case W_TG_AUTOSTART:
        g_autoStart = !g_autoStart;
        if (!g_first)
            AutoStartSet(g_autoStart);
        Redraw();
        break;
    case W_TG_AUTOHIDE:
        Dock_SetAutoHide(!Dock_GetAutoHide());
        Dock_SaveSettings();
        Redraw();
        break;
    case W_TG_NOTICE:
        Notice_SetEnabled(!Notice_Enabled());
        Redraw();
        break;
    case W_SEG_THEME:
        g_theme = h->arg;
        if (!g_first)
            SaveSettings();
        ApplyTheme();
        break;
    case W_SEG_EDGE:
        Dock_SetEdge(h->arg);
        Dock_SaveSettings();
        Redraw();
        break;
    case W_SEG_DKSTYLE:
        Dock_SetStyle(h->arg);
        Dock_SaveSettings();
        Redraw();
        break;
    case W_SEG_DKSEL:
        Dock_SetEdit(h->arg);
        Redraw();
        break;
    case W_BTN_DKADD:
        if (Dock_Add())
        {
            Dock_SaveSettings();
            Notice(L"ドックを追加しました。画面の端に空のドックが表示されます。", FALSE);
        }
        else
            Notice(L"ドックは3つまでです。", FALSE);
        Redraw();
        break;
    case W_BTN_DKDEL:
        if (Dock_Count() > 1 && Dock_RemoveAt(Dock_GetEdit()))
        {
            Dock_SaveSettings();
            Notice(L"選んだドックを削除しました。", FALSE);
        }
        Redraw();
        break;
    case W_SEG_DKZ:
        Dock_SetZOrder(h->arg);
        Dock_SaveSettings();
        Redraw();
        break;
    case W_SEG_DKTYPE:
        Dock_SetType(h->arg);
        Dock_SaveSettings();
        Redraw();
        break;
    case W_SEG_LNSTYLE:
        g_lnStyle = h->arg;
        if (!g_first)
        {
            SaveSettings();
            Launcher_SetStyle(g_lnStyle, g_lnEdge);
        }
        Redraw();
        break;
    case W_SEG_LNEDGE:
        g_lnEdge = h->arg;
        if (!g_first)
        {
            SaveSettings();
            Launcher_SetStyle(g_lnStyle, g_lnEdge);
        }
        Redraw();
        break;
    case W_STP_COLS:
    case W_STP_ROWS:
    {
        int c = Launcher_GetCols(), r = Launcher_GetRows();
        int d = h->arg == 0 ? -1 : 1;
        if (h->id == W_STP_COLS)
            c = ClampInt(c + d, 0, 20);
        else
            r = ClampInt(r + d, 0, 20);
        Launcher_SetGrid(c, r);
        Redraw();
        break;
    }
    case W_DD_HOTKEY:
    case W_DD_BROWSER:
    case W_DD_ENGINE:
    case W_DD_WCITY:
    case W_DD_WVAR:
    case W_DD_SMVAR:
    case W_DD_WALLKIND:
        ShowDropdown(hwnd, h);
        break;
    case W_BTN_RESETPOS:
        Dock_ResetPosition();
        Dock_SaveSettings();
        Notice(L"ドックを端の中央に戻しました。", FALSE);
        break;
    case W_BTN_OPENDIR:
    {
        wchar_t dir[MAX_PATH];
        wcscpy(dir, g_ini);
        wchar_t *sl = wcsrchr(dir, L'\\');
        if (sl)
            *sl = 0;
        ShellExecuteW(NULL, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case W_BTN_QUIT:
        DestroyWindow(hwnd);
        Dock_Quit();
        break;
    case W_BTN_ADD:
        if (!SearchBar_Add(g_sbBrowser, g_sbEngine))
            Notice(L"追加できる検索バーは最大 8 個までです。", TRUE);
        else
            Notice(L"検索バーを追加しました。デスクトップに表示されています。", FALSE);
        Redraw();
        break;
    case W_BTN_DEL:
        SearchBar_Remove(h->arg); /* 内部で表示も更新される */
        Redraw();
        break;
    case W_BTN_MOVE:
        SearchBar_SetMoveMode(!SearchBar_MoveMode());
        Redraw();
        break;
    case W_TG_PFMOVE:
        PhotoFrame_SetMovable(!PhotoFrame_Movable());
        Redraw();
        break;
    case W_STP_PFSIZE:
    {
        int i = h->arg / 2, d = (h->arg % 2) ? 1 : -1;
        PhotoFrame_SetSize(i, ClampInt(PhotoFrame_GetSize(i) + d * 50, 100, 1200));
        Redraw();
        break;
    }
    case W_BTN_PFADD:
    {
        wchar_t path[MAX_PATH];
        if (PhotoFrame_PickFile(hwnd, path, MAX_PATH))
        {
            if (PhotoFrame_Add(path, g_pfNewSize))
                Notice(L"デスクトップに追加しました。ドラッグで好きな場所へ動かせます。", FALSE);
            else
                Notice(L"追加できませんでした。対応していない形式か、読み込めないファイルです。", TRUE);
        }
        Redraw();
        break;
    }
    case W_BTN_PFDEL:
        PhotoFrame_Remove(h->arg); /* 内部で表示も更新される */
        Redraw();
        break;
    case W_TG_CLOCK:
        Clock_SetEnabled(!Clock_Enabled());
        Redraw();
        break;
    case W_SEG_CLSTYLE:
        Clock_SetStyle(h->arg);
        Redraw();
        break;
    case W_SEG_CLVAR:
        Clock_SetVariant(h->arg);
        Redraw();
        break;
    case W_TG_CL24:
        Clock_Set24h(!Clock_Get24h());
        Redraw();
        break;
    case W_TG_CLSEC:
        Clock_SetSeconds(!Clock_GetSeconds());
        Redraw();
        break;
    case W_TG_CLDATE:
        Clock_SetDate(!Clock_GetDate());
        Redraw();
        break;
    case W_TG_CLMOVE:
        Clock_SetMovable(!Clock_Movable());
        Redraw();
        break;
    case W_BTN_CLRESET:
        Clock_ResetPosition();
        Notice(L"時計を画面の右上に戻しました。", FALSE);
        break;
    case W_TG_SMON:
        SysMon_SetEnabled(!SysMon_Enabled());
        Redraw();
        break;
    case W_SEG_SMINT:
        SysMon_SetInterval(h->arg);
        Redraw();
        break;
    case W_TG_SMCPU:
    case W_TG_SMMEM:
    case W_TG_SMDISK:
    case W_TG_SMNET:
    {
        int idx = h->id == W_TG_SMCPU ? 0 : (h->id == W_TG_SMMEM ? 1 : (h->id == W_TG_SMDISK ? 2 : 3));
        if (SysMon_GetItem(idx))
        {
            int n = 0;
            for (int k = 0; k < 4; k++)
                n += SysMon_GetItem(k) ? 1 : 0;
            if (n <= 1)
            {
                Notice(L"項目は最低1つ表示する必要があります。", TRUE);
                break;
            }
        }
        SysMon_SetItem(idx, !SysMon_GetItem(idx));
        Redraw();
        break;
    }
    case W_TG_SMMOVE:
        SysMon_SetMovable(!SysMon_Movable());
        Redraw();
        break;
    case W_BTN_SMRESET:
        SysMon_ResetPosition();
        Notice(L"システムモニタを初期位置に戻しました。", FALSE);
        break;
    case W_TG_WEATHER:
        Weather_SetEnabled(!Weather_Enabled());
        Redraw();
        break;
    case W_TG_WALL:
        Wallpaper_SetEnabled(!Wallpaper_Enabled());
        Redraw();
        break;
    case W_SEG_WALLFPS:
        Wallpaper_SetFps(h->arg);
        Redraw();
        break;
    case W_SEG_WALLSPD:
        Wallpaper_SetSpeed(h->arg);
        Redraw();
        break;
    case W_BTN_WALLFILE:
        if (Wallpaper_PickFile(hwnd))
            Notice(L"壁紙に設定しました。", FALSE);
        Redraw();
        break;
    case W_BTN_WALLVIDEO:
        if (Wallpaper_PickVideo(hwnd))
            Notice(L"動画を壁紙に設定しました。読み込めない形式のときは元の壁紙のままです。", FALSE);
        Redraw();
        break;
    case W_SEG_WVAR:
        Weather_SetVariant(h->arg);
        Redraw();
        break;
    case W_SEG_WUNIT:
        Weather_SetUnit(h->arg);
        Redraw();
        break;
    case W_BTN_WCOORD:
        if (Weather_PickCoords(hwnd))
            Notice(L"指定した緯度・経度の天気を表示します。", FALSE);
        Redraw();
        break;
    case W_BTN_WREFRESH:
        Weather_Refresh();
        Notice(L"天気を更新しています…", FALSE);
        break;
    case W_TG_WMOVE:
        Weather_SetMovable(!Weather_Movable());
        Redraw();
        break;
    case W_BTN_WRESET:
        Weather_ResetPosition();
        Notice(L"天気を画面の左上に戻しました。", FALSE);
        break;
    case W_BTN_OK:
        if (g_first)
        {
            g_accepted = TRUE;
            if (g_autoStart)
                AutoStartSet(TRUE);
            ApplyAll();
        }
        DestroyWindow(hwnd);
        break;
    case W_BTN_EXIT:
        DestroyWindow(hwnd);
        break;
    }
}

/* Tab / Shift+Tab でフォーカスを移動 */
static void FocusMove(int dir)
{
    if (g_nhit <= 0)
        return;
    int cur = -1;
    for (int i = 0; i < g_nhit; i++)
        if (g_hit[i].id == g_focusId && g_hit[i].arg == g_focusArg)
            cur = i;
    for (int k = 1; k <= g_nhit; k++)
    {
        int i = ((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir * k + g_nhit * 2) % g_nhit;
        if (g_hit[i].en)
        {
            g_focusId = g_hit[i].id;
            g_focusArg = g_hit[i].arg;
            /* スクロール領域内の部品は見える位置までスクロール */
            if (g_hit[i].content)
            {
                int top = (int)(g_hit[i].r.top / g_dpi), bot = (int)(g_hit[i].r.bottom / g_dpi);
                int vt = HEAD_H, vb = WIN_H - FOOT_H;
                if (top < vt + 4)
                    g_scroll[g_page] -= (vt + 4 - top);
                else if (bot > vb - 4)
                    g_scroll[g_page] += (bot - (vb - 4));
                if (g_scroll[g_page] < 0)
                    g_scroll[g_page] = 0;
            }
            break;
        }
    }
    g_kbd = TRUE;
    Redraw();
}

static Hit *FocusedHit(void)
{
    for (int i = 0; i < g_nhit; i++)
        if (g_hit[i].id == g_focusId && g_hit[i].arg == g_focusArg)
            return &g_hit[i];
    return NULL;
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_set = hwnd; /* ApplyTheme() でタイトルバーの色を変えるため、ここで先に入れておく */
        RECT rc;
        GetClientRect(hwnd, &rc);
        g_pw = rc.right;
        g_ph = rc.bottom;
        BITMAPINFO bi;
        memset(&bi, 0, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = g_pw;
        bi.bmiHeader.biHeight = -g_ph;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC sdc = GetDC(hwnd);
        g_mem = CreateCompatibleDC(sdc);
        g_dib = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, (void **)&g_px, NULL, 0);
        ReleaseDC(hwnd, sdc);
        if (!g_dib || !g_px)
        {
            g_set = NULL;
            return -1;
        }
        g_oldBmp = (HBITMAP)SelectObject(g_mem, g_dib);
        g_autoStart = g_first ? FALSE : AutoStartGet();
        g_hoverId = g_pressId = g_focusId = W_NONE;
        g_kbd = FALSE;
        ApplyTheme();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (g_mem && g_px)
        {
            DrawAll();
            BitBlt(dc, 0, 0, g_pw, g_ph, g_mem, 0, 0, SRCCOPY);
            BOOL anim = FALSE;
            for (int i = 0; i < W_COUNT; i++)
                if (g_ainit[i] && fabsf(g_at[i] - g_av[i]) > 0.003f)
                    anim = TRUE;
            if (anim)
                SetTimer(hwnd, TIMER_ANIM, 16, NULL);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_ANIM)
        {
            BOOL any = FALSE;
            for (int i = 0; i < W_COUNT; i++)
            {
                float d = g_at[i] - g_av[i];
                if (fabsf(d) > 0.003f)
                {
                    g_av[i] += d * 0.42f;
                    any = TRUE;
                }
                else
                    g_av[i] = g_at[i];
            }
            if (!any)
                KillTimer(hwnd, TIMER_ANIM);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (wp == TIMER_NOTICE)
        {
            if (GetTickCount() - g_noticeTick > 4500)
            {
                g_notice[0] = 0;
                KillTimer(hwnd, TIMER_NOTICE);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_MOUSEMOVE:
    {
        if (!g_tracking)
        {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_tracking = TRUE;
        }
        int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
        if (g_dragSlider)
        {
            for (int i = 0; i < g_nhit; i++)
                if (g_hit[i].id == g_dragId)
                {
                    SliderFromX(&g_hit[i], px, FALSE);
                    break;
                }
            return 0;
        }
        Hit *h = HitAt(px, py);
        int id = h && h->en ? h->id : W_NONE, arg = h && h->en ? h->arg : 0;
        if (id != g_hoverId || arg != g_hoverArg)
        {
            g_hoverId = id;
            g_hoverArg = arg;
            Redraw();
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_tracking = FALSE;
        if (g_hoverId != W_NONE)
        {
            g_hoverId = W_NONE;
            Redraw();
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hoverId != W_NONE)
        {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
    {
        SetFocus(hwnd);
        g_kbd = FALSE;
        Hit *h = HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h && h->en)
        {
            g_pressId = h->id;
            g_pressArg = h->arg;
            g_focusId = h->id;
            g_focusArg = h->arg;
            SetCapture(hwnd);
            if (h->id >= W_SL_HIDEMS && h->id <= W_SL_SMOPA)
            {
                g_dragSlider = TRUE;
                g_dragId = h->id;
                SliderFromX(h, GET_X_LPARAM(lp), FALSE);
            }
        }
        else
            g_pressId = W_NONE;
        Redraw();
        return 0;
    }

    case WM_LBUTTONUP:
    {
        int px = GET_X_LPARAM(lp), py = GET_Y_LPARAM(lp);
        if (g_dragSlider)
        {
            g_dragSlider = FALSE;
            if (GetCapture() == hwnd)
                ReleaseCapture();
            SetSlider(g_dragId, SliderGet(g_dragId), TRUE); /* 確定して保存 */
            g_pressId = W_NONE;
            return 0;
        }
        if (GetCapture() == hwnd)
            ReleaseCapture();
        Hit *h = HitAt(px, py);
        int id = g_pressId, arg = g_pressArg;
        g_pressId = W_NONE;
        if (h && h->en && h->id == id && h->arg == arg)
        {
            Hit copy = *h; /* Activate 中に g_hit が組み替わっても安全なようにコピー */
            Activate(hwnd, &copy);
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_dragSlider)
        {
            g_dragSlider = FALSE;
            Dock_SaveSettings();
        }
        return 0;

    case WM_MOUSEWHEEL:
    {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        g_scroll[g_page] -= d * 64 / 120;
        int mx = g_contentH - (WIN_H - FOOT_H - HEAD_H);
        g_scroll[g_page] = ClampInt(g_scroll[g_page], 0, mx < 0 ? 0 : mx);
        Redraw();
        return 0;
    }

    case WM_KEYDOWN:
    {
        switch (wp)
        {
        case VK_TAB:
            FocusMove((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        case VK_SPACE:
        case VK_RETURN:
        {
            Hit *h = g_kbd ? FocusedHit() : NULL;
            if (h)
            {
                Hit copy = *h;
                Activate(hwnd, &copy);
            }
            else if (wp == VK_RETURN)
            {
                Hit ok = {{0, 0, 0, 0}, W_BTN_OK, 0, TRUE, FALSE, 0, 0};
                Activate(hwnd, &ok);
            }
            return 0;
        }
        case VK_LEFT:
        case VK_RIGHT:
        {
            Hit *h = g_kbd ? FocusedHit() : NULL;
            if (!h || !h->en)
                return 0;
            int d = wp == VK_LEFT ? -1 : 1;
            if (h->id >= W_SL_HIDEMS && h->id <= W_SL_SMOPA)
            {
                int mn, mx, st;
                SliderRange(h->id, &mn, &mx, &st);
                SetSlider(h->id, SliderGet(h->id) + d * st, TRUE);
            }
            else if (h->id == W_SEG_THEME || h->id == W_SEG_EDGE || h->id == W_SEG_LNSTYLE || h->id == W_SEG_LNEDGE ||
                     h->id == W_SEG_CLSTYLE || h->id == W_SEG_CLVAR || h->id == W_SEG_DKSTYLE || h->id == W_SEG_DKTYPE || h->id == W_SEG_DKZ || h->id == W_SEG_DKSEL || h->id == W_SEG_WVAR || h->id == W_SEG_WUNIT || h->id == W_SEG_SMINT)
            {
                int n = h->id == W_SEG_DKSEL ? Dock_Count() : h->id == W_SEG_DKZ                           ? 2
                                                          : h->id == W_SEG_DKTYPE                          ? Dock_TypeCount()
                                                          : h->id == W_SEG_DKSTYLE                         ? Dock_StyleCount()
                                                          : (h->id == W_SEG_THEME || h->id == W_SEG_SMINT) ? 3
                                                                                                           : ((h->id == W_SEG_LNSTYLE || h->id == W_SEG_CLSTYLE || h->id == W_SEG_WUNIT) ? 2 : (h->id == W_SEG_WVAR ? Weather_VariantCount() : (h->id == W_SEG_CLVAR ? Clock_VariantCount(Clock_GetStyle()) : 4)));
                int a = ClampInt(h->arg + d, 0, n - 1);
                Hit copy = *h;
                copy.arg = a;
                g_focusArg = a;
                Activate(hwnd, &copy);
            }
            return 0;
        }
        case VK_ESCAPE:
            if (g_first)
                return 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_SETTINGCHANGE:
        if (g_theme == 0)
            ApplyTheme();
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ANIM);
        KillTimer(hwnd, TIMER_NOTICE);
        if (g_mem && g_oldBmp)
            SelectObject(g_mem, g_oldBmp);
        if (g_dib)
            DeleteObject(g_dib);
        if (g_mem)
            DeleteDC(g_mem);
        g_dib = NULL;
        g_mem = NULL;
        g_px = NULL;
        g_set = NULL;
        g_dragSlider = FALSE;
        g_notice[0] = 0;
        memset(g_ainit, 0, sizeof(g_ainit));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenSettingsWindow(int page)
{
    if (page >= 0)
        g_page = ClampInt(page, 0, PAGE_COUNT - 1);
    if (g_set)
    {
        ShowWindow(g_set, SW_SHOWNORMAL);
        SetForegroundWindow(g_set);
        Redraw();
        return;
    }
    static BOOL reg = FALSE;
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.hIcon = g_icon;
        wc.hIconSm = g_iconSm;
        wc.lpszClassName = L"PromDockSettingsWnd";
        RegisterClassExW(&wc);
        reg = TRUE;
    }
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD ex = WS_EX_TOPMOST;
    RECT rc = {0, 0, S(WIN_W), S(WIN_H)};
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - w) / 2, y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    if (y < wa.top)
        y = wa.top;
    g_set = CreateWindowExW(ex, L"PromDockSettingsWnd", L"PromDock 設定", style, x, y, w, h, NULL, NULL, g_hInst, NULL);
    if (!g_set)
        return;
    SendMessageW(g_set, WM_SETICON, ICON_SMALL, (LPARAM)g_iconSm);
    SendMessageW(g_set, WM_SETICON, ICON_BIG, (LPARAM)g_icon);
    ShowWindow(g_set, SW_SHOWNORMAL);
    UpdateWindow(g_set);
    SetForegroundWindow(g_set);
}

void Settings_Open(void) { OpenSettingsWindow(-1); }
void Settings_OpenPage(int page) { OpenSettingsWindow(page); }

/* キー操作は SettingsProc が直接処理するので、ここでは何もしない (従来の呼び出し口を残してある) */
BOOL Settings_PreTranslate(MSG *m)
{
    (void)m;
    return FALSE;
}

void Settings_RefreshSearchBar(void) { Redraw(); }
void Settings_RefreshPhotoFrame(void) { Redraw(); }
void Settings_RefreshClock(void) { Redraw(); }
void Settings_RefreshWeather(void) { Redraw(); }
void Settings_RefreshSysMon(void) { Redraw(); }

/* ------------------------------------------------------------------ */
/* タスクトレイ                                                          */
/* ------------------------------------------------------------------ */
static void AddTray(void)
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_host;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_iconSm ? g_iconSm : LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(nid.szTip, L"PromDock");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid);
}

static void DelTray(void)
{
    if (!g_trayAdded)
        return;
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_host;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = FALSE;
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"アプリ設定…");
    AppendMenuW(menu, MF_STRING | (g_lnOn ? 0 : MF_GRAYED), 2, L"ランチャーを開く / 閉じる");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 3, L"PromDock を終了");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (cmd == 1)
        OpenSettingsWindow(-1);
    else if (cmd == 2)
        Launcher_Toggle();
    else if (cmd == 3)
        Dock_Quit();
}

static LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msgTaskbar && msg == g_msgTaskbar)
    {
        g_trayAdded = FALSE;
        AddTray(); /* エクスプローラー再起動後にアイコンを戻す */
        return 0;
    }
    switch (msg)
    {
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK)
            OpenSettingsWindow(-1);
        else if (LOWORD(lp) == WM_RBUTTONUP)
            ShowTrayMenu(hwnd);
        return 0;
    case WM_SHOWSETTINGS: /* 2つ目のインスタンスから */
        OpenSettingsWindow(-1);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
static HFONT MakeFont(const LOGFONTW *base, int px, int weight)
{
    LOGFONTW lf = *base;
    lf.lfHeight = -S(px);
    lf.lfWidth = 0;
    lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&lf);
}

BOOL Settings_Init(HINSTANCE hInst)
{
    g_hInst = hInst;

    /* 二重起動: 動作中のアプリの設定ウィンドウを開いて終了 */
    g_mutex = CreateMutexW(NULL, FALSE, L"PromDockSingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND other = FindWindowW(L"PromDockHostWnd", NULL);
        if (other)
        {
            AllowSetForegroundWindow(ASFW_ANY);
            PostMessageW(other, WM_SHOWSETTINGS, 0, 0);
        }
        CloseHandle(g_mutex);
        g_mutex = NULL;
        return FALSE;
    }

    HDC scr = GetDC(NULL);
    g_dpi = GetDeviceCaps(scr, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, scr);

    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    wchar_t *slash = wcsrchr(g_ini, L'\\');
    if (slash)
        wcscpy(slash + 1, L"settings.ini");
    else
        wcscpy(g_ini, L"settings.ini");

    /* 作業領域が小さい画面 (低解像度・高DPI) でも設定ウィンドウが収まるように倍率を下げる */
    {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        float maxH = (float)(wa.bottom - wa.top - GetSystemMetrics(SM_CYCAPTION) - 2 * GetSystemMetrics(SM_CYFRAME) - 8);
        float maxW = (float)(wa.right - wa.left - 2 * GetSystemMetrics(SM_CXFRAME) - 8);
        float lim = fminf(maxW / WIN_W, maxH / WIN_H);
        if (lim > 0.5f && g_dpi > lim)
            g_dpi = lim;
    }

    g_icon = LoadAppIco(GetSystemMetrics(SM_CXICON));
    g_iconSm = LoadAppIco(GetSystemMetrics(SM_CXSMICON));
    if (!g_icon) /* promdock.ico が埋め込まれていないときは内蔵アイコン */
        g_icon = MakeAppIcon();
    if (!g_iconSm)
        g_iconSm = g_icon;
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_fBody = MakeFont(&ncm.lfMessageFont, 15, FW_NORMAL);
    g_fSmall = MakeFont(&ncm.lfMessageFont, 12, FW_NORMAL);
    g_fBtn = MakeFont(&ncm.lfMessageFont, 14, FW_NORMAL);
    g_fSec = MakeFont(&ncm.lfMessageFont, 15, FW_SEMIBOLD);
    g_fApp = MakeFont(&ncm.lfMessageFont, 18, FW_BOLD);
    g_fHead = MakeFont(&ncm.lfMessageFont, 28, FW_BOLD);

    WNDCLASSEXW hc;
    memset(&hc, 0, sizeof(hc));
    hc.cbSize = sizeof(hc);
    hc.lpfnWndProc = HostProc;
    hc.hInstance = hInst;
    hc.lpszClassName = L"PromDockHostWnd";
    RegisterClassExW(&hc);
    g_host = CreateWindowExW(0, hc.lpszClassName, L"PromDockHost", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");

    g_first = GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES;
    if (!g_first)
        LoadSettings();
    g_sbBrowser = g_sbEngine = 0;

    if (g_first)
    {
        /* 初回: 設定ウィンドウを先に出して、閉じるまで待つ */
        OpenSettingsWindow(PAGE_GENERAL);
        MSG m;
        while (g_set && GetMessageW(&m, NULL, 0, 0) > 0)
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!g_accepted)
        { /* 「終了」/ ×で閉じた -> 何も保存せず終了 (次回また初回設定が出る) */
            Settings_Shutdown();
            return FALSE;
        }
        g_first = FALSE;
    }

    AddTray();
    Wallpaper_Init(hInst); /* 動く壁紙 (有効なら開始) */
    return TRUE;
}

void Settings_Shutdown(void)
{
    Wallpaper_Shutdown();
    DelTray();
    if (g_set)
        DestroyWindow(g_set);
    if (g_host)
        DestroyWindow(g_host);
    g_set = g_host = NULL;
    if (g_iconSm && g_iconSm != g_icon)
        DestroyIcon(g_iconSm);
    if (g_icon)
        DestroyIcon(g_icon);
    g_iconSm = NULL;
    HFONT *fs[] = {&g_fBody, &g_fSmall, &g_fSec, &g_fHead, &g_fApp, &g_fBtn};
    for (int i = 0; i < 6; i++)
        if (*fs[i])
        {
            DeleteObject(*fs[i]);
            *fs[i] = NULL;
        }
    g_icon = NULL;
    if (g_mutex)
    {
        CloseHandle(g_mutex);
        g_mutex = NULL;
    }
}