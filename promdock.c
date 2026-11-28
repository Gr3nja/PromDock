/*
 * promdock.c - PromDock: NeXuS Dock風フローティングドック (Win32 / C / MSVC)
 *
 * ビルド:
 *     build.bat をダブルクリック (または実行) するだけです。
 *     Visual Studio / Build Tools を自動検出し、build\PromDock.exe を生成します。
 *     ※ ソースはUTF-8です。build.bat は /utf-8 を付けてコンパイルします。
 *
 * 機能:
 *   - Macのような薄いダークグレー半透明のドック (枠線・区切り線なし)
 *   - ホバー: 触ったアイコンとその周辺が滑らかに少し拡大 / 起動バウンス / ラベル表示
 *   - ドラッグ&ドロップ
 *       ファイルやフォルダをドロップ ..... ドックに追加 (落とした位置に挿入)
 *       アイコンをドックの外へドラッグ ... そのまま離すと削除
 *       ドック内でドラッグ ............... 並べ替え
 *   - 初期状態は空。追加したアプリだけを表示 / 起動中のアプリの下に丸を1つ表示
 *   - クリック: 未起動なら起動 / 実行中で前面ならもう一度クリックで最小化(収縮) / 裏・最小化中なら前面へ
 *       Shift+クリック or 中クリック: 新しく起動
 *   - 右クリック: ウィンドウを閉じる / 新しく起動 / 固定・削除 / 設定 / 終了
 *       設定: 向き(横/縦) / 位置(下・上・左・右) / 自動的に隠す / 隠れるまでの時間
 *   - Ctrl+ドラッグでドックを画面上の好きな場所へ制限なく移動 (端への吸着なし。自由配置中は「自動的に隠す」は無効)

 *       右クリック > 設定 > 位置 (下・上・左・右 / 中央に戻す) で再び端に固定

 *   - ドックは最大3つまで。アイコン一覧・位置・形・デザイン・重なり順などはドックごとに独立
 *       右クリック > ドックを追加 / このドックを削除、またはアプリ設定(ドック)で増減・編集するドックを選ぶ
 *       promdock.txt は @dock=N の行でドックごとに区切る (@dock= のない古い設定は1つ目のドックとして読む)
 *   - 重なり順: 最前列 (常に他のウィンドウの手前) / 最奥 (デスクトップの上・他のウィンドウの背面)。右クリック > 設定 > 重なり順 で切り替え

 *   - 全円ドック: マウスホイール または Shift+ドラッグ でアイコンを回転
 *   - 設定は exe と同じフォルダの promdock.txt (UTF-8) に保存
 *       @autohide @edge @frac @hidems のほか、アプリ設定(setting.c)から変更できる
 *       @iconsize=48 (アイコンサイズ px) / @zoom=150 (ホバー時の拡大率 %) / @opacity=78 (背景の濃さ %)
 *       @style=0 (デザイン 0:標準 1:macOS 2:ガラス 3:モダン 4:ネオン 5:オーロラ 6:オニキス 7:ノワール)
 *       デザインは右クリック > 設定 > デザイン、またはアプリ設定(ドック > 外観)から切り替えられる
 *       @docktype=0 (ドックの形 0:バー 1:半円 2:全円。半円は画面の端を中心にした円弧状、全円は画面の端に接する完全な円環状に並び、デザインもそのまま使える)
 *       ドックの形は右クリック > 設定 > ドックの形、またはアプリ設定(ドック > 外観)から切り替えられる
 *   - F11 でフルスクリーンのランチャーを開く (launcher.c / 同じexeにビルド)
 *   - デスクトップに置ける検索バー (searchbar.c / 同じexeにビルド。ブラウザと検索エンジンを選んで追加)
 *   - デスクトップに置ける画像・GIF (photoframe.c) と時計ウィジェット (clock.c) も同じexeにビルド
 *   - 初回起動時はアプリ設定(setting.c)が開き、ドック/ランチャーを有効にするか選べる
 *     アプリ設定は「全般 / ドック / ランチャー / 検索バー / 画像・GIF追加 / 時計」のページに分かれており、各機能の細かい設定もそこで変更できる
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
#include <shlobj.h>
#include <shobjidl.h>
#include <ole2.h>
#include <dwmapi.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* launcher.c (同じexeにリンクされるランチャー) */
void Launcher_Init(HINSTANCE hInst);
void Launcher_Toggle(void);
void Launcher_Shutdown(void);
/* searchbar.c (デスクトップに置ける検索バー) */
void SearchBar_Init(HINSTANCE hInst);
void SearchBar_Shutdown(void);
/* photoframe.c (デスクトップに置ける写真・GIF) */
void PhotoFrame_Init(HINSTANCE hInst);
void PhotoFrame_Shutdown(void);
/* clock.c (デスクトップに置ける時計) */
void Clock_Init(HINSTANCE hInst);
void Clock_Shutdown(void);
/* setting.c (アプリ設定: ドック/ランチャーの有効・無効) */
BOOL Settings_Init(HINSTANCE hInst);
void Settings_Open(void);
void Settings_OpenPage(int page); /* 0:全般 1:ドック 2:ランチャー 3:検索バー */
BOOL Settings_DockEnabled(void);
BOOL Settings_LauncherEnabled(void);
BOOL Settings_PreTranslate(MSG *m);
void Settings_Shutdown(void);

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
#define BASE_SIZE_DEF 48.0f /* アイコンの標準サイズ (設定で変更可) */
#define MAX_SCALE_DEF 1.5f  /* 拡大の最大倍率 (設定で変更可) */
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
    wchar_t proc[64];           /* --processStart で起動される実プロセス名 (例: Discord.exe) */
    BOOL pinned;
    DWORD launchTick;
    BOOL iconOk;               /* FALSE: アイコン取得失敗(仮アイコン)。後で再取得する */
    uint32_t *mip[MIP_LEVELS]; /* premultiplied BGRA 256/128/64 */
    float scale, cx, sz;
    float ang; /* サークル時: 円弧上の角度 (u軸からのラジアン。π/2が真上) */
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
static const Style g_labelStyle = {26, 26, 30, 0.92f, 0, 0, 0, 0.0f};

/* ------------------------------------------------------------------ */
/* 複数ドック (最大 MAX_DOCKS 個)                                         */
/*   ドックごとの状態は DockState にまとめ、gDock が「いま処理中のドック」を指す。   */
/*   g_xxx という名前は gDock->xxx への別名なので、描画・操作のコードはそのまま使える。 */
/*   ウィンドウメッセージ / ドロップ / 設定からの呼び出しの入口で gDock を切り替える。  */
/* ------------------------------------------------------------------ */
#define MAX_DOCKS 3
typedef struct DockState
{
    Item items[MAX_ITEMS];
    int count;
    HWND hwnd;
    HDC memdc;
    HBITMAP dib, oldBmp;
    uint32_t *bits;
    int W, H, posX, posY0;
    float hideDelta, slide;
    int LW, LH;
    int edge;
    float frac;
    int hideMs;
    BOOL moving, zBottom, freeMode;
    float fx, fy;
    POINT moveStart;
    int moveWX, moveWY;
    float rot, rotTarget;
    BOOL rotating;
    float rotLast;
    float base, gap, padx, pady, margin, bounce;
    float baseline, barV0, barV1, barL, barR, hoverV;
    float cU, cV, Rref, ro0, ro1;
    BOOL hover, tracking, animOn, menuOpen, dragged;
    BOOL autohide;
    int dragOut;
    BOOL dropActive, dropOk;
    int dropIdx;
    float dropOpen;
    float mouseU, mouseV;
    int pressed;
    POINT downPt;
    BOOL armed;
    DWORD leaveTick;
    unsigned sig;
    int style, dockType;
    Style barStyle;
    float baseSize, maxScale;
    void *dt; /* IDropTarget (MyDT) */
} DockState;

static DockState *gDock;
static DockState *g_docks[MAX_DOCKS];
static int g_nDocks = 0;
static int g_editDock = 0;        /* アプリ設定で編集中のドック */
static BOOL g_dockEnabled = TRUE; /* ドック全体の有効/無効 (設定) */
BOOL Dock_Add(void);              /* ドックを1つ追加 (右クリックメニュー・設定から) */
#define g_dockType (gDock->dockType)
#define g_style (gDock->style)
#define g_barStyle (gDock->barStyle)
#define g_baseSize (gDock->baseSize)
#define g_maxScale (gDock->maxScale)
#define g_items (gDock->items)
#define g_count (gDock->count)
#define g_hwnd (gDock->hwnd)
#define g_memdc (gDock->memdc)
#define g_dib (gDock->dib)
#define g_oldBmp (gDock->oldBmp)
#define g_bits (gDock->bits)
#define g_W (gDock->W)
#define g_H (gDock->H)
#define g_posX (gDock->posX)
#define g_posY0 (gDock->posY0)
#define g_hideDelta (gDock->hideDelta)
#define g_slide (gDock->slide)
#define g_LW (gDock->LW)
#define g_LH (gDock->LH)
#define g_edge (gDock->edge)
#define g_frac (gDock->frac)
#define g_hideMs (gDock->hideMs)
#define g_moving (gDock->moving)
#define g_zBottom (gDock->zBottom)
#define g_free (gDock->freeMode)
#define g_fx (gDock->fx)
#define g_fy (gDock->fy)
#define g_moveStart (gDock->moveStart)
#define g_moveWX (gDock->moveWX)
#define g_moveWY (gDock->moveWY)
#define g_rot (gDock->rot)
#define g_rotTarget (gDock->rotTarget)
#define g_rotating (gDock->rotating)
#define g_rotLast (gDock->rotLast)
#define g_base (gDock->base)
#define g_gap (gDock->gap)
#define g_padx (gDock->padx)
#define g_pady (gDock->pady)
#define g_margin (gDock->margin)
#define g_bounce (gDock->bounce)
#define g_baseline (gDock->baseline)
#define g_barV0 (gDock->barV0)
#define g_barV1 (gDock->barV1)
#define g_barL (gDock->barL)
#define g_barR (gDock->barR)
#define g_hoverV (gDock->hoverV)
#define g_cU (gDock->cU)
#define g_cV (gDock->cV)
#define g_Rref (gDock->Rref)
#define g_ro0 (gDock->ro0)
#define g_ro1 (gDock->ro1)
#define g_hover (gDock->hover)
#define g_tracking (gDock->tracking)
#define g_animOn (gDock->animOn)
#define g_menuOpen (gDock->menuOpen)
#define g_dragged (gDock->dragged)
#define g_autohide (gDock->autohide)
#define g_dragOut (gDock->dragOut)
#define g_dropActive (gDock->dropActive)
#define g_dropOk (gDock->dropOk)
#define g_dropIdx (gDock->dropIdx)
#define g_dropOpen (gDock->dropOpen)
#define g_mouseU (gDock->mouseU)
#define g_mouseV (gDock->mouseV)
#define g_pressed (gDock->pressed)
#define g_downPt (gDock->downPt)
#define g_armed (gDock->armed)
#define g_leaveTick (gDock->leaveTick)
#define g_sig (gDock->sig)

static DockState *NewDock(void)
{
    DockState *d = (DockState *)calloc(1, sizeof(DockState));
    if (!d)
        return NULL;
    d->edge = 0;
    d->frac = 0.5f;
    d->hideMs = 600;
    d->fx = d->fy = 0.5f;
    d->dragOut = -1;
    d->pressed = -1;
    d->baseSize = BASE_SIZE_DEF;
    d->maxScale = MAX_SCALE_DEF;
    d->barStyle.fr = 36;
    d->barStyle.fg = 36;
    d->barStyle.fb = 40;
    d->barStyle.fa = 0.78f; /* ダークグレー半透明(濃いめ)・枠線なし。fa は設定で変更可 */
    return d;
}

/* ------------------------------------------------------------------ */
/* デザイン (テーマ)                                                     */
/*   バーの形・色・影・枠線・実行中の目印・ラベルをテーマごとに持つ。            */
/*   新しいデザインは g_themes に1行足して N_STYLES を増やすだけ。            */
/* ------------------------------------------------------------------ */
#define N_STYLES 8
typedef struct
{
    float r, g, b, a; /* r,g,b: 0..255 / a: 0..1 */
} Col;
typedef struct
{
    const wchar_t *name;
    BOOL tiles;                         /* TRUE: バーを描かず、アイコンごとにタイルを描く */
    float padx, pady, gap, margin, rad; /* 96DPI基準px。rad = バーの角丸 */
    float iconScale;                    /* アイコンの描画倍率 (タイル内に収める用) */
    Col f0, f1;                         /* 塗り: f0=画面端側 (alongU時は始点) / f1=外側 (終点) */
    BOOL alongU;                        /* TRUE: 塗りと枠線のグラデーションを端に沿う方向にかける */
    Col b0, b1;                         /* 枠線の色 */
    float bw;                           /* 枠線の太さ (0で枠線なし) */
    float shine;                        /* 外側半分にかける光沢の強さ */
    float rim;                          /* 外側の縁のすぐ内側に引くハイライトの強さ */
    Col sh;                             /* 影 / グロー の色 */
    float shBlur;                       /* 影 / グロー の広がり (px) */
    int ind;                            /* 実行中の目印 0:丸 1:ピル 2:グロー付きの丸 */
    float indR;                         /* 丸の半径 (px) */
    Col indC;                           /* 目印の色 */
    BOOL halo;                          /* 丸の周りに薄い影を付ける (明るい背景でも見えるように) */
    float divider;                      /* 固定項目と実行中項目の区切り線の濃さ (0で無し) */
    Style label;                        /* ラベルの背景・枠線 */
    float labelRad;                     /* ラベルの角丸 (高さに対する比。0.5でピル) */
    Col labelTxt;                       /* ラベルの文字色 */
} Theme;

static const Theme g_themes[N_STYLES] = {
    /* 0: 標準 (従来のダークグレー半透明) */
    {L"標準", FALSE, BAR_PAD_X, BAR_PAD_Y, ICON_GAP, EDGE_MARGIN, 18.0f, 1.0f, {36, 36, 40, 0.78f}, {36, 36, 40, 0.78f}, FALSE, {0, 0, 0, 0.0f}, {0, 0, 0, 0.0f}, 0.0f, 0.0f, 0.0f, {0, 0, 0, 0.0f}, 0.0f, 0, 2.6f, {235, 235, 240, 0.95f}, FALSE, 0.0f, {26, 26, 30, 0.92f, 0, 0, 0, 0.0f}, 0.5f, {255, 255, 255, 1.0f}},

    /* 1: macOS風 (すりガラス調の明るい半透明バー + 細い縁 + やわらかい影 + 小さな丸 + 区切り線) */
    {L"macOS", FALSE, 12.0f, 10.0f, 6.0f, 10.0f, 22.0f, 1.0f, {246, 248, 255, 0.20f}, {255, 255, 255, 0.34f}, FALSE, {255, 255, 255, 0.20f}, {255, 255, 255, 0.52f}, 1.0f, 0.0f, 0.0f, {0, 0, 0, 0.34f}, 12.0f, 0, 2.3f, {255, 255, 255, 0.92f}, TRUE, 0.30f, {32, 32, 36, 0.90f, 255, 255, 255, 0.14f}, 0.34f, {255, 255, 255, 1.0f}},

    /* 2: ガラス (青みのある透明ガラス + 光沢 + 明るい縁) */
    {L"ガラス", FALSE, 14.0f, 11.0f, 8.0f, 10.0f, 26.0f, 1.0f, {120, 150, 215, 0.16f}, {190, 215, 255, 0.26f}, FALSE, {255, 255, 255, 0.20f}, {255, 255, 255, 0.78f}, 1.2f, 0.20f, 0.35f, {30, 50, 110, 0.40f}, 12.0f, 2, 2.4f, {175, 215, 255, 1.0f}, FALSE, 0.22f, {24, 32, 56, 0.90f, 190, 215, 255, 0.35f}, 0.5f, {240, 247, 255, 1.0f}},

    /* 3: モダン (バーなし。アイコンごとのダークタイル + ピル型の目印) */
    {L"モダン", TRUE, 8.0f, 11.0f, 12.0f, 10.0f, 0.0f, 0.78f, {24, 26, 34, 0.82f}, {46, 50, 64, 0.82f}, FALSE, {255, 255, 255, 0.06f}, {255, 255, 255, 0.20f}, 1.0f, 0.06f, 0.0f, {0, 0, 0, 0.40f}, 9.0f, 1, 0.0f, {96, 165, 255, 1.0f}, FALSE, 0.0f, {22, 24, 30, 0.94f, 255, 255, 255, 0.10f}, 0.32f, {255, 255, 255, 1.0f}},

    /* 4: ネオン (濃い紺のバー + マゼンタ→シアンの発光する枠線) */
    {L"ネオン", FALSE, 16.0f, 12.0f, 9.0f, 12.0f, 20.0f, 1.0f, {12, 10, 26, 0.86f}, {12, 10, 26, 0.86f}, TRUE, {255, 64, 200, 1.0f}, {64, 220, 255, 1.0f}, 1.6f, 0.05f, 0.0f, {150, 90, 255, 0.50f}, 12.0f, 2, 2.4f, {90, 245, 255, 1.0f}, FALSE, 0.25f, {14, 12, 28, 0.94f, 120, 220, 255, 0.55f}, 0.5f, {220, 245, 255, 1.0f}},

    /* 5: オーロラ (紫→ピンクのグラデーション + 光沢) */
    {L"オーロラ", FALSE, 14.0f, 11.0f, 8.0f, 10.0f, 24.0f, 1.0f, {110, 90, 255, 0.62f}, {255, 110, 180, 0.62f}, TRUE, {255, 255, 255, 0.40f}, {255, 255, 255, 0.40f}, 1.0f, 0.18f, 0.25f, {90, 50, 180, 0.42f}, 12.0f, 0, 2.4f, {255, 255, 255, 1.0f}, TRUE, 0.35f, {40, 24, 80, 0.92f, 255, 255, 255, 0.18f}, 0.5f, {255, 255, 255, 1.0f}},

    /* 6: オニキス (マットな漆黒 + 細い縁 + 小さめの角丸。ミニマルでシック) */
    {L"オニキス", FALSE, 12.0f, 10.0f, 8.0f, 10.0f, 10.0f, 1.0f, {12, 12, 14, 0.94f}, {30, 30, 34, 0.94f}, FALSE, {255, 255, 255, 0.06f}, {255, 255, 255, 0.16f}, 1.0f, 0.0f, 0.10f, {0, 0, 0, 0.55f}, 10.0f, 0, 2.2f, {235, 235, 240, 0.90f}, FALSE, 0.14f, {10, 10, 12, 0.95f, 255, 255, 255, 0.12f}, 0.22f, {240, 240, 244, 1.0f}},

    /* 7: ノワール (黒 + シャンパンゴールドの細い縁と目印。ほぼ直角に近い角丸) */
    {L"ノワール", FALSE, 14.0f, 10.0f, 8.0f, 10.0f, 6.0f, 1.0f, {6, 6, 7, 0.95f}, {14, 14, 15, 0.95f}, FALSE, {196, 164, 104, 0.35f}, {222, 190, 130, 0.85f}, 1.0f, 0.0f, 0.0f, {0, 0, 0, 0.60f}, 10.0f, 0, 2.2f, {222, 190, 130, 1.0f}, FALSE, 0.22f, {6, 6, 7, 0.96f, 222, 190, 130, 0.50f}, 0.18f, {240, 230, 210, 1.0f}},
};
static const Theme *Th(void) { return &g_themes[g_style]; }

/* ドックの形: 0=バー(従来) / 1=半円 (画面の端を中心にした円弧状に並ぶ) / 2=全円 (画面の端に接する完全な円環状に並ぶ)。デザインはバーと共通 */
#define N_DOCKTYPES 3
static const wchar_t *const g_dockTypeNames[N_DOCKTYPES] = {L"バー", L"半円", L"全円"};
static BOOL IsCircle(void) { return g_dockType >= 1; } /* 極座標配置 (半円・全円共通) */
static BOOL IsRing(void) { return g_dockType == 2; }   /* 全円 (円環) */
#define HALF_PI 1.5707963f
#define PI_F 3.14159265f
/* 「背景の濃さ」設定 (既定 0.78 = 等倍) をテーマの塗りの不透明度に掛ける */
static float OpacityMul(void) { return g_barStyle.fa / 0.78f; }

static const int g_mipSize[MIP_LEVELS] = {256, 128, 64};

static float g_dpi = 1.0f;
static BOOL AutoHideOn(void) { return g_autohide && !g_free; } /* 自由配置中は端へ隠せないので無効 */

/* ドラッグ&ドロップ状態 */
static HWND g_ghost; /* ドラッグ中アイコンのゴースト窓 */
static HDC g_ghostDC;
static HBITMAP g_ghostBmp, g_ghostOld;
static uint32_t *g_ghostBits;
static int g_ghostAX, g_ghostAY;
static wchar_t g_cfgPath[MAX_PATH];

/* ラベル用キャッシュ */
static wchar_t g_lblText[128];
static uint8_t *g_lblMask;
static int g_lblW, g_lblH;

/* ------------------------------------------------------------------ */
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static BOOL SelectByHwnd(HWND h)
{
    for (int i = 0; i < g_nDocks; i++)
        if (g_docks[i] && g_docks[i]->hwnd == h)
        {
            gDock = g_docks[i];
            return TRUE;
        }
    return FALSE;
}
static BOOL IsDockHwnd(HWND h)
{
    for (int i = 0; i < g_nDocks; i++)
        if (g_docks[i] && g_docks[i]->hwnd == h)
            return TRUE;
    return FALSE;
}
static void UseEdit(void) /* 設定から呼ばれる関数の入口: 編集中のドックを処理対象にする */
{
    g_editDock = ClampI(g_editDock, 0, g_nDocks > 0 ? g_nDocks - 1 : 0);
    gDock = g_docks[g_editDock];
}
static void DocksInitDefault(void)
{
    if (g_nDocks == 0)
    {
        g_docks[0] = NewDock();
        g_nDocks = 1;
    }
    gDock = g_docks[0];
}

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

/* サークル: 極座標 (半径 rho, 角度 phi) -> 論理座標。phi は u 軸からの角度 (π/2 が真上) */
static void PolarUV(float rho, float phi, float *u, float *v)
{
    *u = g_cU + rho * cosf(phi);
    *v = g_cV + rho * sinf(phi);
}
/* ウィンドウ座標の点と、その点での接線方向 (ウィンドウ座標の単位ベクトル。不要なら NULL) */
static void PolarW(float rho, float phi, float *x, float *y, float *tx, float *ty)
{
    float u, v;
    PolarUV(rho, phi, &u, &v);
    L2W(u, v, x, y);
    if (tx && ty)
    {
        float u2, v2, x2, y2;
        PolarUV(rho, phi + 0.02f, &u2, &v2);
        L2W(u2, v2, &x2, &y2);
        float dx = x2 - *x, dy = y2 - *y, l = sqrtf(dx * dx + dy * dy);
        if (l < 1e-4f)
            l = 1.0f;
        *tx = dx / l;
        *ty = dy / l;
    }
}
/* 項目を並べる軸 (バー: u / サークル: 円弧の長さ) 上でのマウス位置。項目の cx と同じ座標系 */
/* 中心から見た (dx,dy) の方向を、真上を0・時計回りを正とした角度で返す。
 * 半円: 下半分は左右の端に丸める / 全円: -π..π に折り返す (真下が継ぎ目) */
static float ArcAngle(float dx, float dy)
{
    if (IsRing())
    {
        float a = HALF_PI - atan2f(dy, dx);
        return a > PI_F ? a - 2.0f * PI_F : a;
    }
    float phi = dy >= 0.0f ? atan2f(dy, dx) : (dx < 0.0f ? PI_F : 0.0f);
    return HALF_PI - phi;
}
static float WrapPi(float a)
{
    while (a > PI_F)
        a -= 2.0f * PI_F;
    while (a <= -PI_F)
        a += 2.0f * PI_F;
    return a;
}
static float RotOf(void) { return IsRing() ? g_rot : 0.0f; } /* 全円だけ回転できる */
static float MouseAlong(void)
{
    if (!IsCircle())
        return g_mouseU;
    return g_LW * 0.5f + WrapPi(ArcAngle(g_mouseU - g_cU, g_mouseV - g_cV) - RotOf()) * g_Rref;
}
/* ドックの外へ引き出し中か (離すと削除) */
static BOOL OutsideDock(void)
{
    if (IsCircle())
    {
        float dx = g_mouseU - g_cU, dy = g_mouseV - g_cV;
        return sqrtf(dx * dx + dy * dy) > g_barV1 + 40.0f * g_dpi;
    }
    return g_mouseV > g_barV1 + 40.0f * g_dpi;
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

/* .exe ならそのパス、.lnk ならリンク先を exe に入れる。
 * Discord等 (Update.exe --processStart Discord.exe) は実プロセス名を proc に入れる */
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
    DockState *keep = gDock;
    fprintf(f, "@docks=%d\r\n", g_nDocks);
    for (int di = 0; di < g_nDocks; di++)
    {
        gDock = g_docks[di];
        fprintf(f, "@dock=%d\r\n", di); /* ここから次の @dock= までが、このドックの設定とアイコン一覧 */
        fprintf(f, "@autohide=%d\r\n@edge=%d\r\n@frac=%d\r\n@hidems=%d\r\n", g_autohide ? 1 : 0, g_edge, (int)(g_frac * 1000.0f + 0.5f), g_hideMs);
        fprintf(f, "@iconsize=%d\r\n@zoom=%d\r\n@opacity=%d\r\n", (int)(g_baseSize + 0.5f), (int)(g_maxScale * 100.0f + 0.5f), (int)(g_barStyle.fa * 100.0f + 0.5f));
        fprintf(f, "@style=%d\r\n@docktype=%d\r\n", g_style, g_dockType);
        fprintf(f, "@zorder=%d\r\n", g_zBottom ? 1 : 0);
        fprintf(f, "@free=%d\r\n@fx=%d\r\n@fy=%d\r\n", g_free ? 1 : 0, (int)floorf(g_fx * 1000.0f + 0.5f), (int)floorf(g_fy * 1000.0f + 0.5f));
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
    }
    gDock = keep;
    fclose(f);
}

static void LoadConfig(void)
{
    DocksInitDefault(); /* 古い形式 (@dock= なし) の設定は、1つ目のドックとして読む */
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
            if (strncmp(p, "@dock=", 6) == 0)
            {
                int n = ClampI(atoi(p + 6), 0, MAX_DOCKS - 1);
                while (g_nDocks <= n)
                {
                    DockState *nd = NewDock();
                    if (!nd)
                        break;
                    g_docks[g_nDocks++] = nd;
                }
                gDock = g_docks[ClampI(n, 0, g_nDocks - 1)];
            }
            else if (strncmp(p, "@autohide=", 10) == 0)
                g_autohide = atoi(p + 10) != 0;
            else if (strncmp(p, "@edge=", 6) == 0)
                g_edge = ClampI(atoi(p + 6), 0, 3);
            else if (strncmp(p, "@frac=", 6) == 0)
                g_frac = ClampI(atoi(p + 6), 0, 1000) / 1000.0f;
            else if (strncmp(p, "@zorder=", 8) == 0)
                g_zBottom = atoi(p + 8) != 0;
            else if (strncmp(p, "@free=", 6) == 0)
                g_free = atoi(p + 6) != 0;
            else if (strncmp(p, "@fx=", 4) == 0)
                g_fx = ClampI(atoi(p + 4), -500, 1500) / 1000.0f;
            else if (strncmp(p, "@fy=", 4) == 0)
                g_fy = ClampI(atoi(p + 4), -500, 1500) / 1000.0f;
            else if (strncmp(p, "@hidems=", 8) == 0)
                g_hideMs = ClampI(atoi(p + 8), 0, 60000);
            else if (strncmp(p, "@iconsize=", 10) == 0)
                g_baseSize = (float)ClampI(atoi(p + 10), 32, 96);
            else if (strncmp(p, "@zoom=", 6) == 0)
                g_maxScale = ClampI(atoi(p + 6), 100, 200) / 100.0f;
            else if (strncmp(p, "@opacity=", 9) == 0)
                g_barStyle.fa = ClampI(atoi(p + 9), 30, 100) / 100.0f;
            else if (strncmp(p, "@style=", 7) == 0)
                g_style = ClampI(atoi(p + 7), 0, N_STYLES - 1);
            else if (strncmp(p, "@docktype=", 10) == 0)
                g_dockType = ClampI(atoi(p + 10), 0, N_DOCKTYPES - 1);
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
    gDock = g_docks[0];
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
    if (IsDockHwnd(h) || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
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
/* デスクトップ (アイコンを載せているシェルのウィンドウ) */
static HWND FindDesktopHost(void)
{
    HWND pm = FindWindowW(L"Progman", NULL);
    if (pm && FindWindowExW(pm, NULL, L"SHELLDLL_DefView", NULL))
        return pm;
    HWND w = NULL;
    while ((w = FindWindowExW(NULL, w, L"WorkerW", NULL)) != NULL)
        if (FindWindowExW(w, NULL, L"SHELLDLL_DefView", NULL))
            return w;
    return pm;
}

/* 重なり順を反映する。最前列: TOPMOST / 最奥: デスクトップのすぐ上 (他のウィンドウは全部手前になる)。
 * HWND_BOTTOM だとデスクトップの裏に回って見えなくなることがあるため、デスクトップの直前に差し込む */
static void ApplyZOrder(int x, int y, int w, int h, UINT flags)
{
    if (!g_zBottom)
    {
        SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, flags | SWP_NOACTIVATE);
        return;
    }
    HWND host = FindDesktopHost();
    HWND prev = host ? GetWindow(host, GW_HWNDPREV) : NULL;
    if (prev == g_hwnd)
    { /* すでにデスクトップの直上 */
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, x, y, w, h, flags | SWP_NOACTIVATE | SWP_NOZORDER);
        if (GetWindowLongW(g_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
            SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    SetWindowPos(g_hwnd, prev ? prev : HWND_BOTTOM, x, y, w, h, flags | SWP_NOACTIVATE);
}

static void ResizeDock(void)
{
    float d = g_dpi;
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    BOOL vert = IsVert();

    const Theme *th = Th();
    g_gap = th->gap * d;
    g_padx = th->padx * d;
    g_pady = th->pady * d;
    g_margin = th->margin * d;
    g_bounce = BOUNCE_HEIGHT * d;

    int n = g_count > 0 ? g_count : 1;
    float len = (float)(vert ? (wa.bottom - wa.top) : (wa.right - wa.left));
    float perp = (float)(vert ? (wa.right - wa.left) : (wa.bottom - wa.top));
    float limit = len * 0.92f;

    if (IsCircle())
    {
        /* サークル: 画面の端に中心を置いた半円状の帯 (全円は端に接する円環)。個数が増えるほど半径が大きくなる。
         * 軸方向 g_LW = 円の直径 + 余白 / 端からの厚み g_LH = 中心の高さ + 半径 + 余白 */
        const float maxSpan = IsRing() ? 2.0f * PI_F : 2.3f; /* 帯が占める最大の角度 (半円: 約132度 / 全円: 360度) */
        g_base = g_baseSize * d;
        for (;;)
        { /* 画面に収まるまでアイコンを縮める */
            /* 斜めに並ぶので、重ならないよう隙間を広めに取る (タイルは特に広く) */
            g_gap = th->tiles ? g_base * 0.46f : fmaxf(th->gap * d, g_base * 0.22f);
            float restLen = n * g_base + (n - 1) * g_gap + 2 * g_padx + SepW();
            float extra = (n < 6 ? n : 6) * g_base * (g_maxScale - 1.0f);
            float rmin = g_base * 1.7f + g_pady + g_base * 0.5f; /* 中の空きを確保 (ラベル表示用) */
            g_Rref = fmaxf(rmin, (restLen + extra) / maxSpan);
            g_ro0 = g_Rref - g_base * 0.5f - g_pady;
            g_ro1 = g_ro0 + g_base + 2 * g_pady;
            float roMax = g_ro0 + g_pady + g_maxScale * g_base + g_bounce;
            /* 半円: 中心は端の少し内側 / 全円: 帯の下端が端から余白だけ離れて接するよう、中心を半径ぶん持ち上げる */
            g_cV = IsRing() ? g_margin + g_ro1 : g_margin + g_pady + g_base * 0.5f;
            g_LW = (int)(2 * (roMax + g_margin));
            g_LH = (int)(g_cV + roMax + g_margin + 4);
            if ((g_LW <= limit && g_LH <= perp * 0.92f) || g_base <= 20 * d)
                break;
            g_base -= 1.0f;
        }
        g_cU = g_LW * 0.5f;
        g_W = vert ? g_LH : g_LW;
        g_H = vert ? g_LW : g_LH;

        g_barV0 = g_ro0;
        g_barV1 = g_ro1;
        g_baseline = g_ro0 + g_pady;
        g_hoverV = g_baseline + g_maxScale * g_base + 6 * d;
        g_hideDelta = g_cV + g_ro1 - 4 * d; /* 隠れた時は円弧の頂点だけ4px見せる */
    }
    else
    {
        float sep = SepW();
        g_base = g_baseSize * d;
        for (;;)
        { /* 画面に収まるまでアイコンを縮める */
            float restW = n * g_base + (n - 1) * g_gap + 2 * g_padx + sep;
            float extra = (n < 6 ? n : 6) * g_base * (g_maxScale - 1.0f);
            g_LW = (int)(restW + extra + (g_base + g_gap) + 4 * g_margin);
            if (g_LW <= limit || g_base <= 20 * d)
                break;
            g_base -= 1.0f;
        }
        float labelExt = vert ? LABEL_W_VERT * d : LABEL_H * d;
        g_LH = (int)(g_margin + g_pady + g_maxScale * g_base + g_bounce + labelExt + 4);
        g_W = vert ? g_LH : g_LW;
        g_H = vert ? g_LW : g_LH;

        g_barV0 = g_margin;
        g_barV1 = g_margin + g_base + 2 * g_pady;
        g_baseline = g_margin + g_pady;
        g_hoverV = g_baseline + g_maxScale * g_base + 6 * d;
        g_hideDelta = g_barV1 - 4 * d; /* 隠れた時はバーの端4pxだけ見せる */
    }

    float start = (float)(vert ? wa.top : wa.left);
    int along = (int)(start + g_frac * len - g_LW * 0.5f);
    along = ClampI(along, (int)start, (int)(start + len) - g_LW);
    if (g_free)
    { /* 自由配置: 制限なし (見失わないよう、一部だけは作業領域内に残す) */
        float fw = (float)(wa.right - wa.left), fh = (float)(wa.bottom - wa.top);
        int keep = (int)(48 * d);
        g_posX = (int)(wa.left + g_fx * fw - g_W * 0.5f);
        g_posY0 = (int)(wa.top + g_fy * fh - g_H * 0.5f);
        g_posX = ClampI(g_posX, wa.left - g_W + keep, wa.right - keep);
        g_posY0 = ClampI(g_posY0, wa.top - g_H + keep, wa.bottom - keep);
    }
    else
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
    ApplyZOrder(g_posX, g_posY0, g_W, g_H, 0);
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
        if (IsCircle()) /* cx は弧の長さ (中心 = g_LW/2)。基準半径での弧長から角度を求める */
            g_items[i].ang = HALF_PI - (g_items[i].cx - g_LW * 0.5f) / g_Rref - RotOf();
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
    float mu = MouseAlong();

    for (int i = 0; i < g_count; i++)
    {
        Item *it = &g_items[i];
        float target = 1.0f;
        if (i == g_dragOut)
            target = 0.0f; /* 引き出し中は隙間を詰める */
        else if (g_hover && !g_dragged && !g_dropActive)
        {
            /* マウス直下を最大に、距離に応じてガウス状に周辺も拡大 */
            float dx = mu - (off + c[i]);
            target = 1.0f + (g_maxScale - 1.0f) * expf(-(dx * dx) / (2 * sigma * sigma));
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

    /* 全円の回転 (ホイール: 目標角へなめらかに追従) */
    if (IsRing())
    {
        float rd = g_rotTarget - g_rot;
        if (fabsf(rd) < 0.0005f)
        {
            g_rot = g_rotTarget = WrapPi(g_rotTarget);
        }
        else
        {
            g_rot += rd * 0.3f;
            busy = TRUE;
        }
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
    if (AutoHideOn() && !g_hover && !g_menuOpen && g_dragOut < 0 && now - g_leaveTick > (DWORD)g_hideMs)
        slideTarget = 1.0f;
    float sd = slideTarget - g_slide;
    if (fabsf(sd) < 0.003f)
        g_slide = slideTarget;
    else
    {
        g_slide += sd * 0.25f;
        busy = TRUE;
    }
    if (AutoHideOn() && !g_hover && g_slide < 1.0f)
        busy = TRUE; /* 隠れるまで待機 */

    return busy || g_hover;
}

static int HitTest(float u, float v)
{
    if (IsCircle())
    { /* 帯の外周より内側で、マウスの角度にある項目 */
        float dx = u - g_cU, dy = v - g_cV;
        float rho = sqrtf(dx * dx + dy * dy);
        if ((dy < 0.0f && !IsRing()) || rho > g_hoverV || rho < g_ro0 - 6.0f * g_dpi)
            return -1;
        float a = g_LW * 0.5f + WrapPi(ArcAngle(dx, dy) - RotOf()) * g_Rref;
        for (int i = 0; i < g_count; i++)
        {
            float l = g_items[i].cx - g_items[i].sz * 0.5f - g_gap * 0.5f;
            float r = g_items[i].cx + g_items[i].sz * 0.5f + g_gap * 0.5f;
            if (a >= l && a < r)
                return i;
        }
        return -1;
    }
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

/* 項目の中心 (論理座標)。バー: 端に沿った位置 / サークル: 円弧上の角度と半径。バウンスは外向きに持ち上がる */
static void ItemCenterUV(const Item *it, DWORD now, float *u, float *v)
{
    float lift = LiftOf(it, now);
    if (IsCircle())
        PolarUV(g_baseline + it->sz * 0.5f + lift, it->ang, u, v);
    else
    {
        *u = it->cx;
        *v = g_baseline + it->sz * 0.5f + lift;
    }
}

/* 汎用パネル: 影/グロー + グラデーション塗り + 光沢 + 縁ハイライト + 枠線 を1パスで描く。
 * 座標は論理座標 (u: 軸方向, v: 画面端からの距離)。既存ピクセルの上に合成する。
 *   pass  : 1=影だけ 2=本体だけ 3=両方 (タイルは影を先にまとめて描くために分ける)
 *   aMul  : 塗りの不透明度の倍率 / lift: 白に近づける量 0..1 / accent: 枠線色の上書き (NULL可) */
static float Lerpf(float a, float b, float t) { return a + (b - a) * t; }

#define OVER(cr, cg, cb, ca)      \
    do                            \
    {                             \
        float _a = (ca);          \
        float _i = 1.0f - _a;     \
        pr = (cr) * _a + pr * _i; \
        pg = (cg) * _a + pg * _i; \
        pb = (cb) * _a + pb * _i; \
        pa = _a + pa * _i;        \
    } while (0)

static void DrawPanel(float u0, float v0, float u1, float v1, float rad, const Theme *t,
                      float aMul, float lift, const Col *accent, int pass)
{
    float L, T, R, B;
    LRect(u0, v0, u1, v1, &L, &T, &R, &B);
    float d = g_dpi;
    float blur = (pass & 1) ? t->shBlur * d : 0.0f;
    float cx = (L + R) * 0.5f, cy = (T + B) * 0.5f, hw = (R - L) * 0.5f, hh = (B - T) * 0.5f;
    float ext = blur + 3.0f;
    int x0 = ClampI((int)(L - ext), 0, g_W), x1 = ClampI((int)(R + ext) + 1, 0, g_W);
    int y0 = ClampI((int)(T - ext), 0, g_H), y1 = ClampI((int)(B + ext) + 1, 0, g_H);
    float bw = t->bw * d;
    float lu = fmaxf(u1 - u0, 1.0f), lv = fmaxf(v1 - v0, 1.0f);
    float fa0 = fminf(t->f0.a * aMul, 1.0f), fa1 = fminf(t->f1.a * aMul, 1.0f);

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f - cx, py = y + 0.5f - cy;
            float qx = fabsf(px) - hw + rad, qy = fabsf(py) - hh + rad;
            float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f);
            float dist = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - rad; /* 角丸矩形SDF */
            if (dist > blur + 1.0f)
                continue;

            float pr = 0.0f, pg = 0.0f, pb = 0.0f, pa = 0.0f;

            if (blur > 0.0f && t->sh.a > 0.0f)
            { /* 影/グロー: パネルの外側だけ (透けるガラスの内側を濁らせない) */
                float cov = Clamp01(dist + 0.5f);
                float k = Clamp01(1.0f - fmaxf(dist, 0.0f) / blur);
                OVER(t->sh.r, t->sh.g, t->sh.b, t->sh.a * k * k * cov);
            }

            if (pass & 2)
            {
                float outer = Clamp01(0.5f - dist);
                if (outer > 0.0f)
                {
                    float u, v;
                    W2L(x + 0.5f, y + 0.5f, &u, &v);
                    float tv = Clamp01((v - v0) / lv), tu = Clamp01((u - u0) / lu);
                    float g = t->alongU ? tu : tv;

                    float fr = Lerpf(t->f0.r, t->f1.r, g);
                    float fgc = Lerpf(t->f0.g, t->f1.g, g);
                    float fb = Lerpf(t->f0.b, t->f1.b, g);
                    if (lift > 0.0f)
                    {
                        fr += (255.0f - fr) * lift * 0.10f;
                        fgc += (255.0f - fgc) * lift * 0.10f;
                        fb += (255.0f - fb) * lift * 0.10f;
                    }
                    OVER(fr, fgc, fb, Lerpf(fa0, fa1, g) * outer);

                    if (t->shine > 0.0f) /* 外側の半分に光沢 */
                    {
                        float sa = t->shine * Clamp01((tv - 0.35f) / 0.65f);
                        OVER(255.0f, 255.0f, 255.0f, sa * outer);
                    }
                    if (t->rim > 0.0f) /* 外側の縁のすぐ内側に細いハイライト */
                    {
                        float rc = Clamp01(0.5f * d + 0.5f - fabsf(dist + bw + 0.5f * d));
                        float w = Clamp01((tv - 0.55f) / 0.45f);
                        OVER(255.0f, 255.0f, 255.0f, t->rim * rc * w * outer);
                    }
                    if (bw > 0.0f)
                    {
                        float bc = Clamp01(0.5f - dist) - Clamp01(0.5f - (dist + bw));
                        if (bc > 0.0f)
                        {
                            const Col *c0 = accent ? accent : &t->b0, *c1 = accent ? accent : &t->b1;
                            OVER(Lerpf(c0->r, c1->r, g), Lerpf(c0->g, c1->g, g), Lerpf(c0->b, c1->b, g),
                                 Lerpf(c0->a, c1->a, g) * bc);
                        }
                    }
                }
            }

            if (pa <= 0.0f)
                continue;
            uint32_t *dp = &g_bits[y * g_W + x];
            uint32_t dv = *dp;
            float inv = 1.0f - pa;
            float oa = pa * 255.0f + ((dv >> 24) & 255) * inv;
            float orr = pr + ((dv >> 16) & 255) * inv;
            float og = pg + ((dv >> 8) & 255) * inv;
            float ob = pb + (dv & 255) * inv;
            if (oa > 255.0f)
                oa = 255.0f;
            if (orr > oa)
                orr = oa; /* premultiplied を超えないように */
            if (og > oa)
                og = oa;
            if (ob > oa)
                ob = oa;
            *dp = ((uint32_t)(oa + 0.5f) << 24) | ((uint32_t)(orr + 0.5f) << 16) |
                  ((uint32_t)(og + 0.5f) << 8) | (uint32_t)(ob + 0.5f);
        }
}

/* サークル用パネル: DrawPanel と同じ見た目 (影/塗り/光沢/縁ハイライト/枠線) を、円弧に沿って曲げた角丸の帯に描く。
 *   s0..s1 : 弧の範囲 (基準半径 g_Rref での弧長。真上が0) / ro0..ro1 : 帯の内径・外径 (中心からの半径)
 *   pass   : 1=影だけ 2=本体だけ 3=両方 */
static void DrawArcPanel(float s0, float s1, float ro0, float ro1, float rad, const Theme *t, float aMul, int pass)
{
    float d = g_dpi;
    float blur = (pass & 1) ? t->shBlur * d : 0.0f;
    float ext = blur + 3.0f;
    float hs = (s1 - s0) * 0.5f, sc = (s0 + s1) * 0.5f;
    float rc = (ro0 + ro1) * 0.5f, hh = (ro1 - ro0) * 0.5f;
    float rin = fmaxf(ro0 - ext, 0.0f), rout = ro1 + ext;
    float rin2 = rin * rin, rout2 = rout * rout;
    float bw = t->bw * d;
    float lt = fmaxf(ro1 - ro0, 1.0f), ls = fmaxf(s1 - s0, 1.0f);
    float fa0 = fminf(t->f0.a * aMul, 1.0f), fa1 = fminf(t->f1.a * aMul, 1.0f);

    float L, T, R, B;
    BOOL ring = IsRing();
    LRect(g_cU - rout, ring ? g_cV - rout : g_cV - ext - hh, g_cU + rout, g_cV + rout, &L, &T, &R, &B);
    int x0 = ClampI((int)L, 0, g_W), x1 = ClampI((int)R + 1, 0, g_W);
    int y0 = ClampI((int)T, 0, g_H), y1 = ClampI((int)B + 1, 0, g_H);

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float u, v;
            W2L(x + 0.5f, y + 0.5f, &u, &v);
            float dx = u - g_cU, dy = v - g_cV;
            float r2 = dx * dx + dy * dy;
            if (r2 < rin2 || r2 > rout2)
                continue;
            float rho = sqrtf(r2);
            float s, dist;
            if (ring)
            { /* 全円: 端のない完全な円環 (半径方向の距離だけで決まる) */
                s = 0.0f;
                dist = fabsf(rho - rc) - hh;
            }
            else
            {
                s = (HALF_PI - atan2f(dy, dx)) * g_Rref;
                float qx = fabsf(s - sc) - hs + rad, qy = fabsf(rho - rc) - hh + rad;
                float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f);
                dist = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - rad; /* 曲げた角丸矩形SDF */
            }
            if (dist > blur + 1.0f)
                continue;

            float pr = 0.0f, pg = 0.0f, pb = 0.0f, pa = 0.0f;

            if (blur > 0.0f && t->sh.a > 0.0f)
            {
                float cov = Clamp01(dist + 0.5f);
                float k = Clamp01(1.0f - fmaxf(dist, 0.0f) / blur);
                OVER(t->sh.r, t->sh.g, t->sh.b, t->sh.a * k * k * cov);
            }

            if (pass & 2)
            {
                float outer = Clamp01(0.5f - dist);
                if (outer > 0.0f)
                {
                    float tv = Clamp01((rho - ro0) / lt);
                    float tu = ring ? 0.5f + 0.5f * dx / fmaxf(rho, 1.0f) /* 全円: 継ぎ目が出ないよう左右方向のグラデーション */
                                    : Clamp01((s - s0) / ls);
                    float g = t->alongU ? tu : tv; /* tv: 内側(端側)0 -> 外側1 */

                    float fr = Lerpf(t->f0.r, t->f1.r, g);
                    float fgc = Lerpf(t->f0.g, t->f1.g, g);
                    float fb = Lerpf(t->f0.b, t->f1.b, g);
                    OVER(fr, fgc, fb, Lerpf(fa0, fa1, g) * outer);

                    if (t->shine > 0.0f)
                    {
                        float sa = t->shine * Clamp01((tv - 0.35f) / 0.65f);
                        OVER(255.0f, 255.0f, 255.0f, sa * outer);
                    }
                    if (t->rim > 0.0f)
                    {
                        float rcv = Clamp01(0.5f * d + 0.5f - fabsf(dist + bw + 0.5f * d));
                        float w = Clamp01((tv - 0.55f) / 0.45f);
                        OVER(255.0f, 255.0f, 255.0f, t->rim * rcv * w * outer);
                    }
                    if (bw > 0.0f)
                    {
                        float bc = Clamp01(0.5f - dist) - Clamp01(0.5f - (dist + bw));
                        if (bc > 0.0f)
                            OVER(Lerpf(t->b0.r, t->b1.r, g), Lerpf(t->b0.g, t->b1.g, g), Lerpf(t->b0.b, t->b1.b, g),
                                 Lerpf(t->b0.a, t->b1.a, g) * bc);
                    }
                }
            }

            if (pa <= 0.0f)
                continue;
            uint32_t *dp = &g_bits[y * g_W + x];
            uint32_t dv = *dp;
            float inv = 1.0f - pa;
            float oa = pa * 255.0f + ((dv >> 24) & 255) * inv;
            float orr = pr + ((dv >> 16) & 255) * inv;
            float og = pg + ((dv >> 8) & 255) * inv;
            float ob = pb + (dv & 255) * inv;
            if (oa > 255.0f)
                oa = 255.0f;
            if (orr > oa)
                orr = oa;
            if (og > oa)
                og = oa;
            if (ob > oa)
                ob = oa;
            *dp = ((uint32_t)(oa + 0.5f) << 24) | ((uint32_t)(orr + 0.5f) << 16) |
                  ((uint32_t)(og + 0.5f) << 8) | (uint32_t)(ob + 0.5f);
        }
}
#undef OVER

/* モダン用: アイコンごとのタイル。ホバーで少し明るく、前面のアプリは枠をアクセント色に */
static void DrawTile(const Item *it, DWORD now, int pass)
{
    if (it->sz < 4.0f)
        return;
    const Theme *t = Th();
    float half = it->sz * 0.5f;
    float uc, vc;
    ItemCenterUV(it, now, &uc, &vc);
    float hot = (g_maxScale > 1.001f) ? Clamp01((it->scale - 1.0f) / (g_maxScale - 1.0f)) : 0.0f;
    static const Col acc = {96.0f, 165.0f, 255.0f, 0.85f};
    DrawPanel(uc - half, vc - half, uc + half, vc + half, it->sz * 0.26f, t,
              OpacityMul(), hot, (it->fg && it->nwin > 0) ? &acc : NULL, pass);
}

static void DrawItem(const Item *it, DWORD now)
{
    int dw = (int)(it->sz * Th()->iconScale + 0.5f);
    if (dw < 4)
        return;
    float uc, vc, xc, yc;
    ItemCenterUV(it, now, &uc, &vc);
    L2W(uc, vc, &xc, &yc);
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

/* ピル (カプセル) 状の目印。長さ len、厚み th、向き (ax, ay) はウィンドウ座標の単位ベクトル */
static void DrawCapsule(float cx, float cy, float len, float th, float ax, float ay, const Col *c)
{
    float hl = fmaxf(len * 0.5f - th * 0.5f, 0.0f);
    float e = fmaxf(len, th) * 0.5f + 2.0f;
    int x0 = (int)(cx - e), x1 = (int)(cx + e) + 1, y0 = (int)(cy - e), y1 = (int)(cy + e) + 1;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float al = dx * ax + dy * ay, pe = -dx * ay + dy * ax;
            float a = fmaxf(fabsf(al) - hl, 0.0f);
            float cov = Clamp01(0.5f - (sqrtf(a * a + pe * pe) - th * 0.5f));
            BlendPx(x, y, c->r, c->g, c->b, cov * c->a);
        }
}

/* 起動中のアプリ: アイコンの画面端側 (サークルでは内側) に目印を1つ (形はテーマごと) */
static void DrawIndicator(const Item *it)
{
    if (it->nwin <= 0 && !IsPending(it))
        return;
    const Theme *t = Th();
    const Col *c = &t->indC;
    float d = g_dpi, x, y;
    if (t->ind == 1)
    { /* ピル: 前面のアプリは長く */
        float ax = IsVert() ? 0.0f : 1.0f, ay = IsVert() ? 1.0f : 0.0f;
        if (IsCircle())
            PolarW(g_ro0 + g_pady * 0.42f, it->ang, &x, &y, &ax, &ay); /* 円弧の接線方向に寝かせる */
        else
            L2W(it->cx, g_margin + g_pady * 0.42f, &x, &y);
        DrawCapsule(x, y, ((it->fg && it->nwin > 0) ? 16.0f : 6.0f) * d, 3.2f * d, ax, ay, c);
        return;
    }
    if (IsCircle())
        PolarW(g_ro0 + g_pady * 0.38f, it->ang, &x, &y, NULL, NULL);
    else
        L2W(it->cx, g_margin + g_pady * 0.38f, &x, &y);
    float r = t->indR * d;
    if (t->halo)
        DrawDot(x, y, r + 0.9f * d, 0.f, 0.f, 0.f, 0.22f);
    if (t->ind == 2)
    { /* グロー付き */
        DrawDot(x, y, r * 2.8f, c->r, c->g, c->b, 0.12f);
        DrawDot(x, y, r * 1.8f, c->r, c->g, c->b, 0.22f);
    }
    DrawDot(x, y, r, c->r, c->g, c->b, c->a);
}

/* 固定項目と実行中項目の間の細い区切り線 (テーマが有効にしたときだけ) */
static void DrawDivider(void)
{
    const Theme *t = Th();
    int np = NPinned();
    if (t->divider <= 0.0f || t->tiles || np <= 0 || np >= g_count)
        return;
    const Item *a = &g_items[np - 1], *b = &g_items[np];
    if (IsCircle())
    { /* 弧の途中に、半径方向の細い線 */
        float s = ((a->cx + a->sz * 0.5f) + (b->cx - b->sz * 0.5f)) * 0.5f - g_LW * 0.5f;
        float px, py, tx, ty;
        PolarW(g_baseline + g_base * 0.5f, HALF_PI - s / g_Rref - RotOf(), &px, &py, &tx, &ty);
        Col dc = {255.0f, 255.0f, 255.0f, t->divider};
        DrawCapsule(px, py, g_base * 0.76f, ceilf(g_dpi), -ty, tx, &dc); /* 接線に直交 = 半径方向 */
        return;
    }
    float u = ((a->cx + a->sz * 0.5f) + (b->cx - b->sz * 0.5f)) * 0.5f;
    float v0 = g_baseline + g_base * 0.12f, v1 = g_baseline + g_base * 0.88f;
    float xa, ya, xb, yb;
    L2W(u, v0, &xa, &ya);
    L2W(u, v1, &xb, &yb);
    int w = (int)ceilf(g_dpi), X0, X1, Y0, Y1;
    if (!IsVert())
    {
        X0 = (int)floorf(xa - w * 0.5f + 0.5f);
        X1 = X0 + w;
        Y0 = (int)fminf(ya, yb);
        Y1 = (int)fmaxf(ya, yb);
    }
    else
    {
        Y0 = (int)floorf(ya - w * 0.5f + 0.5f);
        Y1 = Y0 + w;
        X0 = (int)fminf(xa, xb);
        X1 = (int)fmaxf(xa, xb);
    }
    for (int y = Y0; y < Y1; y++)
        for (int x = X0; x < X1; x++)
            BlendPx(x, y, 255.f, 255.f, 255.f, t->divider);
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
    float L, T, R, B;
    if (IsCircle())
    { /* サークル: 円の内側の空きに表示 */
        float xc, yc;
        L2W(g_cU, g_cV + (IsRing() ? 0.0f : g_ro0 * 0.45f), &xc, &yc); /* 全円は円の中心 */
        L = xc - pw * 0.5f;
        T = yc - ph * 0.5f;
        L = fminf(fmaxf(L, 2.0f), fmaxf(g_W - pw - 2.0f, 2.0f));
        T = fminf(fmaxf(T, 2.0f), fmaxf(g_H - ph - 2.0f, 2.0f));
        R = L + pw;
        B = T + ph;
    }
    else
    {
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
        LRect(u0, v0, u1, v1, &L, &T, &R, &B);
    }
    DrawRRect(L, T, R, B, ph * Th()->labelRad, &Th()->label);

    const Col *lt = &Th()->labelTxt;
    int tx = (int)(L + 10 * d), ty = (int)(T + 5 * d);
    for (int y = 0; y < g_lblH; y++)
        for (int x = 0; x < g_lblW; x++)
        {
            uint8_t m = g_lblMask[y * g_lblW + x];
            if (m)
                BlendPx(tx + x, ty + y, lt->r, lt->g, lt->b, m / 255.0f * lt->a);
        }
}

static void Render(void)
{
    memset(g_bits, 0, (size_t)g_W * g_H * 4);
    Layout();

    /* アルファ1のほぼ透明領域: 0だとマウスが貫通するためホバー検知用に塗る */
    float L, T, R, B;
    if (IsCircle())
    { /* サークル: 帯とその外側 (半円の環) だけをホバー領域にする。円の内側の空きは下のウィンドウを操作できるよう貫通させる */
        float hr2 = g_hoverV * g_hoverV, lr = g_ro0 - 8.0f * g_dpi;
        float lr2 = lr > 0.0f ? lr * lr : 0.0f;
        for (int y = 0; y < g_H; y++)
            for (int x = 0; x < g_W; x++)
            {
                float u, v;
                W2L(x + 0.5f, y + 0.5f, &u, &v);
                float dx = u - g_cU, dy = v - g_cV, r2 = dx * dx + dy * dy;
                if ((dy >= 0.0f || IsRing()) && r2 >= lr2 && r2 <= hr2)
                    g_bits[y * g_W + x] = 0x01000000u;
            }
    }
    else
    {
        LRect(0, 0, (float)g_LW, g_hoverV, &L, &T, &R, &B);
        int hx0 = ClampI((int)floorf(L), 0, g_W), hx1 = ClampI((int)ceilf(R), 0, g_W);
        int hy0 = ClampI((int)floorf(T), 0, g_H), hy1 = ClampI((int)ceilf(B), 0, g_H);
        for (int y = hy0; y < hy1; y++)
            for (int x = hx0; x < hx1; x++)
                g_bits[y * g_W + x] = 0x01000000u;
    }

    const Theme *th = Th();
    DWORD now = GetTickCount();
    float r = th->rad * g_dpi, hh = (g_barV1 - g_barV0) * 0.5f;
    if (r > hh)
        r = hh;
    if (th->tiles)
    { /* タイル: 影を先にまとめて描き、隣のタイルに影が被らないようにする */
        for (int i = 0; i < g_count; i++)
            DrawTile(&g_items[i], now, 1);
    }
    else if (IsCircle())
    { /* サークル: 全デザイン共通で、円弧に沿った帯に描く (標準もここ。見た目は従来のバーと同じ配色) */
        DrawArcPanel(g_barL - g_LW * 0.5f, g_barR - g_LW * 0.5f, g_barV0, g_barV1, r, th, OpacityMul(), 3);
        DrawDivider();
    }
    else if (g_style == 0)
    { /* クラシック: 従来どおり */
        LRect(g_barL, g_barV0, g_barR, g_barV1, &L, &T, &R, &B);
        DrawRRect(L, T, R, B, r, &g_barStyle);
    }
    else
    {
        DrawPanel(g_barL, g_barV0, g_barR, g_barV1, r, th, OpacityMul(), 0.0f, NULL, 3);
        DrawDivider();
    }

    for (int i = 0; i < g_count; i++)
    {
        if (th->tiles)
            DrawTile(&g_items[i], now, 2);
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
#define WM_REMOVEDOCK (WM_APP + 2)
typedef struct
{
    wchar_t spec[MAX_PATH * 2];
    wchar_t path[MAX_PATH * 2];
    HWND owner; /* 起動を頼んだドックのウィンドウ (別スレッドから失敗を知らせる先) */
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
    else if (!PostMessageW(rq->owner, WM_LAUNCHFAIL, 0, (LPARAM)rq))
        free(rq);
    return 0;
}

static void Launch(int idx)
{
    DWORD now = GetTickCount();
    if (g_items[idx].launchTick && now - g_items[idx].launchTick < 2000)
        return; /* 起動待ちの連打で二重起動しない */
    LaunchReq *rq = (LaunchReq *)malloc(sizeof(LaunchReq));
    if (rq)
        rq->owner = g_hwnd;
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
    HMENU set = CreatePopupMenu(), mo = CreatePopupMenu(), mp = CreatePopupMenu(), mt = CreatePopupMenu(), mz = CreatePopupMenu();
    AppendMenuW(mz, MF_STRING | (!g_zBottom ? MF_CHECKED : 0), 60, L"最前列 (常に手前)");
    AppendMenuW(mz, MF_STRING | (g_zBottom ? MF_CHECKED : 0), 61, L"最奥 (デスクトップの上・他のウィンドウの背面)");
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
    HMENU mk = CreatePopupMenu();
    for (int i = 0; i < N_DOCKTYPES; i++)
        AppendMenuW(mk, MF_STRING | (g_dockType == i ? MF_CHECKED : 0), 50 + i, g_dockTypeNames[i]);
    HMENU ms = CreatePopupMenu();
    for (int i = 0; i < N_STYLES; i++)
        AppendMenuW(ms, MF_STRING | (g_style == i ? MF_CHECKED : 0), 40 + i, g_themes[i].name);
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mk, L"ドックの形");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)ms, L"デザイン");
    AppendMenuW(set, MF_SEPARATOR, 0, NULL);
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mo, L"向き");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mp, L"位置");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mz, L"重なり順");
    AppendMenuW(set, MF_SEPARATOR, 0, NULL);
    AppendMenuW(set, MF_STRING | (g_autohide ? MF_CHECKED : 0), 6, L"自動的に隠す");
    AppendMenuW(set, MF_POPUP, (UINT_PTR)mt, L"隠れるまでの時間");
    AppendMenuW(menu, MF_STRING | (Settings_LauncherEnabled() ? 0 : MF_GRAYED), 30, L"ランチャーを開く (F11)");
    AppendMenuW(menu, MF_STRING, 31, L"アプリ設定…");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)set, L"設定");
    AppendMenuW(menu, MF_STRING | (g_nDocks < MAX_DOCKS ? 0 : MF_GRAYED), 80, L"ドックを追加");
    AppendMenuW(menu, MF_STRING | (g_nDocks > 1 ? 0 : MF_GRAYED), 81, L"このドックを削除");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
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
    if (g_zBottom) /* メニュー表示で前面に出たので、最奥へ戻す */
        ApplyZOrder(0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

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
    case 30:
        Launcher_Toggle();
        break;
    case 31:
        Settings_OpenPage(1);
        break;
    case 80:
        Dock_Add();
        break;
    case 81:
        PostMessageW(hwnd, WM_REMOVEDOCK, 0, 0); /* 自分のウィンドウを今ここで壊さない */
        break;
    case 60:
    case 61:
        g_zBottom = (cmd == 61);
        ApplyLayoutChange();
        break;
    case 6:
        g_autohide = !g_autohide;
        SaveConfig();
        break;
    case 10:
        if (g_edge >= 2)
        {
            g_edge = 0;
            g_free = FALSE;
            ApplyLayoutChange();
        }
        break;
    case 11:
        if (g_edge < 2)
        {
            g_edge = 3;
            g_free = FALSE;
            ApplyLayoutChange();
        }
        break;
    case 12:
    case 13:
    case 14:
    case 15:
        g_edge = cmd - 12;
        g_free = FALSE; /* 端に固定し直す */
        ApplyLayoutChange();
        break;
    case 16:
        g_frac = 0.5f;
        g_free = FALSE;
        ApplyLayoutChange();
        break;
    default:
        if (cmd >= 20 && cmd < 20 + N_HIDE_CHOICES)
        {
            g_hideMs = g_hideChoices[cmd - 20];
            SaveConfig();
        }
        else if (cmd >= 50 && cmd < 50 + N_DOCKTYPES)
        {
            g_dockType = cmd - 50;
            ApplyLayoutChange();
        }
        else if (cmd >= 40 && cmd < 40 + N_STYLES)
        {
            g_style = cmd - 40;
            ApplyLayoutChange(); /* 寸法がテーマごとに違うので、保存 + 再レイアウト */
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
    int sW = g_W, sH = g_H, sEdge = g_edge, sType = g_dockType;
    float sBase = g_baseline;
    g_bits = g_ghostBits;
    g_W = gw;
    g_H = gh;
    g_edge = 0;
    g_dockType = 0; /* ゴーストは常に水平配置で描く */

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
    g_dockType = sType;
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
        if (off + c[i] < MouseAlong())
            k++;
    g_dropIdx = k;
    EnsureTimer();
}

typedef struct
{
    IDropTargetVtbl *lpVtbl;
    HWND hwnd;
} MyDT; /* ドックごとのドロップ先 (どのドックへのドロップか判別するため hwnd を持つ) */

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
    SelectByHwnd(((MyDT *)t)->hwnd);
    (void)ks;
    g_dropOk = DropHasFiles(d);
    if (g_dropOk)
        DropUpdate(pt);
    *eff = g_dropOk ? DropEffect(*eff) : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DT_DragOver(IDropTarget *t, DWORD ks, POINTL pt, DWORD *eff)
{
    SelectByHwnd(((MyDT *)t)->hwnd);
    (void)ks;
    if (g_dropOk)
        DropUpdate(pt);
    *eff = g_dropOk ? DropEffect(*eff) : DROPEFFECT_NONE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DT_DragLeave(IDropTarget *t)
{
    SelectByHwnd(((MyDT *)t)->hwnd);
    g_dropActive = FALSE;
    g_dropOk = FALSE;
    g_hover = FALSE;
    g_leaveTick = GetTickCount();
    EnsureTimer();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE DT_Drop(IDropTarget *t, IDataObject *d, DWORD ks, POINTL pt, DWORD *eff)
{
    SelectByHwnd(((MyDT *)t)->hwnd);
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

/* ドックのウィンドウを作る (ドロップ先の登録まで) */
static void CreateDockWindow(DockState *d)
{
    DockState *prev = gDock;
    gDock = d; /* 作成中に届くメッセージの処理対象 */
    d->hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"PromDockWnd", L"PromDock", WS_POPUP, 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    MyDT *dt = (MyDT *)calloc(1, sizeof(MyDT));
    if (dt)
    {
        dt->lpVtbl = &g_dtVtbl;
        dt->hwnd = d->hwnd;
        d->dt = dt;
        RegisterDragDrop(d->hwnd, (IDropTarget *)dt);
    }
    d->leaveTick = GetTickCount();
    gDock = prev ? prev : d;
}

/* Ctrl+ドラッグ: 掴んだ位置を保ったまま、画面上の好きな場所へ制限なく移動 (端への吸着なし) */
static void MoveDockToCursor(void)
{
    POINT pt;
    GetCursorPos(&pt);
    RECT wa;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    float fw = fmaxf((float)(wa.right - wa.left), 1.0f), fh = fmaxf((float)(wa.bottom - wa.top), 1.0f);
    int wx = g_moveWX + (pt.x - g_moveStart.x), wy = g_moveWY + (pt.y - g_moveStart.y);
    float fx = ((float)wx + g_W * 0.5f - wa.left) / fw;
    float fy = ((float)wy + g_H * 0.5f - wa.top) / fh;
    if (!g_free || fabsf(fx - g_fx) > 0.0002f || fabsf(fy - g_fy) > 0.0002f)
    {
        g_free = TRUE;
        g_fx = fx;
        g_fy = fy;
        g_slide = 0.0f;
        ResizeDock();
        Render();
        /* 作業領域の外へ出過ぎて ResizeDock が位置を丸めた場合は、丸めた位置を保存値にする */
        g_fx = ((float)g_posX + g_W * 0.5f - wa.left) / fw;
        g_fy = ((float)g_posY0 + g_H * 0.5f - wa.top) / fh;
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

static BOOL Dock_Remove(int i);
BOOL Dock_Add(void);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SelectByHwnd(hwnd); /* このウィンドウのドックを処理対象にする */
    switch (msg)
    {
    case WM_REMOVEDOCK: /* 右クリックメニューの「このドックを削除」 (メニューの処理が終わってから実行) */
        for (int di = 0; di < g_nDocks; di++)
            if (g_docks[di]->hwnd == hwnd)
            {
                Dock_Remove(di);
                break;
            }
        return 0;
    case WM_MOUSEMOVE:
        if (g_moving)
        {
            MoveDockToCursor();
            return 0;
        }
        if (g_rotating)
        { /* Shift+ドラッグ: カーソルが円の中心まわりに動いた角度だけ回す */
            W2L((float)(short)LOWORD(lp), (float)(short)HIWORD(lp), &g_mouseU, &g_mouseV);
            float a = ArcAngle(g_mouseU - g_cU, g_mouseV - g_cV);
            g_rotTarget += WrapPi(a - g_rotLast);
            g_rot = g_rotTarget;
            g_rotLast = a;
            g_hover = TRUE;
            Render();
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
            BOOL out = OutsideDock();
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
            if (g_zBottom && !g_menuOpen && !g_moving)
                ApplyZOrder(0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); /* 最奥の位置を保つ */
            int r = RefreshRunning();
            if (r == 2)
            {
                ResizeDock();
                Render();
            }
            else if (r == 1 || iconFixed || AnyPending())
                Render();
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (GetKeyState(VK_CONTROL) < 0)
        { /* Ctrl+ドラッグ: ドックを移動 */
            g_moving = TRUE;
            g_pressed = -1;
            g_dragged = TRUE;
            GetCursorPos(&g_moveStart);
            g_moveWX = g_posX;
            g_moveWY = g_posY0;
            SetCapture(hwnd);
            return 0;
        }
        if (IsRing() && GetKeyState(VK_SHIFT) < 0)
        { /* 全円: Shift+ドラッグでダイヤルのように回す */
            float u, v;
            W2L((float)(short)LOWORD(lp), (float)(short)HIWORD(lp), &u, &v);
            g_rotating = TRUE;
            g_pressed = -1;
            g_dragged = TRUE;
            g_rotLast = ArcAngle(u - g_cU, v - g_cV);
            g_rotTarget = g_rot;
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

    case WM_MOUSEWHEEL:
        if (IsRing() && g_count > 0 && !g_rotating)
        { /* 全円: ホイール1ノッチでアイコン1個ぶん回る (下へ回すと時計回り) */
            float delta = (float)(short)HIWORD(wp);
            g_rotTarget -= (g_base + g_gap) / g_Rref * delta / 120.0f;
            EnsureTimer();
            return 0;
        }
        break;

    case WM_LBUTTONUP:
    {
        if (g_rotating)
        {
            g_rotating = FALSE;
            g_dragged = FALSE;
            ReleaseCapture();
            EnsureTimer();
            return 0;
        }
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
        if (g_rotating)
        {
            g_rotating = FALSE;
            g_dragged = FALSE;
            EnsureTimer();
        }
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

    case WM_DESTROY: /* 終了は Dock_Quit が PostQuitMessage する (ドック1つの削除では終了しない) */
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 設定(setting.c)からドックの有効/無効を切り替える。無効の間は隠して監視も止める */
void Dock_SetEnabled(BOOL on)
{
    g_dockEnabled = on ? TRUE : FALSE;
    DockState *keep = gDock;
    for (int di = 0; di < g_nDocks; di++)
    {
        gDock = g_docks[di];
        if (!g_hwnd)
            continue;
        if (on)
        {
            RefreshRunning();
            ResizeDock();
            Render();
            ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
            SetTimer(g_hwnd, TIMER_POLL, POLL_MS, NULL);
            if (g_autohide)
                EnsureTimer();
        }
        else
        {
            KillTimer(g_hwnd, TIMER_POLL);
            ShowWindow(g_hwnd, SW_HIDE);
        }
    }
    gDock = keep ? keep : g_docks[0];
}

/* ------------------------------------------------------------------ */
/* 設定(setting.c)から使うドックの設定。値は即座に反映され、                    */
/* 確定したら Dock_SaveSettings() で promdock.txt に保存する。                */
/* 初回設定中 (ウィンドウ作成前) に呼ばれても安全 (変数を書き換えるだけ)。      */
/* ------------------------------------------------------------------ */
static void DockRelayout(void)
{
    if (!g_hwnd)
        return;
    g_slide = 0.0f;
    ResizeDock();
    Render();
}

void Dock_SaveSettings(void)
{
    if (g_hwnd && g_cfgPath[0])
        SaveConfig();
}

int Dock_GetZOrder(void) { return g_zBottom ? 1 : 0; } /* 0:最前列 1:最奥 */
void Dock_SetZOrder(int z)
{
    UseEdit();
    g_zBottom = z != 0;
    if (g_hwnd)
        ApplyZOrder(0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

int Dock_GetEdge(void)
{
    UseEdit();
    return g_edge;
}
void Dock_SetEdge(int e)
{
    UseEdit();
    g_edge = ClampI(e, 0, 3);
    g_free = FALSE;
    DockRelayout();
}

void Dock_ResetPosition(void)
{
    UseEdit();
    g_frac = 0.5f;
    g_free = FALSE;
    DockRelayout();
}

BOOL Dock_GetAutoHide(void)
{
    UseEdit();
    return g_autohide;
}
void Dock_SetAutoHide(BOOL on)
{
    UseEdit();
    g_autohide = on ? TRUE : FALSE;
    if (g_hwnd && IsWindowVisible(g_hwnd))
        EnsureTimer();
}

int Dock_GetHideMs(void)
{
    UseEdit();
    return g_hideMs;
}
void Dock_SetHideMs(int ms)
{
    UseEdit();
    g_hideMs = ClampI(ms, 0, 60000);
}

int Dock_GetIconSize(void)
{
    UseEdit();
    return (int)(g_baseSize + 0.5f);
}
void Dock_SetIconSize(int px)
{
    UseEdit();
    g_baseSize = (float)ClampI(px, 32, 96);
    DockRelayout();
}

int Dock_GetZoom(void)
{
    UseEdit();
    return (int)(g_maxScale * 100.0f + 0.5f);
}
void Dock_SetZoom(int pct)
{
    UseEdit();
    g_maxScale = ClampI(pct, 100, 200) / 100.0f;
    DockRelayout();
}

int Dock_GetOpacity(void)
{
    UseEdit();
    return (int)(g_barStyle.fa * 100.0f + 0.5f);
}
void Dock_SetOpacity(int pct)
{
    UseEdit();
    g_barStyle.fa = ClampI(pct, 30, 100) / 100.0f;
    if (g_hwnd)
        Render();
}

int Dock_GetStyle(void)
{
    UseEdit();
    return g_style;
}
void Dock_SetStyle(int s)
{
    UseEdit();
    g_style = ClampI(s, 0, N_STYLES - 1);
    DockRelayout();
}
int Dock_StyleCount(void)
{
    UseEdit();
    return N_STYLES;
}

int Dock_GetType(void)
{
    UseEdit();
    return g_dockType;
}
void Dock_SetType(int t)
{
    UseEdit();
    g_dockType = ClampI(t, 0, N_DOCKTYPES - 1);
    DockRelayout();
}
int Dock_TypeCount(void)
{
    UseEdit();
    return N_DOCKTYPES;
}
const wchar_t *Dock_TypeName(int i)
{
    UseEdit();
    return g_dockTypeNames[ClampI(i, 0, N_DOCKTYPES - 1)];
}
const wchar_t *Dock_StyleName(int i)
{
    UseEdit();
    return g_themes[ClampI(i, 0, N_STYLES - 1)].name;
}

/* タスクトレイ/設定ウィンドウからの終了用 */
void Dock_Quit(void)
{
    for (int di = 0; di < g_nDocks; di++)
        if (g_docks[di] && g_docks[di]->hwnd)
            DestroyWindow(g_docks[di]->hwnd);
    PostQuitMessage(0);
}

/* ------------------------------------------------------------------ */
/* ドックの追加・削除・編集対象の切り替え (最大 MAX_DOCKS 個)                */
/* ------------------------------------------------------------------ */
int Dock_Count(void) { return g_nDocks; }
int Dock_GetEdit(void) { return g_editDock; }
void Dock_SetEdit(int i)
{
    g_editDock = ClampI(i, 0, g_nDocks - 1);
    UseEdit();
}

static void DestroyDock(DockState *d) /* 配列から外したあとで呼ぶ */
{
    HWND h = d->hwnd;
    d->hwnd = NULL;
    if (h)
    {
        RevokeDragDrop(h);
        DestroyWindow(h);
    }
    for (int i = 0; i < d->count; i++)
        FreeItem(&d->items[i]);
    if (d->memdc)
    {
        SelectObject(d->memdc, d->oldBmp);
        DeleteObject(d->dib);
        DeleteDC(d->memdc);
    }
    free(d->dt);
    free(d);
}

BOOL Dock_Add(void)
{
    if (g_nDocks >= MAX_DOCKS)
        return FALSE;
    DockState *prev = gDock;
    DockState *d = NewDock();
    if (!d)
        return FALSE;
    DockState *src = g_docks[0]; /* デザインやサイズは1つ目のドックを引き継ぐ。アイコン一覧は空 (独立) */
    d->style = src->style;
    d->dockType = src->dockType;
    d->baseSize = src->baseSize;
    d->maxScale = src->maxScale;
    d->barStyle = src->barStyle;
    d->hideMs = src->hideMs;
    BOOL used[4] = {FALSE, FALSE, FALSE, FALSE};
    for (int i = 0; i < g_nDocks; i++)
        used[g_docks[i]->edge & 3] = TRUE;
    int e = 0;
    while (e < 3 && used[e])
        e++;
    d->edge = e; /* まだ使っていない端に置く (下 > 上 > 左 > 右) */
    g_docks[g_nDocks++] = d;
    g_editDock = g_nDocks - 1;
    if (g_cfgPath[0])
    { /* 起動処理が済んでいれば、すぐにウィンドウを作って表示する */
        CreateDockWindow(d);
        gDock = d;
        RefreshRunning();
        ResizeDock();
        Render();
        if (g_dockEnabled)
        {
            ShowWindow(d->hwnd, SW_SHOWNOACTIVATE);
            SetTimer(d->hwnd, TIMER_POLL, POLL_MS, NULL);
        }
        SaveConfig();
    }
    gDock = prev ? prev : d;
    return TRUE;
}

static BOOL Dock_Remove(int i)
{
    if (g_nDocks <= 1 || i < 0 || i >= g_nDocks)
        return FALSE;
    DockState *d = g_docks[i];
    for (int k = i; k < g_nDocks - 1; k++)
        g_docks[k] = g_docks[k + 1];
    g_docks[--g_nDocks] = NULL;
    if (g_editDock > i)
        g_editDock--;
    g_editDock = ClampI(g_editDock, 0, g_nDocks - 1);
    gDock = g_docks[0]; /* d は解放するので、別のドックへ切り替えておく */
    DestroyDock(d);
    gDock = g_docks[g_editDock];
    if (g_cfgPath[0])
        SaveConfig();
    return TRUE;
}
BOOL Dock_RemoveAt(int i) { return Dock_Remove(i); }

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

    /* 初回はここで設定ウィンドウが開き、ドック/ランチャーを有効にするか選ぶ。
       FALSE: 二重起動 (動作中のアプリの設定を開いた) か、初回設定で終了が選ばれた */
    DocksInitDefault(); /* 設定画面がドックの値を触っても安全なように、先に1つ目のドックを用意 */
    if (!Settings_Init(hInst))
    {
        OleUninitialize();
        return 0;
    }

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

    Launcher_Init(hInst);
    SearchBar_Init(hInst);
    PhotoFrame_Init(hInst);
    Clock_Init(hInst);
    LoadConfig(); /* ドックの数・設定・アイコン一覧を読む */
    for (int di = 0; di < g_nDocks; di++)
    {
        if (!g_docks[di]->hwnd)
            CreateDockWindow(g_docks[di]);
        gDock = g_docks[di];
        g_leaveTick = GetTickCount();
        RefreshRunning();
        ResizeDock();
        Render();
    }
    gDock = g_docks[0];
    Dock_SetEnabled(Settings_DockEnabled()); /* 表示 + 実行中アプリの監視タイマー開始 (無効なら隠す) */

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0)
    {
        if (Settings_PreTranslate(&m))
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    Settings_Shutdown();
    for (int di = 0; di < g_nDocks; di++)
        for (int i = 0; i < g_docks[di]->count; i++)
            FreeItem(&g_docks[di]->items[i]);
    free(g_lblMask);
    DestroyGhost();
    Clock_Shutdown();
    PhotoFrame_Shutdown();
    SearchBar_Shutdown();
    Launcher_Shutdown();
    OleUninitialize();
    return 0;
}