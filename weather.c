/*
 * weather.c - PromDock のデスクトップ天気ウィジェット (promdock.c / launcher.c / setting.c / searchbar.c / photoframe.c / clock.c と同じ exe にビルド)
 *
 *     cl ... /Febuild\PromDock.exe promdock.c launcher.c setting.c searchbar.c photoframe.c clock.c weather.c /link /SUBSYSTEM:WINDOWS
 *
 * 機能:
 *   - デスクトップに半透明の天気・気温ウィジェットを置く (アプリ設定の「天気」ページで表示のオン/オフ)
 *       天気データは Open-Meteo (https://open-meteo.com / 無料・APIキー不要) から WinHTTP で取得
 *       取得は別スレッド。30分ごとに自動更新 (失敗したら2分後に再試行)。前回の結果は weather.ini にキャッシュ
 *   - 場所: ドロップダウンから都市を選ぶ / 「緯度・経度を指定…」で好きな場所を指定
 *   - デザイン (variant): 0:カード / 1:細字(大・背景なし) / 2:予報(5日) / 3:縦並び / 4:ミニ / 5:週間 / 6:スカイ(青) / 7:アイコン(大・背景なし) / 8:巨大(気温のみ・背景なし)
 *       天気アイコンは画像を使わず、コードで描画 (晴れ・月・くもり・霧・雨・雪・雷雨)
 *   - 気温の単位: ℃ / ℉
 *   - 検索バー・画像・時計と同じく、最奥 (壁紙のすぐ上=デスクトップの上) に置く。ドラッグ移動可 (ロック可)、位置は自動保存
 *   - 右クリック: デザイン / 単位 / 場所 / 今すぐ更新 / ドラッグ移動 / 天気を消す / アプリ設定
 *   - 設定は exe と同じフォルダの weather.ini に保存
 *       [Weather]
 *       enabled=1
 *       variant=0      (0〜8)
 *       city=2         (都市の番号。最後の番号 = 緯度・経度の指定)
 *       lat=35.6895    (city が「指定」のときの緯度・経度)
 *       lon=139.6917
 *       unit=0         (0:℃ / 1:℉)
 *       size=100       (%)
 *       opacity=78     (%)
 *       movable=1
 *       x=40
 *       y=40
 *
 * 外部 (promdock.c / setting.c) から呼ぶ関数:
 *     Weather_Init(hInst) / Weather_Shutdown()
 *     Weather_Enabled / _SetEnabled
 *     Weather_GetVariant / _SetVariant / _VariantCount
 *     Weather_CityCount / _CityName / _GetCity / _SetCity / _LocLabel / _GetLatLon / _PickCoords
 *     Weather_GetUnit / _SetUnit
 *     Weather_GetSize / _SetSize, _GetOpacity / _SetOpacity
 *     Weather_Movable / _SetMovable / _ResetPosition
 *     Weather_Refresh / Weather_Status
 *   ※ Settings_Init() は Weather_Init() より先に走る (初回設定) ので、
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
#include <winhttp.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winhttp.lib")
#endif

/* setting.c */
void Settings_RefreshWeather(void);
void Settings_OpenPage(int page); /* 6: 天気のページ */

/* このファイルが公開する関数 */
void Weather_Init(HINSTANCE hInst);
void Weather_Shutdown(void);
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

/* 使うフォントの優先順 (最初に見つかったものを使用。検索バー・時計と同じ) */
static const wchar_t *const WX_FONTS[] = {L"Hiragino Maru Gothic ProN", L"ヒラギノ丸ゴシック ProN", L"Kosugi Maru",
                                          L"M PLUS Rounded 1c", L"Yu Gothic UI", L"Meiryo UI", L"Segoe UI"};
#define TIMER_Z 2     /* 最奥の位置を保つための見張り */
#define TIMER_FETCH 3 /* 天気の自動更新 */
#define WM_WEATHER (WM_APP + 1)
#define POS_AUTO 0x7fffffff
#define FETCH_OK_MS (30 * 60 * 1000)
#define FETCH_RETRY_MS (2 * 60 * 1000)
#define N_VARIANTS 9
#define DEF_CITY 2 /* 東京 */
#define PI_F 3.14159265f

#define MENU_UNIT_C 1
#define MENU_UNIT_F 2
#define MENU_REFRESH 3
#define MENU_MOVABLE 4
#define MENU_HIDE 5
#define MENU_SETTINGS 6
#define MENU_VAR 100  /* 100+番号: デザイン */
#define MENU_CITY 200 /* 200+番号: 場所 */

/* ------------------------------------------------------------------ */
/* 場所の一覧 (最後に「緯度・経度を指定」が付く)                            */
/* ------------------------------------------------------------------ */
typedef struct
{
    const wchar_t *name;
    double lat, lon;
} City;

static const City kCities[] = {
    {L"札幌", 43.0621, 141.3544},
    {L"仙台", 38.2682, 140.8694},
    {L"東京", 35.6895, 139.6917},
    {L"横浜", 35.4437, 139.6380},
    {L"新潟", 37.9162, 139.0364},
    {L"金沢", 36.5613, 136.6562},
    {L"静岡", 34.9769, 138.3831},
    {L"名古屋", 35.1815, 136.9066},
    {L"京都", 35.0116, 135.7681},
    {L"大阪", 34.6937, 135.5023},
    {L"神戸", 34.6901, 135.1955},
    {L"広島", 34.3853, 132.4553},
    {L"高松", 34.3401, 134.0434},
    {L"福岡", 33.5904, 130.4017},
    {L"長崎", 32.7503, 129.8777},
    {L"鹿児島", 31.5966, 130.5571},
    {L"那覇", 26.2124, 127.6809},
    {L"ソウル", 37.5665, 126.9780},
    {L"台北", 25.0330, 121.5654},
    {L"上海", 31.2304, 121.4737},
    {L"ハノイ", 21.0278, 105.8342},
    {L"ホーチミン", 10.8231, 106.6297},
    {L"バンコク", 13.7563, 100.5018},
    {L"シンガポール", 1.3521, 103.8198},
    {L"シドニー", -33.8688, 151.2093},
    {L"ロンドン", 51.5074, -0.1278},
    {L"パリ", 48.8566, 2.3522},
    {L"ニューヨーク", 40.7128, -74.0060},
    {L"ロサンゼルス", 34.0522, -118.2437},
};
#define N_CITIES ((int)(sizeof(kCities) / sizeof(kCities[0])))

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
    BOOL valid;
    double temp, feels, wind; /* 常に ℃ / km/h で保持し、表示のときに換算 */
    int hum, code, isDay, n;
    int dcode[5];
    double dmax[5], dmin[5];
    wchar_t upd[8]; /* 最終更新 "HH:MM" */
} WData;

static HINSTANCE g_hInst;
static BOOL g_ready, g_loaded;
static BOOL g_on, g_movable = TRUE;
static int g_var, g_city = DEF_CITY, g_unit, g_size = 100, g_opa = 78, g_x = POS_AUTO, g_y = POS_AUTO;
static double g_lat = 35.6895, g_lon = 139.6917;
static wchar_t g_ini[MAX_PATH];
static float g_dpi = 1.0f;

static HWND g_hwnd;
static HDC g_dc;
static HBITMAP g_dib, g_old;
static uint32_t *g_bits;
static int g_W, g_H;
static BOOL g_placing;

static HFONT g_fBig, g_fMid, g_fSm;
static WData g_data;
static int g_gen; /* 場所を変えるたびに増やす (古い取得結果を捨てるため) */
static BOOL g_failed, g_fetching;

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static int S(float v) { return (int)(v * g_dpi + 0.5f); }
static float U(float v) { return v * g_dpi * g_size / 100.0f; } /* 大きさ(%) を反映した 論理px -> デバイスpx */
static int Ui(float v) { return (int)(U(v) + 0.5f); }

static void EffLoc(double *la, double *lo)
{
    if (g_city >= 0 && g_city < N_CITIES)
    {
        *la = kCities[g_city].lat;
        *lo = kCities[g_city].lon;
    }
    else
    {
        *la = g_lat;
        *lo = g_lon;
    }
}

static const wchar_t *LocName(void)
{
    return (g_city >= 0 && g_city < N_CITIES) ? kCities[g_city].name : L"指定地点";
}

static int R10(double v) { return (int)floor(v * 10.0 + 0.5); }

/* ------------------------------------------------------------------ */
/* 保存 / 読み込み (weather.ini)                                          */
/* ------------------------------------------------------------------ */
static void SaveWeather(void)
{
    wchar_t v[48];
#define PUT(key, val)                                          \
    do                                                         \
    {                                                          \
        _snwprintf(v, 48, L"%d", (int)(val));                  \
        WritePrivateProfileStringW(L"Weather", key, v, g_ini); \
    } while (0)
    PUT(L"enabled", g_on);
    PUT(L"variant", g_var);
    PUT(L"city", g_city);
    PUT(L"unit", g_unit);
    PUT(L"size", g_size);
    PUT(L"opacity", g_opa);
    PUT(L"movable", g_movable);
    PUT(L"x", g_x);
    PUT(L"y", g_y);
#undef PUT
    _snwprintf(v, 48, L"%.4f", g_lat);
    WritePrivateProfileStringW(L"Weather", L"lat", v, g_ini);
    _snwprintf(v, 48, L"%.4f", g_lon);
    WritePrivateProfileStringW(L"Weather", L"lon", v, g_ini);
}

static void LocKey(wchar_t *out, int cch)
{
    double la, lo;
    EffLoc(&la, &lo);
    _snwprintf(out, cch, L"%.4f,%.4f", la, lo);
    out[cch - 1] = 0;
}

/* 取得結果のキャッシュ (起動直後でも前回の天気をすぐに出すため) */
static void SaveCache(void)
{
    if (!g_data.valid)
        return;
    wchar_t buf[512], key[48];
    int o = _snwprintf(buf, 512, L"%d,%d,%d,%d,%d,%d,%d", R10(g_data.temp), R10(g_data.feels), g_data.hum,
                       R10(g_data.wind), g_data.code, g_data.isDay, g_data.n);
    for (int i = 0; i < g_data.n && o > 0 && o < 450; i++)
        o += _snwprintf(buf + o, 512 - o, L",%d,%d,%d", g_data.dcode[i], R10(g_data.dmax[i]), R10(g_data.dmin[i]));
    buf[511] = 0;
    LocKey(key, 48);
    WritePrivateProfileStringW(L"Weather", L"cache_loc", key, g_ini);
    WritePrivateProfileStringW(L"Weather", L"cache_upd", g_data.upd, g_ini);
    WritePrivateProfileStringW(L"Weather", L"cache", buf, g_ini);
}

static BOOL NextInt(const wchar_t **p, int *v)
{
    wchar_t *e;
    long x = wcstol(*p, &e, 10);
    if (e == *p)
        return FALSE;
    *v = (int)x;
    *p = e;
    if (**p == L',')
        (*p)++;
    return TRUE;
}

static void LoadCache(void)
{
    wchar_t key[48], saved[48], buf[512];
    LocKey(key, 48);
    GetPrivateProfileStringW(L"Weather", L"cache_loc", L"", saved, 48, g_ini);
    if (wcscmp(key, saved) != 0)
        return;
    GetPrivateProfileStringW(L"Weather", L"cache", L"", buf, 512, g_ini);
    const wchar_t *p = buf;
    int a[7];
    for (int i = 0; i < 7; i++)
        if (!NextInt(&p, &a[i]))
            return;
    int n = a[6];
    if (n < 1 || n > 5)
        return;
    WData d;
    memset(&d, 0, sizeof(d));
    d.temp = a[0] / 10.0;
    d.feels = a[1] / 10.0;
    d.hum = a[2];
    d.wind = a[3] / 10.0;
    d.code = a[4];
    d.isDay = a[5];
    d.n = n;
    for (int i = 0; i < n; i++)
    {
        int c, hi, lo;
        if (!NextInt(&p, &c) || !NextInt(&p, &hi) || !NextInt(&p, &lo))
            return;
        d.dcode[i] = c;
        d.dmax[i] = hi / 10.0;
        d.dmin[i] = lo / 10.0;
    }
    GetPrivateProfileStringW(L"Weather", L"cache_upd", L"", d.upd, 8, g_ini);
    d.valid = TRUE;
    g_data = d;
}

static void EnsureLoaded(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    HDC scr = GetDC(NULL);
    g_dpi = GetDeviceCaps(scr, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, scr);

    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    wchar_t *slash = wcsrchr(g_ini, L'\\');
    if (slash)
        wcscpy(slash + 1, L"weather.ini");
    else
        wcscpy(g_ini, L"weather.ini");

    wchar_t v[48];
    g_on = GetPrivateProfileIntW(L"Weather", L"enabled", 0, g_ini) != 0;
    g_var = ClampI((int)GetPrivateProfileIntW(L"Weather", L"variant", 0, g_ini), 0, N_VARIANTS - 1);
    g_city = ClampI((int)GetPrivateProfileIntW(L"Weather", L"city", DEF_CITY, g_ini), 0, N_CITIES);
    g_unit = GetPrivateProfileIntW(L"Weather", L"unit", 0, g_ini) ? 1 : 0;
    g_size = ClampI((int)GetPrivateProfileIntW(L"Weather", L"size", 100, g_ini), 60, 200);
    g_opa = ClampI((int)GetPrivateProfileIntW(L"Weather", L"opacity", 78, g_ini), 30, 100);
    g_movable = GetPrivateProfileIntW(L"Weather", L"movable", 1, g_ini) != 0;
    g_x = (int)GetPrivateProfileIntW(L"Weather", L"x", POS_AUTO, g_ini);
    g_y = (int)GetPrivateProfileIntW(L"Weather", L"y", POS_AUTO, g_ini);
    GetPrivateProfileStringW(L"Weather", L"lat", L"35.6895", v, 48, g_ini);
    g_lat = wcstod(v, NULL);
    GetPrivateProfileStringW(L"Weather", L"lon", L"139.6917", v, 48, g_ini);
    g_lon = wcstod(v, NULL);
    if (g_lat < -90.0 || g_lat > 90.0 || g_lon < -180.0 || g_lon > 180.0)
    {
        g_lat = 35.6895;
        g_lon = 139.6917;
    }
    LoadCache();
}

/* ------------------------------------------------------------------ */
/* 天気の取得 (WinHTTP + Open-Meteo)                                      */
/* ------------------------------------------------------------------ */
/* HTTPS GET。成功したら本文 (NUL 終端) を malloc して返す */
static BOOL HttpGet(const wchar_t *host, const wchar_t *path, char **out)
{
    BOOL ok = FALSE;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    HINTERNET s = WinHttpOpen(L"PromDock/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s)
        return FALSE;
    WinHttpSetTimeouts(s, 5000, 5000, 8000, 10000);
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE)
                    : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL))
    {
        DWORD code = 0, sz = sizeof(code);
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX) &&
            code == 200)
        {
            ok = TRUE;
            for (;;)
            {
                DWORD avail = 0, got = 0;
                if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0)
                    break;
                if (len + avail + 1 > cap)
                {
                    size_t nc = (len + avail + 1) * 2;
                    if (nc > 256 * 1024)
                    {
                        ok = FALSE;
                        break;
                    }
                    char *nb = (char *)realloc(buf, nc);
                    if (!nb)
                    {
                        ok = FALSE;
                        break;
                    }
                    buf = nb;
                    cap = nc;
                }
                if (!WinHttpReadData(r, buf + len, avail, &got) || got == 0)
                {
                    ok = FALSE;
                    break;
                }
                len += got;
            }
        }
    }
    if (r)
        WinHttpCloseHandle(r);
    if (c)
        WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    if (!ok || !buf)
    {
        free(buf);
        return FALSE;
    }
    buf[len] = 0;
    *out = buf;
    return TRUE;
}

/* "key": の次の数値 (sec 以降で最初に現れるもの) */
static BOOL NumAfter(const char *sec, const char *key, double *v)
{
    char pat[64];
    _snprintf(pat, 64, "\"%s\":", key);
    pat[63] = 0;
    const char *q = strstr(sec, pat);
    if (!q)
        return FALSE;
    q += strlen(pat);
    if (strncmp(q, "null", 4) == 0)
        return FALSE;
    *v = strtod(q, NULL);
    return TRUE;
}

/* "key":[1,2,3] の配列 */
static int ArrAfter(const char *sec, const char *key, double *out, int max)
{
    char pat[64];
    _snprintf(pat, 64, "\"%s\":[", key);
    pat[63] = 0;
    const char *q = strstr(sec, pat);
    if (!q)
        return 0;
    q += strlen(pat);
    int n = 0;
    while (*q && *q != ']' && n < max)
    {
        char *e;
        if (strncmp(q, "null", 4) == 0)
        {
            out[n++] = 0;
            q += 4;
        }
        else
        {
            out[n++] = strtod(q, &e);
            if (e == q)
                break;
            q = e;
        }
        if (*q == ',')
            q++;
    }
    return n;
}

static BOOL ParseForecast(const char *js, WData *d)
{
    memset(d, 0, sizeof(*d));
    const char *cur = strstr(js, "\"current\":{");
    const char *dly = strstr(js, "\"daily\":{");
    if (!cur || !dly)
        return FALSE;
    cur += strlen("\"current\":{");
    dly += strlen("\"daily\":{");
    double v;
    if (!NumAfter(cur, "temperature_2m", &d->temp))
        return FALSE;
    if (!NumAfter(cur, "apparent_temperature", &d->feels))
        d->feels = d->temp;
    if (NumAfter(cur, "relative_humidity_2m", &v))
        d->hum = (int)(v + 0.5);
    if (NumAfter(cur, "wind_speed_10m", &v))
        d->wind = v;
    if (NumAfter(cur, "weather_code", &v))
        d->code = (int)(v + 0.5);
    d->isDay = 1;
    if (NumAfter(cur, "is_day", &v))
        d->isDay = v > 0.5;

    double c[5], hi[5], lo[5];
    int nc = ArrAfter(dly, "weather_code", c, 5);
    int nh = ArrAfter(dly, "temperature_2m_max", hi, 5);
    int nl = ArrAfter(dly, "temperature_2m_min", lo, 5);
    int n = nc < nh ? nc : nh;
    if (nl < n)
        n = nl;
    if (n < 1)
        return FALSE;
    d->n = n;
    for (int i = 0; i < n; i++)
    {
        d->dcode[i] = (int)(c[i] + 0.5);
        d->dmax[i] = hi[i];
        d->dmin[i] = lo[i];
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf(d->upd, 8, L"%02d:%02d", st.wHour, st.wMinute);
    d->upd[7] = 0;
    d->valid = TRUE;
    return TRUE;
}

typedef struct
{
    HWND hwnd;
    int gen;
    double lat, lon;
} FetchReq;

typedef struct
{
    int gen;
    BOOL ok;
    WData d;
} FetchRes;

static DWORD WINAPI FetchThread(LPVOID p)
{
    FetchReq *rq = (FetchReq *)p;
    FetchRes *rs = (FetchRes *)calloc(1, sizeof(FetchRes));
    if (rs)
    {
        rs->gen = rq->gen;
        wchar_t path[512];
        _snwprintf(path, 512,
                   L"/v1/forecast?latitude=%.4f&longitude=%.4f"
                   L"&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,wind_speed_10m"
                   L"&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=5",
                   rq->lat, rq->lon);
        path[511] = 0;
        char *body = NULL;
        if (HttpGet(L"api.open-meteo.com", path, &body))
        {
            rs->ok = ParseForecast(body, &rs->d);
            free(body);
        }
        if (!PostMessageW(rq->hwnd, WM_WEATHER, 0, (LPARAM)rs))
            free(rs);
    }
    free(rq);
    return 0;
}

static void StartFetch(void)
{
    if (!g_hwnd)
        return;
    FetchReq *rq = (FetchReq *)malloc(sizeof(FetchReq));
    if (!rq)
        return;
    rq->hwnd = g_hwnd;
    rq->gen = g_gen;
    EffLoc(&rq->lat, &rq->lon);
    HANDLE th = CreateThread(NULL, 0, FetchThread, rq, 0, NULL);
    if (th)
    {
        CloseHandle(th);
        g_fetching = TRUE;
    }
    else
        free(rq);
    Settings_RefreshWeather();
}

/* ------------------------------------------------------------------ */
/* 天気コード (WMO) -> アイコンの種類 / 日本語                                */
/* ------------------------------------------------------------------ */
enum
{
    K_SUN,
    K_PART,
    K_CLOUD,
    K_FOG,
    K_RAIN,
    K_SNOW,
    K_STORM
};

static void CodeInfo(int code, int *kind, const wchar_t **label)
{
    int k = K_CLOUD;
    const wchar_t *t = L"くもり";
    if (code == 0)
        k = K_SUN, t = L"晴れ";
    else if (code == 1)
        k = K_PART, t = L"おおむね晴れ";
    else if (code == 2)
        k = K_PART, t = L"晴れ時々くもり";
    else if (code == 3)
        k = K_CLOUD, t = L"くもり";
    else if (code == 45 || code == 48)
        k = K_FOG, t = L"霧";
    else if (code >= 51 && code <= 57)
        k = K_RAIN, t = L"霧雨";
    else if (code >= 61 && code <= 67)
        k = K_RAIN, t = L"雨";
    else if (code >= 71 && code <= 77)
        k = K_SNOW, t = L"雪";
    else if (code >= 80 && code <= 82)
        k = K_RAIN, t = L"にわか雨";
    else if (code == 85 || code == 86)
        k = K_SNOW, t = L"にわか雪";
    else if (code >= 95 && code <= 99)
        k = K_STORM, t = L"雷雨";
    *kind = k;
    *label = t;
}

/* ------------------------------------------------------------------ */
/* フォント / 文字 (GDI で白文字をマスクに描いて合成)                          */
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
    for (int i = 0; i < (int)(sizeof(WX_FONTS) / sizeof(WX_FONTS[0])); i++)
    {
        LOGFONTW lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfCharSet = DEFAULT_CHARSET;
        wcsncpy(lf.lfFaceName, WX_FONTS[i], LF_FACESIZE - 1);
        BOOL ok = FALSE;
        EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)FontFoundProc, (LPARAM)&ok, 0);
        if (ok)
        {
            found = WX_FONTS[i];
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

static void FreeFonts(void)
{
    if (g_fBig)
        DeleteObject(g_fBig);
    if (g_fMid)
        DeleteObject(g_fMid);
    if (g_fSm)
        DeleteObject(g_fSm);
    g_fBig = g_fMid = g_fSm = NULL;
}

static HFONT MkFont(int px, int weight, const wchar_t *name)
{
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
}

/* 大きさ・デザインが変わったとき: フォントを作り直す */
static void RebuildFonts(void)
{
    FreeFonts();
    const wchar_t *name = PickFontName();
    static const float kBig[N_VARIANTS] = {44, 62, 34, 40, 28, 30, 44, 44, 96};
    float big = kBig[g_var];
    float mid = (g_var == 1 || g_var == 8) ? 15.0f : 14.0f;
    g_fBig = MkFont(Ui(big), FW_LIGHT, name);
    g_fMid = MkFont(Ui(mid), FW_NORMAL, name);
    g_fSm = MkFont(Ui(12.0f), FW_NORMAL, name);
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
static void DrawBgC(float rad, float alpha, float cr, float cg, float cb)
{
    for (int y = 0; y < g_H; y++)
        for (int x = 0; x < g_W; x++)
        {
            float cov = Clamp01(0.5f - RRectDist(x + 0.5f, y + 0.5f, 0, 0, (float)g_W, (float)g_H, rad));
            if (cov > 0)
                BlendPx(x, y, cr, cg, cb, alpha * cov);
        }
}

static void DrawBg(float rad, float alpha) { DrawBgC(rad, alpha, 28, 28, 32); }

/* 背景のないデザインでも全面をつかめるように、ほぼ透明 (alpha=1) で全体を塗る */
static void HitFill(void)
{
    for (int i = 0; i < g_W * g_H; i++)
        g_bits[i] = 0x01000000u;
}

static float SegDist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 1e-6f ? Clamp01((wx * vx + wy * vy) / len2) : 0.0f;
    float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return sqrtf(dx * dx + dy * dy);
}

static void DrawSeg(float ax, float ay, float bx, float by, float th, float r, float g, float bl, float a)
{
    int x0 = ClampI((int)floorf(fminf(ax, bx) - th - 1), 0, g_W), x1 = ClampI((int)ceilf(fmaxf(ax, bx) + th + 1), 0, g_W);
    int y0 = ClampI((int)floorf(fminf(ay, by) - th - 1), 0, g_H), y1 = ClampI((int)ceilf(fmaxf(ay, by) + th + 1), 0, g_H);
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

/* 文字を描く。x は anchor (0:左 1:中央 2:右) の基準、y は縦の中心。shadow は背景のないデザイン用の影 */
static void Txt(HFONT f, const wchar_t *s, float x, float y, int anchor, float r, float g, float bl, float a, BOOL shadow)
{
    Mask m = MakeMask(f, s, (int)wcslen(s));
    if (!m.p)
        return;
    int dx = (int)(anchor == 1 ? x - m.w * 0.5f : (anchor == 2 ? x - m.w : x) + 0.5f);
    int dy = (int)(y - m.h * 0.5f + 0.5f);
    if (shadow)
        BlitMask(&m, dx, dy + (int)fmaxf(1.0f, 1.5f * g_dpi), 0, 0, 0, a * 0.40f);
    BlitMask(&m, dx, dy, r, g, bl, a);
    FreeMask(&m);
}

/* ------------------------------------------------------------------ */
/* 天気アイコン (すべてコードで描画)                                         */
/* ------------------------------------------------------------------ */
static float CircCov(float px, float py, float cx, float cy, float r)
{
    float dx = px - cx, dy = py - cy;
    return Clamp01(0.5f - (sqrtf(dx * dx + dy * dy) - r));
}

/* 雲: 3つの丸 + 底の角丸四角の和。s は雲の幅 */
static void DrawCloud(float cx, float cy, float s, float r, float g, float bl, float a)
{
    int x0 = ClampI((int)floorf(cx - 0.56f * s), 0, g_W), x1 = ClampI((int)ceilf(cx + 0.56f * s), 0, g_W);
    int y0 = ClampI((int)floorf(cy - 0.40f * s), 0, g_H), y1 = ClampI((int)ceilf(cy + 0.36f * s), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f, py = y + 0.5f;
            float cov = CircCov(px, py, cx - 0.20f * s, cy + 0.04f * s, 0.19f * s);
            cov = fmaxf(cov, CircCov(px, py, cx + 0.05f * s, cy - 0.10f * s, 0.26f * s));
            cov = fmaxf(cov, CircCov(px, py, cx + 0.30f * s, cy + 0.07f * s, 0.17f * s));
            cov = fmaxf(cov, Clamp01(0.5f - RRectDist(px, py, cx - 0.47f * s, cy + 0.02f * s, cx + 0.47f * s, cy + 0.30f * s, 0.14f * s)));
            if (cov > 0)
                BlendPx(x, y, r, g, bl, cov * a);
        }
}

static void DrawSun(float cx, float cy, float s)
{
    float th = fmaxf(1.5f, 0.065f * s);
    for (int i = 0; i < 8; i++)
    {
        float a = i * PI_F / 4.0f, sx = sinf(a), sy = -cosf(a);
        DrawSeg(cx + sx * 0.31f * s, cy + sy * 0.31f * s, cx + sx * 0.43f * s, cy + sy * 0.43f * s, th, 255, 200, 70, 1.0f);
    }
    DrawDot(cx, cy, 0.20f * s, 255, 200, 70, 1.0f);
}

static void DrawMoon(float cx, float cy, float s)
{
    float r = 0.30f * s, ox = cx + 0.13f * s, oy = cy - 0.09f * s, r2 = 0.26f * s;
    int x0 = ClampI((int)floorf(cx - r - 1), 0, g_W), x1 = ClampI((int)ceilf(cx + r + 1), 0, g_W);
    int y0 = ClampI((int)floorf(cy - r - 1), 0, g_H), y1 = ClampI((int)ceilf(cy + r + 1), 0, g_H);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            float px = x + 0.5f, py = y + 0.5f;
            float cov = fminf(CircCov(px, py, cx, cy, r), 1.0f - CircCov(px, py, ox, oy, r2));
            if (cov > 0)
                BlendPx(x, y, 236, 236, 246, cov);
        }
}

/* s: アイコン全体のおおよその大きさ (px)。night は晴れ系を月にする */
static void DrawWxIcon(int kind, BOOL night, float cx, float cy, float s)
{
    float th = fmaxf(1.5f, 0.055f * s);
    switch (kind)
    {
    case K_SUN:
        if (night)
            DrawMoon(cx, cy, s);
        else
            DrawSun(cx, cy, s);
        break;
    case K_PART:
        if (night)
            DrawMoon(cx - 0.16f * s, cy - 0.14f * s, s * 0.72f);
        else
            DrawSun(cx - 0.16f * s, cy - 0.14f * s, s * 0.72f);
        DrawCloud(cx + 0.06f * s, cy + 0.10f * s, s * 0.86f, 240, 242, 248, 1.0f);
        break;
    case K_CLOUD:
        DrawCloud(cx, cy, s * 0.95f, 236, 238, 244, 1.0f);
        break;
    case K_FOG:
        DrawCloud(cx, cy - 0.12f * s, s * 0.82f, 226, 228, 236, 1.0f);
        for (int k = 0; k < 3; k++)
        {
            float y = cy + (0.20f + 0.11f * k) * s, hw = (0.34f - 0.05f * k) * s;
            DrawSeg(cx - hw, y, cx + hw, y, th, 226, 228, 236, 0.85f);
        }
        break;
    case K_RAIN:
        DrawCloud(cx, cy - 0.10f * s, s * 0.86f, 196, 204, 220, 1.0f);
        for (int k = 0; k < 3; k++)
        {
            float x = cx + (k - 1) * 0.22f * s;
            DrawSeg(x + 0.04f * s, cy + 0.22f * s, x - 0.04f * s, cy + 0.40f * s, th, 110, 175, 255, 1.0f);
        }
        break;
    case K_SNOW:
        DrawCloud(cx, cy - 0.10f * s, s * 0.86f, 205, 212, 226, 1.0f);
        for (int k = 0; k < 3; k++)
            DrawDot(cx + (k - 1) * 0.22f * s, cy + (k == 1 ? 0.36f : 0.30f) * s, fmaxf(1.4f, 0.045f * s), 255, 255, 255, 1.0f);
        break;
    default: /* K_STORM */
        DrawCloud(cx, cy - 0.10f * s, s * 0.86f, 160, 166, 184, 1.0f);
        DrawSeg(cx + 0.07f * s, cy + 0.17f * s, cx - 0.04f * s, cy + 0.31f * s, th * 1.3f, 255, 214, 90, 1.0f);
        DrawSeg(cx - 0.04f * s, cy + 0.31f * s, cx + 0.05f * s, cy + 0.31f * s, th * 1.3f, 255, 214, 90, 1.0f);
        DrawSeg(cx + 0.05f * s, cy + 0.31f * s, cx - 0.06f * s, cy + 0.47f * s, th * 1.3f, 255, 214, 90, 1.0f);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 描画                                                                  */
/* ------------------------------------------------------------------ */
static const wchar_t *const kWeekday[7] = {L"日", L"月", L"火", L"水", L"木", L"金", L"土"};
static const wchar_t *const kVarNames[N_VARIANTS] = {L"カード", L"細字 (大)", L"予報 (5日)", L"縦並び", L"ミニ",
                                                     L"週間", L"スカイ (青)", L"アイコン (大)", L"巨大 (気温のみ)"};

static int Disp(double c)
{
    double v = g_unit ? c * 9.0 / 5.0 + 32.0 : c;
    int i = (int)floor(v + 0.5);
    return i;
}

static void FmtTemp(double c, wchar_t *out, int cch)
{
    _snwprintf(out, cch, L"%d°", Disp(c));
    out[cch - 1] = 0;
}

static void CalcSize(int *w, int *h)
{
    switch (g_var)
    {
    case 1:
        *w = Ui(230);
        *h = Ui(116);
        break;
    case 2:
        *w = Ui(400);
        *h = Ui(176);
        break;
    case 3:
        *w = Ui(156);
        *h = Ui(232);
        break;
    case 4:
        *w = Ui(150);
        *h = Ui(60);
        break;
    case 5:
        *w = Ui(210);
        *h = Ui(204);
        break;
    case 7:
        *w = Ui(150);
        *h = Ui(196);
        break;
    case 8:
        *w = Ui(250);
        *h = Ui(150);
        break;
    default:
        *w = Ui(250);
        *h = Ui(124);
        break;
    }
}

/* データが無いとき (取得中 / 取得失敗) のメッセージ。描いたら TRUE */
static BOOL NoData(BOOL shadow)
{
    if (g_data.valid)
        return FALSE;
    Txt(g_fMid, g_failed ? L"取得できません" : L"取得中…", g_W * 0.5f, g_H * 0.5f, 1, 238, 238, 240, 0.8f, shadow);
    return TRUE;
}

static void RenderCard(BOOL sky)
{
    const float gr = 214, gg = 184, gb = 132; /* 控えめなシャンパンゴールド */
    if (sky)
        DrawBgC(U(16), g_opa / 100.0f, 58, 108, 182);
    else
        DrawBg(U(16), g_opa / 100.0f);
    Txt(g_fSm, LocName(), U(16), U(18), 0, sky ? 255 : gr, sky ? 255 : gg, sky ? 255 : gb, 0.92f, FALSE);
    if (NoData(FALSE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, U(50), U(64), U(62));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, U(94), U(60), 0, 248, 248, 250, 0.97f, FALSE);
    Txt(g_fMid, lab, U(96), U(90), 0, 238, 238, 240, 0.75f, FALSE);

    _snwprintf(t, 64, L"湿度 %d%%", g_data.hum);
    t[63] = 0;
    Txt(g_fSm, t, U(16), U(108), 0, 238, 238, 240, 0.55f, FALSE);
    wchar_t hi[16], lo[16];
    FmtTemp(g_data.dmax[0], hi, 16);
    FmtTemp(g_data.dmin[0], lo, 16);
    _snwprintf(t, 64, L"最高 %s  最低 %s", hi, lo);
    t[63] = 0;
    Txt(g_fSm, t, g_W - U(16), U(108), 2, 238, 238, 240, 0.7f, FALSE);
}

static void RenderBig(void)
{
    HitFill();
    if (NoData(TRUE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, U(42), U(46), U(66));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, U(88), U(44), 0, 250, 250, 252, 0.97f, TRUE);
    _snwprintf(t, 64, L"%s  ·  %s", LocName(), lab);
    t[63] = 0;
    {
        Mask mt = MakeMask(g_fMid, t, (int)wcslen(t));
        if (mt.w > g_W - 2 * U(16)) /* 長くて収まらないときは、天気だけにする */
            wcscpy(t, lab);
        FreeMask(&mt);
    }
    Txt(g_fMid, t, U(16), U(98), 0, 250, 250, 252, 0.85f, TRUE);
}

static void RenderForecast(void)
{
    const float gr = 226, gg = 200, gb = 150;
    float pad = U(18);
    DrawBg(U(18), g_opa / 100.0f);
    Txt(g_fSm, LocName(), pad, U(18), 0, gr, gg, gb, 0.92f, FALSE);
    if (NoData(FALSE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, pad + U(26), U(58), U(46));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, U(82), U(56), 0, 248, 248, 250, 0.97f, FALSE);
    Txt(g_fMid, lab, g_W - pad, U(46), 2, 238, 238, 240, 0.85f, FALSE);
    wchar_t fl[16];
    FmtTemp(g_data.feels, fl, 16);
    _snwprintf(t, 64, L"体感 %s  湿度 %d%%", fl, g_data.hum);
    t[63] = 0;
    Txt(g_fSm, t, g_W - pad, U(68), 2, 238, 238, 240, 0.6f, FALSE);

    DrawSeg(pad, U(98), g_W - pad, U(98), fmaxf(1.0f, g_dpi), 255, 255, 255, 0.14f);

    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = g_data.n;
    float colW = (g_W - 2 * pad) / (float)n;
    for (int i = 0; i < n; i++)
    {
        float cx = pad + colW * (i + 0.5f);
        int k;
        const wchar_t *l2;
        CodeInfo(g_data.dcode[i], &k, &l2);
        Txt(g_fSm, i == 0 ? L"今日" : kWeekday[(st.wDayOfWeek + i) % 7], cx, U(114), 1, 238, 238, 240, 0.7f, FALSE);
        DrawWxIcon(k, FALSE, cx, U(136), U(30));
        wchar_t hi[16], lo[16];
        FmtTemp(g_data.dmax[i], hi, 16);
        FmtTemp(g_data.dmin[i], lo, 16);
        _snwprintf(t, 64, L"%s / %s", hi, lo);
        t[63] = 0;
        Txt(g_fSm, t, cx, U(158), 1, 248, 248, 250, 0.85f, FALSE);
    }
}

static void RenderTall(void)
{
    const float gr = 214, gg = 184, gb = 132;
    DrawBg(U(20), g_opa / 100.0f);
    Txt(g_fSm, LocName(), g_W * 0.5f, U(22), 1, gr, gg, gb, 0.92f, FALSE);
    if (NoData(FALSE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, g_W * 0.5f, U(80), U(84));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, g_W * 0.5f, U(138), 1, 248, 248, 250, 0.97f, FALSE);
    Txt(g_fMid, lab, g_W * 0.5f, U(168), 1, 238, 238, 240, 0.8f, FALSE);
    wchar_t hi[16], lo[16];
    FmtTemp(g_data.dmax[0], hi, 16);
    FmtTemp(g_data.dmin[0], lo, 16);
    _snwprintf(t, 64, L"最高 %s  最低 %s", hi, lo);
    t[63] = 0;
    Txt(g_fSm, t, g_W * 0.5f, U(190), 1, 238, 238, 240, 0.7f, FALSE);
    _snwprintf(t, 64, L"湿度 %d%%  風 %d km/h", g_data.hum, (int)(g_data.wind + 0.5));
    t[63] = 0;
    Txt(g_fSm, t, g_W * 0.5f, U(209), 1, 238, 238, 240, 0.55f, FALSE);
}

/* 4: ミニ (小さな丸い帯: アイコン + 気温 + 場所) */
static void RenderMini(void)
{
    DrawBg(U(28), g_opa / 100.0f);
    if (NoData(FALSE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, U(32), U(30), U(36));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, U(62), U(26), 0, 248, 248, 250, 0.97f, FALSE);
    Txt(g_fSm, LocName(), U(62), U(46), 0, 214, 184, 132, 0.9f, FALSE);
}

/* 5: 週間 (いまの天気 + 5日分を縦に並べる) */
static void RenderWeek(void)
{
    const float gr = 214, gg = 184, gb = 132;
    DrawBg(U(18), g_opa / 100.0f);
    Txt(g_fSm, LocName(), U(16), U(18), 0, gr, gg, gb, 0.92f, FALSE);
    if (NoData(FALSE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, U(36), U(52), U(40));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, U(66), U(48), 0, 248, 248, 250, 0.97f, FALSE);
    Txt(g_fSm, lab, U(68), U(72), 0, 238, 238, 240, 0.7f, FALSE);
    DrawSeg(U(16), U(86), g_W - U(16), U(86), fmaxf(1.0f, g_dpi), 255, 255, 255, 0.14f);

    SYSTEMTIME st;
    GetLocalTime(&st);
    for (int i = 0; i < g_data.n; i++)
    {
        float cy = U(100.0f + i * 22.0f);
        int k;
        const wchar_t *l2;
        CodeInfo(g_data.dcode[i], &k, &l2);
        Txt(g_fSm, i == 0 ? L"今日" : kWeekday[(st.wDayOfWeek + i) % 7], U(16), cy, 0, 238, 238, 240, 0.7f, FALSE);
        DrawWxIcon(k, FALSE, U(92), cy, U(20));
        wchar_t hi[16], lo[16];
        FmtTemp(g_data.dmax[i], hi, 16);
        FmtTemp(g_data.dmin[i], lo, 16);
        _snwprintf(t, 64, L"%s / %s", hi, lo);
        t[63] = 0;
        Txt(g_fSm, t, g_W - U(16), cy, 2, 248, 248, 250, 0.85f, FALSE);
    }
}

/* 7: アイコン (大): 背景なしで、大きな天気アイコンと気温 */
static void RenderIconBig(void)
{
    HitFill();
    if (NoData(TRUE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    DrawWxIcon(kind, !g_data.isDay, g_W * 0.5f, U(68), U(104));
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, g_W * 0.5f, U(138), 1, 250, 250, 252, 0.97f, TRUE);
    Txt(g_fMid, lab, g_W * 0.5f, U(168), 1, 250, 250, 252, 0.85f, TRUE);
    Txt(g_fSm, LocName(), g_W * 0.5f, U(186), 1, 250, 250, 252, 0.65f, TRUE);
}

/* 8: 巨大: 背景なしで、気温だけをとても大きく */
static void RenderHuge(void)
{
    HitFill();
    if (NoData(TRUE))
        return;
    int kind;
    const wchar_t *lab;
    CodeInfo(g_data.code, &kind, &lab);
    wchar_t t[64];
    FmtTemp(g_data.temp, t, 64);
    Txt(g_fBig, t, U(10), U(62), 0, 250, 250, 252, 0.97f, TRUE);
    _snwprintf(t, 64, L"%s  ·  %s", LocName(), lab);
    t[63] = 0;
    {
        Mask mt = MakeMask(g_fMid, t, (int)wcslen(t));
        if (mt.w > g_W - 2 * U(16))
            wcscpy(t, lab);
        FreeMask(&mt);
    }
    Txt(g_fMid, t, U(16), U(132), 0, 250, 250, 252, 0.85f, TRUE);
}

static void WeatherRender(void)
{
    if (!g_hwnd || !g_bits)
        return;
    memset(g_bits, 0, (size_t)g_W * g_H * 4);
    switch (g_var)
    {
    case 1:
        RenderBig();
        break;
    case 2:
        RenderForecast();
        break;
    case 3:
        RenderTall();
        break;
    case 4:
        RenderMini();
        break;
    case 5:
        RenderWeek();
        break;
    case 6:
        RenderCard(TRUE);
        break;
    case 7:
        RenderIconBig();
        break;
    case 8:
        RenderHuge();
        break;
    default:
        RenderCard(FALSE);
        break;
    }
    POINT src = {0, 0};
    SIZE sz = {g_W, g_H};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, NULL, NULL, &sz, g_dc, &src, 0, &bf, ULW_ALPHA);
}

/* ------------------------------------------------------------------ */
/* 最奥への配置: デスクトップ本体 (Progman / WorkerW) の直上                 */
/* (検索バー・画像・時計と同じ方式。それらのウィンドウは重なり順の判定で飛ばす)  */
/* ------------------------------------------------------------------ */
static BOOL ClassIs(HWND w, const wchar_t *name)
{
    wchar_t c[32];
    return GetClassNameW(w, c, 32) && wcscmp(c, name) == 0;
}

static BOOL IsDesktopHost(HWND w) { return ClassIs(w, L"Progman") || ClassIs(w, L"WorkerW"); }

static BOOL IsOurs(HWND w)
{
    return ClassIs(w, L"PromWeatherWnd") || ClassIs(w, L"PromClockWnd") || ClassIs(w, L"PromPhotoFrameWnd") ||
           ClassIs(w, L"PromSearchBarWnd") || ClassIs(w, L"PromSysMonWnd");
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
    g_x = mi.rcWork.left + S(40); /* 左上 (時計は右上) */
    g_y = mi.rcWork.top + S(40);
}

static LRESULT CALLBACK WeatherProc(HWND h, UINT m, WPARAM w, LPARAM l);

static void WeatherCreate(void)
{
    if (g_hwnd || !g_on || !g_ready)
        return;
    RebuildFonts();
    CalcSize(&g_W, &g_H);
    if (g_x == POS_AUTO || g_y == POS_AUTO)
        AutoPos();
    ClampToWork();
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"PromWeatherWnd", L"PromWeather", WS_POPUP, g_x, g_y,
                             g_W, g_H, NULL, NULL, g_hInst, NULL);
    if (!g_hwnd)
        return;
    if (!MakeBuffers())
    {
        DestroyWindow(g_hwnd);
        return;
    }
    WeatherRender();
    ZPlace();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    ZPlace();
    SetTimer(g_hwnd, TIMER_Z, 1000, NULL);
    SetTimer(g_hwnd, TIMER_FETCH, FETCH_OK_MS, NULL);
    StartFetch(); /* 表示と同時に最新の天気を取りに行く (キャッシュがあればそれを先に表示済み) */
}

static void WeatherDestroyWin(void)
{
    if (g_hwnd)
        DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    g_fetching = FALSE;
    FreeBuffers();
}

/* 大きさ・デザインが変わったとき: 窓の大きさごと作り直す (位置は左上を保つ) */
static void WeatherRelayout(void)
{
    if (!g_hwnd)
        return;
    FreeBuffers();
    RebuildFonts();
    CalcSize(&g_W, &g_H);
    SetWindowPos(g_hwnd, NULL, 0, 0, g_W, g_H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (MakeBuffers())
        WeatherRender();
}

/* 場所が変わったとき: 表示中の天気を捨て (キャッシュがあれば使い)、取り直す */
static void LocChanged(void)
{
    g_gen++;
    memset(&g_data, 0, sizeof(g_data));
    g_failed = FALSE;
    LoadCache();
    SaveWeather();
    if (g_hwnd)
    {
        WeatherRender();
        SetTimer(g_hwnd, TIMER_FETCH, FETCH_OK_MS, NULL);
        StartFetch();
    }
    Settings_RefreshWeather();
}

/* ------------------------------------------------------------------ */
/* 緯度・経度の入力ダイアログ                                              */
/* ------------------------------------------------------------------ */
#define CD_EDIT 100
typedef struct
{
    wchar_t text[96];
    double lat, lon;
    BOOL ok;
    HWND edit;
    HFONT font;
} CoordState;

/* "35.6895, 139.6917" / "35.6895 139.6917" のどちらも受け付ける */
static BOOL ParseLatLon(const wchar_t *s, double *la, double *lo)
{
    wchar_t *e;
    double a = wcstod(s, &e);
    if (e == s)
        return FALSE;
    while (*e == L',' || *e == L' ' || *e == L'\t' || *e == 0x3000 || *e == 0x3001 || *e == 0xFF0C)
        e++;
    wchar_t *e2;
    double b = wcstod(e, &e2);
    if (e2 == e)
        return FALSE;
    if (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0)
        return FALSE;
    *la = a;
    *lo = b;
    return TRUE;
}

static LRESULT CALLBACK CoordProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    CoordState *st = (CoordState *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m)
    {
    case WM_CREATE:
    {
        st = (CoordState *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        HINSTANCE hi = GetModuleHandleW(NULL);
        st->font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HWND lb = CreateWindowExW(0, L"STATIC", L"緯度, 経度 (例: 35.6895, 139.6917)", WS_CHILD | WS_VISIBLE, S(12), S(12),
                                  S(356), S(20), h, NULL, hi, NULL);
        st->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   S(12), S(36), S(356), S(26), h, (HMENU)(INT_PTR)CD_EDIT, hi, NULL);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(196), S(78),
                                  S(80), S(28), h, (HMENU)(INT_PTR)IDOK, hi, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(288),
                                  S(78), S(80), S(28), h, (HMENU)(INT_PTR)IDCANCEL, hi, NULL);
        SendMessageW(lb, WM_SETFONT, (WPARAM)st->font, TRUE);
        SendMessageW(st->edit, WM_SETFONT, (WPARAM)st->font, TRUE);
        SendMessageW(ok, WM_SETFONT, (WPARAM)st->font, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)st->font, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK)
        {
            GetWindowTextW(st->edit, st->text, 96);
            if (ParseLatLon(st->text, &st->lat, &st->lon))
            {
                st->ok = TRUE;
                DestroyWindow(h);
            }
            else
                MessageBoxW(h, L"緯度 (-90〜90) と 経度 (-180〜180) を、カンマか空白で区切って入力してください。\n例: 35.6895, 139.6917",
                            L"天気", MB_OK | MB_ICONINFORMATION);
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

/* 緯度・経度を入力して、場所を「指定地点」にする。OK なら TRUE */
BOOL Weather_PickCoords(HWND owner)
{
    EnsureLoaded();
    static BOOL reg;
    HINSTANCE hi = GetModuleHandleW(NULL);
    if (!reg)
    {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CoordProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"PromWeatherCoord";
        RegisterClassExW(&wc);
        reg = TRUE;
    }
    CoordState st;
    memset(&st, 0, sizeof(st));
    double la, lo;
    EffLoc(&la, &lo);
    _snwprintf(st.text, 96, L"%.4f, %.4f", la, lo);
    st.text[95] = 0;

    if (owner && !IsWindow(owner))
        owner = NULL;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    RECT rc = {0, 0, S(380), S(120)};
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 3;

    HWND dlg = CreateWindowExW(ex, L"PromWeatherCoord", L"緯度・経度を指定", style, x, y, w, h, owner, NULL, hi, &st);
    if (!dlg)
        return FALSE;
    if (owner)
        EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(st.edit);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0)
        if (!IsDialogMessage(dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    if (owner)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (st.font)
        DeleteObject(st.font);
    if (!st.ok)
        return FALSE;
    g_lat = st.lat;
    g_lon = st.lon;
    g_city = N_CITIES;
    LocChanged();
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* 右クリックメニュー / ウィンドウ                                          */
/* ------------------------------------------------------------------ */
static void ShowWeatherMenu(void)
{
    HMENU menu = CreatePopupMenu();
    HMENU vs = CreatePopupMenu(), cs = CreatePopupMenu();
    for (int i = 0; i < N_VARIANTS; i++)
        AppendMenuW(vs, MF_STRING | (g_var == i ? MF_CHECKED : 0), MENU_VAR + i, kVarNames[i]);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)vs, L"デザイン");
    for (int i = 0; i <= N_CITIES; i++)
        AppendMenuW(cs, MF_STRING | (g_city == i ? MF_CHECKED : 0), MENU_CITY + i, Weather_CityName(i));
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)cs, L"場所");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_unit == 0 ? MF_CHECKED : 0), MENU_UNIT_C, L"摂氏 (℃)");
    AppendMenuW(menu, MF_STRING | (g_unit == 1 ? MF_CHECKED : 0), MENU_UNIT_F, L"華氏 (℉)");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_REFRESH, L"今すぐ更新");
    AppendMenuW(menu, MF_STRING | (g_movable ? MF_CHECKED : 0), MENU_MOVABLE, L"ドラッグで移動できるようにする");
    AppendMenuW(menu, MF_STRING, MENU_HIDE, L"天気を消す");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, MENU_SETTINGS, L"アプリ設定…");
    POINT pt;
    GetCursorPos(&pt);
    HWND h = g_hwnd;
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
    DestroyMenu(menu); /* サブメニューも一緒に破棄される */
    PostMessageW(h, WM_NULL, 0, 0);
    if (cmd >= MENU_VAR && cmd < MENU_VAR + N_VARIANTS)
        Weather_SetVariant(cmd - MENU_VAR);
    else if (cmd >= MENU_CITY && cmd <= MENU_CITY + N_CITIES)
    {
        int c = cmd - MENU_CITY;
        if (c == N_CITIES)
            Weather_PickCoords(h);
        else
            Weather_SetCity(c);
    }
    else
        switch (cmd)
        {
        case MENU_UNIT_C:
            Weather_SetUnit(0);
            break;
        case MENU_UNIT_F:
            Weather_SetUnit(1);
            break;
        case MENU_REFRESH:
            Weather_Refresh();
            break;
        case MENU_MOVABLE:
            Weather_SetMovable(!g_movable);
            break;
        case MENU_HIDE:
            Weather_SetEnabled(FALSE); /* 窓が破棄されるので、これ以降触らない */
            break;
        case MENU_SETTINGS:
            Settings_OpenPage(6);
            return;
        default:
            return;
        }
    Settings_RefreshWeather();
}

static LRESULT CALLBACK WeatherProc(HWND h, UINT m, WPARAM w, LPARAM l)
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
        SaveWeather();
        return 0;
    }

    case WM_WEATHER:
    { /* 別スレッドの取得結果 */
        FetchRes *rs = (FetchRes *)l;
        if (rs)
        {
            if (rs->gen == g_gen)
            {
                g_fetching = FALSE;
                if (rs->ok)
                {
                    g_data = rs->d;
                    g_failed = FALSE;
                    SaveCache();
                    SetTimer(h, TIMER_FETCH, FETCH_OK_MS, NULL);
                }
                else
                {
                    g_failed = TRUE;
                    SetTimer(h, TIMER_FETCH, FETCH_RETRY_MS, NULL);
                }
                WeatherRender();
                Settings_RefreshWeather();
            }
            free(rs);
        }
        return 0;
    }

    case WM_TIMER:
        if (w == TIMER_FETCH)
        {
            SetTimer(h, TIMER_FETCH, FETCH_OK_MS, NULL);
            StartFetch();
        }
        else if (w == TIMER_Z)
            ZPlace();
        return 0;

    case WM_NCRBUTTONDOWN:
        return 0;
    case WM_NCRBUTTONUP: /* 移動できる状態では全面が HTCAPTION なので、ここで右クリックを受ける */
    case WM_CONTEXTMENU:
        ShowWeatherMenu();
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
BOOL Weather_Enabled(void)
{
    EnsureLoaded();
    return g_on;
}

void Weather_SetEnabled(BOOL on)
{
    EnsureLoaded();
    on = on ? TRUE : FALSE;
    if (on == g_on)
        return;
    g_on = on;
    SaveWeather();
    if (!g_ready)
        return;
    if (on)
        WeatherCreate();
    else
        WeatherDestroyWin();
    Settings_RefreshWeather();
}

int Weather_VariantCount(void) { return N_VARIANTS; }

const wchar_t *Weather_VariantName(int i)
{
    return (i >= 0 && i < N_VARIANTS) ? kVarNames[i] : L"";
}

int Weather_GetVariant(void)
{
    EnsureLoaded();
    return g_var;
}

void Weather_SetVariant(int v)
{
    EnsureLoaded();
    v = ClampI(v, 0, N_VARIANTS - 1);
    if (v == g_var)
        return;
    g_var = v;
    SaveWeather();
    WeatherRelayout();
}

int Weather_CityCount(void) { return N_CITIES + 1; }

const wchar_t *Weather_CityName(int i)
{
    if (i >= 0 && i < N_CITIES)
        return kCities[i].name;
    return L"緯度・経度を指定…";
}

int Weather_GetCity(void)
{
    EnsureLoaded();
    return g_city;
}

/* 都市を選ぶ (N_CITIES = 指定地点 は、すでに入力済みの緯度・経度を使う) */
void Weather_SetCity(int i)
{
    EnsureLoaded();
    i = ClampI(i, 0, N_CITIES);
    if (i == g_city)
        return;
    g_city = i;
    LocChanged();
}

/* 設定のドロップダウンに出す、いまの場所の表示 */
void Weather_LocLabel(wchar_t *out, int cch)
{
    EnsureLoaded();
    if (g_city >= 0 && g_city < N_CITIES)
        wcsncpy(out, kCities[g_city].name, cch - 1);
    else
        _snwprintf(out, cch, L"指定地点 (%.2f, %.2f)", g_lat, g_lon);
    out[cch - 1] = 0;
}

void Weather_GetLatLon(double *lat, double *lon)
{
    EnsureLoaded();
    EffLoc(lat, lon);
}

int Weather_GetUnit(void)
{
    EnsureLoaded();
    return g_unit;
}

void Weather_SetUnit(int unit)
{
    EnsureLoaded();
    unit = unit ? 1 : 0;
    if (unit == g_unit)
        return;
    g_unit = unit;
    SaveWeather();
    WeatherRender();
}

int Weather_GetSize(void)
{
    EnsureLoaded();
    return g_size;
}

void Weather_SetSize(int pct)
{
    EnsureLoaded();
    pct = ClampI(pct, 60, 200);
    if (pct == g_size)
        return;
    g_size = pct;
    SaveWeather();
    WeatherRelayout();
}

int Weather_GetOpacity(void)
{
    EnsureLoaded();
    return g_opa;
}

void Weather_SetOpacity(int pct)
{
    EnsureLoaded();
    pct = ClampI(pct, 30, 100);
    if (pct == g_opa)
        return;
    g_opa = pct;
    SaveWeather();
    WeatherRender();
}

BOOL Weather_Movable(void)
{
    EnsureLoaded();
    return g_movable;
}

void Weather_SetMovable(BOOL on)
{
    EnsureLoaded();
    g_movable = on ? TRUE : FALSE;
    SaveWeather();
}

void Weather_ResetPosition(void)
{
    EnsureLoaded();
    g_x = g_y = POS_AUTO;
    if (g_hwnd)
    {
        AutoPos();
        ClampToWork();
        SetWindowPos(g_hwnd, NULL, g_x, g_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SaveWeather();
}

/* 今すぐ更新 */
void Weather_Refresh(void)
{
    EnsureLoaded();
    if (!g_hwnd)
        return;
    g_failed = FALSE;
    StartFetch();
    WeatherRender();
}

/* 設定ページに出す、取得の状況 */
void Weather_Status(wchar_t *out, int cch)
{
    EnsureLoaded();
    if (!g_on)
        _snwprintf(out, cch, L"天気の表示がオフです");
    else if (g_data.valid && g_failed)
        _snwprintf(out, cch, L"更新に失敗しました (最終更新 %s)", g_data.upd[0] ? g_data.upd : L"-");
    else if (g_data.valid)
        _snwprintf(out, cch, L"最終更新 %s%s", g_data.upd[0] ? g_data.upd : L"-", g_fetching ? L"  ·  更新中…" : L"");
    else if (g_failed)
        _snwprintf(out, cch, L"取得できませんでした。ネットワーク接続を確認してください");
    else
        _snwprintf(out, cch, L"取得中…");
    out[cch - 1] = 0;
}

void Weather_Init(HINSTANCE hInst)
{
    g_hInst = hInst;
    EnsureLoaded();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WeatherProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PromWeatherWnd";
    RegisterClassExW(&wc);

    g_ready = TRUE;
    WeatherCreate();
    if (g_hwnd)
        SaveWeather(); /* 自動配置した位置を記録 */
}

void Weather_Shutdown(void)
{
    g_ready = FALSE;
    WeatherDestroyWin();
    FreeFonts();
}