// smooth.cpp — smooly: momentum smooth-scroll engine + Win32 settings GUI + tray.
// Build with build.ps1 (g++ / w64devkit). See README.md.

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define OEMRESOURCE          // exposes the OCR_* system-cursor ids
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "config.h"

// Some OCR_* system-cursor ids are missing from older MinGW headers; define any gaps.
#ifndef OCR_NORMAL
#define OCR_NORMAL      32512
#endif
#ifndef OCR_IBEAM
#define OCR_IBEAM       32513
#endif
#ifndef OCR_WAIT
#define OCR_WAIT        32514
#endif
#ifndef OCR_CROSS
#define OCR_CROSS       32515
#endif
#ifndef OCR_UP
#define OCR_UP          32516
#endif
#ifndef OCR_SIZENWSE
#define OCR_SIZENWSE    32642
#endif
#ifndef OCR_SIZENESW
#define OCR_SIZENESW    32643
#endif
#ifndef OCR_SIZEWE
#define OCR_SIZEWE      32644
#endif
#ifndef OCR_SIZENS
#define OCR_SIZENS      32645
#endif
#ifndef OCR_SIZEALL
#define OCR_SIZEALL     32646
#endif
#ifndef OCR_NO
#define OCR_NO          32648
#endif
#ifndef OCR_HAND
#define OCR_HAND        32649
#endif
#ifndef OCR_APPSTARTING
#define OCR_APPSTARTING 32650
#endif
#ifndef OCR_HELP
#define OCR_HELP        32651
#endif

// ------------------------------------------------------------------ globals
static const ULONG_PTR kInjectTag = 0xC0FFEE;  // stamp on our own synthetic events

static std::mutex        g_mutex;              // guards g_cfg + physics state
static std::atomic<bool> g_running{true};
static HHOOK             g_hook   = nullptr;
static Config           g_cfg;

// physics state (guarded by g_mutex)
static double   g_remV = 0.0, g_remH = 0.0;    // scroll distance still owed per axis
static double   g_accV = 0.0, g_accH = 0.0;    // sub-unit remainder per axis
static double   g_burstV = 0.0, g_burstH = 0.0;// recent-scroll accumulator (acceleration)
static LONGLONG g_lastV = 0,  g_lastH = 0;     // last notch time (QPC ticks)
static LONGLONG g_qpcFreq = 1;

// click-gesture request, produced in the hook and consumed by the animation thread
static std::atomic<int> g_gesture{0};          // 0 none · 1 word · 2 line · 3 word+copy
static std::atomic<int> g_zoomDelta{0};        // queued Ctrl+wheel (zoom) delta
// a button action to run on the animation thread (carries a key combo / text)
struct PendingAct { int action = 0; int combo = 0; std::wstring text; };
static PendingAct g_act;                        // guarded by g_mutex
static std::atomic<bool> g_actReady{false};

static std::vector<BtnMap> g_maps;             // registered buttons (GUI-thread only)
// active managed press — shared between hook (GUI thread) and animation thread
static std::atomic<int>      g_pBtn{0};        // button code of the active press (0 = none)
static std::atomic<LONGLONG> g_pDownAt{0};
static std::atomic<bool>     g_pHoldFired{false};
static std::atomic<bool>     g_pScrolled{false};
static std::atomic<int>      g_pHoldAction{0}; // hold action of the active press
static std::atomic<int>      g_pHoldCombo{0};  // hold key-combo (if hold action = shortcut)
static std::wstring          g_pHoldText;      // hold text (guarded by g_mutex)
static std::atomic<int>      g_pDragAction{0}; // drag action of the active press
static std::atomic<bool>     g_pDragged{false};
static std::atomic<int>      g_pendAction{0};  // deferred single-click action
static std::atomic<int>      g_pendCombo{0};
static std::wstring          g_pendText;       // deferred single-click text (guarded)
static std::atomic<LONGLONG> g_pendDeadline{0};
static std::atomic<int>      g_panV{0}, g_panH{0};   // queued drag-pan wheel
static std::atomic<int>      g_dragCmd{0};     // 1 desktop-left · 2 desktop-right · 3 Task View
static int      g_lastClickBtn = 0; static LONGLONG g_lastClickAt = 0;   // double-click (hook only)
static LONG     g_dragLastX = 0, g_dragLastY = 0; static int g_dragTotal = 0, g_dragAccX = 0;  // drag (hook only)
static HHOOK    g_keyCapHook = nullptr;
static std::atomic<int> g_capturedKey{-1};     // -1 capturing · 0 cancel · else packed combo

// shake-to-locate state (touched only by the animation thread, except g_bigUntil)
static std::atomic<LONGLONG> g_bigUntil{0};    // keep cursor big until this tick
static double g_shakeScale = 1.0;              // current eased zoom factor
static bool   g_cursorBig  = false;            // are the system cursors currently scaled?
static bool   g_othersBig  = false;            // have the non-arrow cursors been set big?
static int    g_lastShakePx = 0;               // last applied arrow size (throttle re-applies)

// GUI
static HWND            g_wnd  = nullptr;
static HFONT           g_font = nullptr;
static HICON           g_icon = nullptr;
static NOTIFYICONDATAW g_nid  = {};
static double          S      = 1.0;           // DPI scale
static int             g_dpi  = 96;
static HFONT           g_fontIcon = nullptr, g_fontTitle = nullptr, g_fontSmall = nullptr;
static int                g_page = 0;          // active sidebar page
static int                g_hoverItem = -1;    // hovered sidebar item
static int                g_sysSpeed = 10;     // system pointer speed at launch
static int                g_origMouse[3] = { 6, 10, 1 };  // saved accel params to restore
static ULONG_PTR          g_gdiplus = 0;       // GDI+ token (high-quality cursor scaling)

// near-black premium palette (SQL-Studio-style) + bright blue accent
static const COLORREF C_SIDE   = RGB(0x0b, 0x0b, 0x0d);
static const COLORREF C_BG     = RGB(0x0f, 0x0f, 0x12);
static const COLORREF C_CARD   = RGB(0x17, 0x17, 0x1b);
static const COLORREF C_LINE   = RGB(0x26, 0x26, 0x2c);
static const COLORREF C_HOVER  = RGB(0x1e, 0x1e, 0x23);
static const COLORREF C_TEXT   = RGB(0xf2, 0xf2, 0xf5);
static const COLORREF C_TEXT2  = RGB(0x82, 0x82, 0x8c);
static const COLORREF C_ACCENT = RGB(0x2f, 0x8f, 0xff);
static const COLORREF C_KNOB   = RGB(0x3a, 0x3a, 0x42);
static HBRUSH g_brSide = nullptr, g_brBg = nullptr;

// declarative rows -> lets WM_PAINT draw cards + labels from stored geometry.
// top/h = row rect; cx/cy/ch = the control's own rect (for scroll repositioning).
struct UiRow { int page, card, kind, top, h, cx, cy, ch; HWND ctrl; std::wstring label; };  // kind: 0 toggle,1 dropdown,2 slider,4 capture,5 remove,6 selector pill
static std::vector<UiRow> g_rows;
static int g_selBtn = 0;   // Buttons page: which registered button is shown

// HugeIcons (stroke, 24x24 viewBox) rendered via the mini SVG path renderer below.
static const char* IC_SCROLL[] = {
    "M4.74 17.089c.19 2.391 2.084 4.422 4.525 4.723c.898.11 1.81.188 2.735.188s1.837-.078 2.735-.188c2.44-.301 4.334-2.332 4.524-4.723c.132-1.657.241-3.357.241-5.089s-.11-3.432-.24-5.089c-.19-2.391-2.084-4.422-4.525-4.723C13.837 2.078 12.925 2 12 2s-1.837.078-2.735.188c-2.44.3-4.335 2.332-4.524 4.723C4.609 8.568 4.5 10.268 4.5 12s.109 3.432.24 5.089Z",
    "M11.988 6.84v4.92m-1.984-3.9c.982-1.02 1.58-1.92 2.036-1.855c.383-.003.742.595 1.964 1.855m-.008 3.282c-.982 1.02-1.58 1.92-2.036 1.855c-.383.004-.742-.595-1.964-1.855" };
static const char* IC_POINTER[] = {
    "m9.803 4.63l6.033 2.36c3.48 1.362 5.22 2.043 5.163 3.123c-.058 1.08-1.874 1.576-5.506 2.566c-1.081.295-1.622.442-1.997.817s-.522.916-.817 1.997c-.99 3.632-1.486 5.448-2.566 5.506s-1.76-1.683-3.122-5.163L4.63 9.803C3.204 6.159 2.49 4.338 3.414 3.414c.924-.923 2.745-.21 6.389 1.216Z" };
static const char* IC_MOUSE[] = {
    "M4.74 17.089c.19 2.391 2.084 4.422 4.525 4.723c.898.11 1.81.188 2.735.188s1.837-.078 2.735-.188c2.44-.301 4.334-2.332 4.524-4.723c.132-1.657.241-3.357.241-5.089s-.11-3.432-.24-5.089c-.19-2.391-2.084-4.422-4.525-4.723C13.837 2.078 12.925 2 12 2s-1.837.078-2.735.188c-2.44.3-4.335 2.332-4.524 4.723C4.609 8.568 4.5 10.268 4.5 12s.109 3.432.24 5.089Z",
    "M12 6v3" };
static const char* IC_CLICK[] = {
    "M6.24 17.089c.19 2.391 2.084 4.422 4.525 4.723c.898.11 1.81.188 2.735.188s1.837-.078 2.735-.188c2.44-.301 4.334-2.332 4.524-4.723c.132-1.657.241-3.357.241-5.089s-.11-3.432-.24-5.089c-.19-2.391-2.084-4.422-4.525-4.723C15.337 2.078 14.425 2 13.5 2s-1.837.078-2.735.188c-2.44.3-4.335 2.332-4.524 4.723C6.109 8.568 6 10.268 6 12s.109 3.432.24 5.089Z",
    "M12 7.5c0-.466 0-.699.076-.883a1 1 0 0 1 .541-.54C12.801 6 13.034 6 13.5 6s.699 0 .883.076a1 1 0 0 1 .54.541c.077.184.077.417.077.883v1c0 .466 0 .699-.076.883a1 1 0 0 1-.541.54c-.184.077-.417.077-.883.077s-.699 0-.883-.076a1 1 0 0 1-.54-.541C12 9.199 12 8.966 12 8.5z" };
static const char* IC_MOD[] = {
    "M15 9v6H9V9zm0 6h3a3 3 0 1 1-3 3zm-6 .002H6a3 3 0 1 0 3 3zM15 9V6a3 3 0 1 1 3 3zM9 9V6a3 3 0 1 0-3 3z" };
static const char* IC_GEN[] = {
    "m21.318 7.141l-.494-.856c-.373-.648-.56-.972-.878-1.101c-.317-.13-.676-.027-1.395.176l-1.22.344c-.459.106-.94.046-1.358-.17l-.337-.194a2 2 0 0 1-.788-.967l-.334-.998c-.22-.66-.33-.99-.591-1.178c-.261-.19-.609-.19-1.303-.19h-1.115c-.694 0-1.041 0-1.303.19c-.261.188-.37.518-.59 1.178l-.334.998a2 2 0 0 1-.789.967l-.337.195c-.418.215-.9.275-1.358.17l-1.22-.345c-.719-.203-1.078-.305-1.395-.176c-.318.129-.505.453-.878 1.1l-.493.857c-.35.608-.525.911-.491 1.234c.034.324.268.584.736 1.105l1.031 1.153c.252.319.431.875.431 1.375s-.179 1.056-.43 1.375l-1.032 1.152c-.468.521-.702.782-.736 1.105s.14.627.49 1.234l.494.857c.373.647.56.971.878 1.1s.676.028 1.395-.176l1.22-.344a2 2 0 0 1 1.359.17l.336.194c.36.23.636.57.788.968l.334.997c.22.66.33.99.591 1.18c.262.188.609.188 1.303.188h1.115c.694 0 1.042 0 1.303-.189s.371-.519.59-1.179l.335-.997c.152-.399.428-.738.788-.968l.336-.194c.42-.215.9-.276 1.36-.17l1.22.344c.718.204 1.077.306 1.394.177c.318-.13.505-.454.878-1.101l.493-.857c.35-.607.525-.91.491-1.234s-.268-.584-.736-1.105l-1.031-1.152c-.252-.32-.431-.875-.431-1.375s.179-1.056.43-1.375l1.032-1.153c.468-.52.702-.781.736-1.105s-.14-.626-.49-1.234Z",
    "M15.52 12a3.5 3.5 0 1 1-7 0a3.5 3.5 0 0 1 7 0Z" };

struct SvgIcon { const char* const* paths; int n; };
static const SvgIcon kIcons[5] = {
    { IC_SCROLL, 2 }, { IC_POINTER, 1 }, { IC_CLICK, 2 }, { IC_MOD, 1 }, { IC_GEN, 2 }
};
static const SvgIcon kMouseIcon = { IC_MOUSE, 2 };   // app / tray icon
static const wchar_t* const kPageNames[5] = { L"Scrolling", L"Pointer", L"Buttons", L"Modifier Keys", L"General" };
static const int kNumPages = 5;

#define ID_ENABLE  1001
#define ID_SMOOTH  1002
#define ID_SPEED   1003
#define ID_ACCEL   1004
#define ID_REVERSE 1005
#define ID_HORIZ   1006
#define ID_SHAKE   1008
#define ID_THEME   1009
#define ID_SIZE    1010
#define ID_SHIFTH  1011
#define ID_CTRLT   1012
#define ID_GESTURE 1013
#define ID_PSPEED  1015
#define ID_NOACCEL 1016
#define ID_CAPTURE 1022
#define ID_DYN_BASE 3000   // dynamic button dropdowns: 3000 + mapIndex*8 + trigger(0..3)
#define ID_SEL_BASE 3700   // 3700 + mapIndex = Buttons-page selector pill
#define ID_DYN_REMOVE 3900 // 3900 + mapIndex = "remove this button"
#define ID_STARTUP 1007
#define ID_SUB     1100
#define ID_FOOT    1101
#define WM_TRAY   (WM_APP + 1)
#define IDM_OPEN   2001
#define IDM_TOGGLE 2002
#define IDM_QUIT   2003

static int P(int v) { return (int)(v * S + 0.5); }   // scale a base-96 coordinate

static LONGLONG nowTicks() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double   ticksToMs(LONGLONG d) { return (double)d * 1000.0 / (double)g_qpcFreq; }

// ------------------------------------------------------------------ config I/O
static std::wstring cfgPath() {
    wchar_t buf[MAX_PATH]; buf[0] = 0;
    GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = std::wstring(buf) + L"\\smooly";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

static void LoadConfig() {
    std::wstring p = cfgPath();
    g_cfg.enabled    = GetPrivateProfileIntW(L"scroll", L"enabled",    1, p.c_str());
    g_cfg.smoothness = GetPrivateProfileIntW(L"scroll", L"smoothness", 3, p.c_str());
    g_cfg.speed      = GetPrivateProfileIntW(L"scroll", L"speed",      1, p.c_str());
    g_cfg.accel      = GetPrivateProfileIntW(L"scroll", L"accel",      2, p.c_str());
    g_cfg.reverse    = GetPrivateProfileIntW(L"scroll", L"reverse",    0, p.c_str());
    g_cfg.horizontal = GetPrivateProfileIntW(L"scroll", L"horizontal", 1, p.c_str());
    g_cfg.shiftHoriz = GetPrivateProfileIntW(L"scroll", L"shiftHoriz", 1, p.c_str());
    g_cfg.ctrlTurbo  = GetPrivateProfileIntW(L"scroll", L"ctrlTurbo",  0, p.c_str());
    g_cfg.gestures   = GetPrivateProfileIntW(L"scroll", L"gestures",   0, p.c_str());
    g_cfg.shake      = GetPrivateProfileIntW(L"scroll", L"shake",      1, p.c_str());
    g_cfg.startup    = GetPrivateProfileIntW(L"scroll", L"startup",    0, p.c_str());
    wchar_t tb[128];
    GetPrivateProfileStringW(L"cursor", L"theme", L"", tb, 128, p.c_str());
    g_cfg.theme = tb;
    g_cfg.size  = GetPrivateProfileIntW(L"cursor", L"size", 1, p.c_str());
    g_cfg.pointerSpeed = GetPrivateProfileIntW(L"pointer", L"speed",   0, p.c_str());
    g_cfg.noAccel      = GetPrivateProfileIntW(L"pointer", L"noAccel", 0, p.c_str());
    g_maps.clear();
    int n = GetPrivateProfileIntW(L"buttons", L"count", -1, p.c_str());
    if (n < 0) { g_maps.push_back({ 4 }); g_maps.push_back({ 5 }); }   // seed Back / Forward
    else for (int i = 0; i < n; i++) {
        wchar_t k[16]; BtnMap m;
        wsprintfW(k, L"b%d", i);  m.button = GetPrivateProfileIntW(L"buttons", k, 4, p.c_str());
        wsprintfW(k, L"c%d", i);  m.click  = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"d%d", i);  m.dbl    = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"h%d", i);  m.hold   = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"s%d", i);  m.scroll = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"g%d", i);  m.drag   = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"kc%d", i); m.keyC   = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"kd%d", i); m.keyD   = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wsprintfW(k, L"kh%d", i); m.keyH   = GetPrivateProfileIntW(L"buttons", k, 0, p.c_str());
        wchar_t tb[512];
        wsprintfW(k, L"tc%d", i); GetPrivateProfileStringW(L"buttons", k, L"", tb, 512, p.c_str()); m.txtC = tb;
        wsprintfW(k, L"td%d", i); GetPrivateProfileStringW(L"buttons", k, L"", tb, 512, p.c_str()); m.txtD = tb;
        wsprintfW(k, L"th%d", i); GetPrivateProfileStringW(L"buttons", k, L"", tb, 512, p.c_str()); m.txtH = tb;
        g_maps.push_back(m);
    }
}

static void SaveConfig() {
    std::wstring p = cfgPath();
    auto W = [&](const wchar_t* k, int v) {
        wchar_t b[16]; wsprintfW(b, L"%d", v);
        WritePrivateProfileStringW(L"scroll", k, b, p.c_str());
    };
    W(L"enabled", g_cfg.enabled);   W(L"smoothness", g_cfg.smoothness);
    W(L"speed",   g_cfg.speed);     W(L"accel",      g_cfg.accel);
    W(L"reverse", g_cfg.reverse);   W(L"horizontal", g_cfg.horizontal);
    W(L"shiftHoriz", g_cfg.shiftHoriz); W(L"ctrlTurbo", g_cfg.ctrlTurbo);
    W(L"gestures", g_cfg.gestures);
    W(L"shake",   g_cfg.shake);
    W(L"startup", g_cfg.startup);
    WritePrivateProfileStringW(L"cursor", L"theme", g_cfg.theme.c_str(), p.c_str());
    auto Wc = [&](const wchar_t* sec, const wchar_t* k, int v) {
        wchar_t b[16]; wsprintfW(b, L"%d", v); WritePrivateProfileStringW(sec, k, b, p.c_str());
    };
    Wc(L"cursor",  L"size",    g_cfg.size);
    Wc(L"pointer", L"speed",   g_cfg.pointerSpeed);
    Wc(L"pointer", L"noAccel", g_cfg.noAccel);
    WritePrivateProfileStringW(L"buttons", nullptr, nullptr, p.c_str());   // clear stale entries
    Wc(L"buttons", L"count", (int)g_maps.size());
    for (size_t i = 0; i < g_maps.size(); i++) {
        wchar_t k[16]; const BtnMap& m = g_maps[i];
        wsprintfW(k, L"b%d", (int)i);  Wc(L"buttons", k, m.button);
        wsprintfW(k, L"c%d", (int)i);  Wc(L"buttons", k, m.click);
        wsprintfW(k, L"d%d", (int)i);  Wc(L"buttons", k, m.dbl);
        wsprintfW(k, L"h%d", (int)i);  Wc(L"buttons", k, m.hold);
        wsprintfW(k, L"s%d", (int)i);  Wc(L"buttons", k, m.scroll);
        wsprintfW(k, L"g%d", (int)i);  Wc(L"buttons", k, m.drag);
        wsprintfW(k, L"kc%d", (int)i); Wc(L"buttons", k, m.keyC);
        wsprintfW(k, L"kd%d", (int)i); Wc(L"buttons", k, m.keyD);
        wsprintfW(k, L"kh%d", (int)i); Wc(L"buttons", k, m.keyH);
        wsprintfW(k, L"tc%d", (int)i); WritePrivateProfileStringW(L"buttons", k, m.txtC.c_str(), p.c_str());
        wsprintfW(k, L"td%d", (int)i); WritePrivateProfileStringW(L"buttons", k, m.txtD.c_str(), p.c_str());
        wsprintfW(k, L"th%d", (int)i); WritePrivateProfileStringW(L"buttons", k, m.txtH.c_str(), p.c_str());
    }
}

static void SetStartup(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring q = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(k, L"smooly", 0, REG_SZ,
                       (const BYTE*)q.c_str(), (DWORD)((q.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"smooly");
    }
    RegCloseKey(k);
}

static void ApplyPointerSpeed(int speed) {   // 1..20, the OS pointer sensitivity
    if (speed < 1) speed = 1; else if (speed > 20) speed = 20;
    // No SPIF_SENDCHANGE: the speed applies live immediately; broadcasting
    // WM_SETTINGCHANGE to every window on each drag tick is what made it lag.
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)speed, 0);
}
static void ApplyNoAccel(bool on) {          // toggle "enhance pointer precision"
    int m[3] = { on ? 0 : g_origMouse[0], on ? 0 : g_origMouse[1], on ? 0 : g_origMouse[2] };
    SystemParametersInfoW(SPI_SETMOUSE, 0, m, SPIF_SENDCHANGE);
}

// ------------------------------------------------------------------ cursor roles
// Every standard system cursor role: theme filename, registry value, OCR id, and
// whether it's animated (.ani — can't be runtime-scaled without losing animation).
// Default filenames follow the Bibata/clickgen convention; install.inf overrides.
struct ThemeRole { const wchar_t* key; const wchar_t* file; const wchar_t* reg; DWORD ocr; bool anim; };
static const ThemeRole kThemeRoles[] = {
    { L"pointer",     L"Pointer.cur",     L"Arrow",       OCR_NORMAL,      false },
    { L"help",        L"Help.cur",        L"Help",        OCR_HELP,        false },
    { L"work",        L"Work.ani",        L"AppStarting", OCR_APPSTARTING, true  },
    { L"busy",        L"Busy.ani",        L"Wait",        OCR_WAIT,        true  },
    { L"cross",       L"Cross.cur",       L"Crosshair",   OCR_CROSS,       false },
    { L"text",        L"Text.cur",        L"IBeam",       OCR_IBEAM,       false },
    { L"unavailable", L"Unavailable.cur", L"No",          OCR_NO,          false },
    { L"vert",        L"Vert.cur",        L"SizeNS",      OCR_SIZENS,      false },
    { L"horz",        L"Horz.cur",        L"SizeWE",      OCR_SIZEWE,      false },
    { L"dgn1",        L"Dgn1.cur",        L"SizeNWSE",    OCR_SIZENWSE,    false },
    { L"dgn2",        L"Dgn2.cur",        L"SizeNESW",    OCR_SIZENESW,    false },
    { L"move",        L"Move.cur",        L"SizeAll",     OCR_SIZEALL,     false },
    { L"alternate",   L"Alternate.cur",   L"UpArrow",     OCR_UP,          false },
    { L"link",        L"Link.cur",        L"Hand",        OCR_HAND,        false },
};
static const int kNumRoles = (int)(sizeof(kThemeRoles) / sizeof(kThemeRoles[0]));

// ------------------------------------------------------------------ shake to locate
static void RestoreCursors() {   // reload the originals from the active scheme
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
}

// Assemble a 32-bit alpha cursor from top-down straight-ARGB pixels + hotspot.
static HCURSOR CursorFromARGB(const BYTE* argb, int w, int h, int hotX, int hotY) {
    BITMAPINFO bo = {};
    bo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bo.bmiHeader.biWidth = w; bo.bmiHeader.biHeight = -h;
    bo.bmiHeader.biPlanes = 1; bo.bmiHeader.biBitCount = 32; bo.bmiHeader.biCompression = BI_RGB;
    void* dbits = nullptr;
    HDC md = CreateCompatibleDC(nullptr);
    HBITMAP color = CreateDIBSection(md, &bo, DIB_RGB_COLORS, &dbits, nullptr, 0);
    HCURSOR out = nullptr;
    if (color && dbits) {
        memcpy(dbits, argb, (size_t)w * h * 4);
        HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);   // ignored: alpha carries transparency
        std::vector<BYTE> zero((size_t)(((w + 15) / 16) * 2) * h, 0);
        SetBitmapBits(mask, (DWORD)zero.size(), zero.data());
        ICONINFO ni = { FALSE, (DWORD)hotX, (DWORD)hotY, mask, color };
        out = (HCURSOR)CreateIconIndirect(&ni);
        DeleteObject(mask);
    }
    if (color) DeleteObject(color);
    DeleteDC(md);
    return out;
}

// Build a crisp scaled copy of a cursor from an explicit source handle (NOT the
// live system cursor — scaling the live one each frame would compound). Recovers
// true straight alpha by rendering the cursor over black and over white and taking
// the per-pixel difference, so BOTH alpha and mask cursors (incl. the default Windows
// arrow) come out with correct transparency — no black box — then bicubic-scales.
static HCURSOR MakeScaledCursor(HCURSOR src, double scale) {
    if (!src) return nullptr;
    ICONINFO ii = {};
    if (!GetIconInfo(src, &ii)) return nullptr;

    BITMAP bm = {};
    int baseW = 32, baseH = 32;
    if (ii.hbmColor && GetObject(ii.hbmColor, sizeof(bm), &bm)) { baseW = bm.bmWidth; baseH = bm.bmHeight; }
    else if (ii.hbmMask && GetObject(ii.hbmMask, sizeof(bm), &bm)) { baseW = bm.bmWidth; baseH = bm.bmHeight / 2; }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    int w = (int)(baseW * scale + 0.5), h = (int)(baseH * scale + 0.5);
    if (w < 1) w = 1; if (h < 1) h = 1;
    int hotX = (int)(ii.xHotspot * scale + 0.5), hotY = (int)(ii.yHotspot * scale + 0.5);

    auto renderOn = [&](COLORREF bg, std::vector<BYTE>& out) {
        HDC screen = GetDC(nullptr); HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = baseW; bi.bmiHeader.biHeight = -baseH;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HBITMAP old = (HBITMAP)SelectObject(mem, dib);
        RECT rc = { 0, 0, baseW, baseH }; HBRUSH br = CreateSolidBrush(bg);
        FillRect(mem, &rc, br); DeleteObject(br);
        DrawIconEx(mem, 0, 0, src, baseW, baseH, 0, nullptr, DI_NORMAL);
        SelectObject(mem, old);
        out.resize((size_t)baseW * baseH * 4);
        if (bits) memcpy(out.data(), bits, out.size());
        DeleteObject(dib); DeleteDC(mem); ReleaseDC(nullptr, screen);
    };
    std::vector<BYTE> onBlack, onWhite;
    renderOn(RGB(0, 0, 0), onBlack);
    renderOn(RGB(255, 255, 255), onWhite);

    std::vector<BYTE> base((size_t)baseW * baseH * 4);   // straight BGRA
    for (size_t i = 0; i < base.size(); i += 4) {
        int d = (onWhite[i] - onBlack[i]) + (onWhite[i + 1] - onBlack[i + 1]) + (onWhite[i + 2] - onBlack[i + 2]);
        int a = 255 - d / 3; if (a < 0) a = 0; else if (a > 255) a = 255;
        for (int c = 0; c < 3; c++) {                    // un-premultiply the over-black color
            int v = a > 0 ? onBlack[i + c] * 255 / a : 0;
            base[i + c] = (BYTE)(v > 255 ? 255 : v);
        }
        base[i + 3] = (BYTE)a;
    }

    HCURSOR out = nullptr;
    Gdiplus::Bitmap srcBmp(baseW, baseH, baseW * 4, PixelFormat32bppARGB, base.data());
    Gdiplus::Bitmap dst(w, h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(&srcBmp, 0, 0, w, h);
    }
    Gdiplus::Rect rc(0, 0, w, h);
    Gdiplus::BitmapData bd;
    if (dst.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
        std::vector<BYTE> outBits((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            memcpy(outBits.data() + (size_t)y * w * 4, (BYTE*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)w * 4);
        dst.UnlockBits(&bd);
        out = CursorFromARGB(outBits.data(), w, h, hotX, hotY);
    }
    return out;
}

// Snapshot the currently-displayed cursors as the stable base for a shake zoom,
// so every animation frame scales from these (fixed) copies — no compounding.
static HCURSOR g_shakeBase[16] = {};
static void CaptureShakeBase() {
    for (int i = 0; i < kNumRoles; i++)
        g_shakeBase[i] = CopyCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(kThemeRoles[i].ocr)));
}
static void FreeShakeBase() {
    for (int i = 0; i < kNumRoles; i++) {
        if (g_shakeBase[i]) DestroyCursor(g_shakeBase[i]);
        g_shakeBase[i] = nullptr;
    }
}

// Called on every raw mouse move (hook thread only -> plain statics are safe).
static void DetectShake(const MSLLHOOKSTRUCT* ms) {
    if (!g_cfg.shake) return;
    static LONG     sLastX = 0;   static int  sSign = 0;   static bool sInit = false;
    static LONGLONG sRev[8] = {}; static int  sRevN = 0;

    LONG x = ms->pt.x;
    if (!sInit) { sLastX = x; sInit = true; return; }
    LONG dx = x - sLastX; sLastX = x;
    if ((dx < 0 ? -dx : dx) < kShakeMinDx) return;

    int sign = dx > 0 ? 1 : -1;
    if (sSign != 0 && sign != sSign) {                 // a direction reversal
        LONGLONG now = nowTicks();
        sRev[sRevN++ & 7] = now;
        int cnt = 0;
        for (LONGLONG t : sRev)
            if (t != 0 && ticksToMs(now - t) <= kShakeWindowMs) cnt++;
        if (cnt >= kShakeMinReversals)
            g_bigUntil.store(now + (LONGLONG)kShakeHoldMs * g_qpcFreq / 1000);
    }
    sSign = sign;
}

// ------------------------------------------------------------------ cursor themes
static std::wstring exeDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p); size_t i = s.find_last_of(L"\\/");
    return (i == std::wstring::npos) ? std::wstring(L".") : s.substr(0, i);
}
static bool fileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool dirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::vector<std::wstring> themeBases() {   // where themes live, bundled first
    std::vector<std::wstring> v;
    v.push_back(exeDir() + L"\\cursors");
    wchar_t ad[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH))
        v.push_back(std::wstring(ad) + L"\\smooly\\cursors");
    return v;
}
static bool hasCursorFiles(const std::wstring& dir) {
    return fileExists(dir + L"\\install.inf") || fileExists(dir + L"\\Pointer.cur");
}
static bool looksLikeTheme(const std::wstring& dir) {   // flat pack OR one with size subfolders
    if (hasCursorFiles(dir)) return true;
    for (auto sz : kSizeNames) if (hasCursorFiles(dir + L"\\" + sz)) return true;
    return false;
}
// The folder that actually holds the .cur/.ani files for a given size preference.
static std::wstring themeContentDir(const std::wstring& themeDir, int sizeIdx) {
    if (sizeIdx >= 0 && sizeIdx < 4) {
        std::wstring d = themeDir + L"\\" + kSizeNames[sizeIdx];
        if (hasCursorFiles(d)) return d;
    }
    for (auto sz : kSizeNames) {                    // fall back to any available size
        std::wstring d = themeDir + L"\\" + sz;
        if (hasCursorFiles(d)) return d;
    }
    return themeDir;                                // flat (BYO) pack
}
static std::wstring findThemeDir(const std::wstring& name) {
    for (auto& base : themeBases()) {
        std::wstring d = base + L"\\" + name;
        if (dirExists(d) && looksLikeTheme(d)) return d;
    }
    return L"";
}
static std::vector<std::wstring> scanThemes() {
    std::vector<std::wstring> out;
    for (auto& base : themeBases()) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L".." || !looksLikeTheme(base + L"\\" + n)) continue;
            bool dup = false;
            for (auto& e : out) if (e == n) { dup = true; break; }
            if (!dup) out.push_back(n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}
static std::wstring resolveFile(const std::wstring& dir, const ThemeRole& r) {
    std::wstring inf = dir + L"\\install.inf";
    if (fileExists(inf)) {
        wchar_t buf[128];
        GetPrivateProfileStringW(L"Strings", r.key, r.file, buf, 128, inf.c_str());
        std::wstring f = buf;
        while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(f.begin());
        while (!f.empty() && (f.back()  == L' ' || f.back()  == L'"')) f.pop_back();
        if (!f.empty()) return f;
    }
    return r.file;
}

static int sizeClamp() { return (g_cfg.size >= 0 && g_cfg.size < 4) ? g_cfg.size : 1; }

// How much to runtime-scale the base (registry) cursors for the chosen size.
// Native folders (Regular/Large/Extra-Large of a theme) need no scaling; "Small"
// has no bundled folder so it's scaled down from Regular, and every size of the
// bare Windows scheme is scaled from the OS defaults.
static double currentResidual() {
    int sz = sizeClamp();
    if (g_cfg.theme.empty() || g_cfg.theme == L"Windows Default") return kSizeScale[sz];
    std::wstring d = findThemeDir(g_cfg.theme);
    if (d.empty()) return 1.0;
    bool native = dirExists(d + L"\\" + kSizeNames[sz]);   // "Small" never has a native folder
    return native ? 1.0 : kSizeScale[sz];
}

static int cursorWidth(HCURSOR c) {
    ICONINFO ii = {}; if (!c || !GetIconInfo(c, &ii)) return 0;
    BITMAP bm = {}; int wd = 0;
    HBITMAP b = ii.hbmColor ? ii.hbmColor : ii.hbmMask;
    if (b && GetObject(b, sizeof(bm), &bm)) wd = bm.bmWidth;
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    return wd;
}

// Runtime-scale the current base cursors in place for the chosen size (skips the
// animated .ani roles). For a downscale we source from the theme's Extra-Large art
// when available — downscaling from the largest render keeps small cursors crisp.
static void ApplyResidualScale() {
    double r = currentResidual();
    if (r == 1.0) return;

    std::wstring hiDir;
    if (!(g_cfg.theme.empty() || g_cfg.theme == L"Windows Default")) {
        std::wstring d = findThemeDir(g_cfg.theme);
        if (!d.empty()) hiDir = themeContentDir(d, 3);   // Extra-Large (or largest available)
    }

    for (auto& role : kThemeRoles) {
        if (role.anim) continue;
        HCURSOR base = LoadCursorW(nullptr, MAKEINTRESOURCEW(role.ocr));   // current (target-size) cursor
        HCURSOR src = base; HCURSOR owned = nullptr; double s = r;

        if (r < 1.0 && !hiDir.empty()) {
            std::wstring path = hiDir + L"\\" + resolveFile(hiDir, role);
            HCURSOR hi = (HCURSOR)LoadImageW(nullptr, path.c_str(), IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE);
            int bw = cursorWidth(base), hw = cursorWidth(hi);
            if (hi && bw > 0 && hw > 0) { s = r * (double)bw / hw; src = hi; owned = hi; }
            else if (hi) DestroyCursor(hi);
        }

        HCURSOR scaled = MakeScaledCursor(src, s);
        if (scaled) SetSystemCursor(scaled, role.ocr);
        if (owned) DestroyCursor(owned);
    }
}
// Reload the active theme from the registry, then reapply the size scaling.
static void ReassertCursors() {
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE);
    ApplyResidualScale();
}

// Apply a theme by writing the standard cursor-scheme registry values, broadcasting
// SPI_SETCURSORS, then applying the size scaling. Persists across reboots and
// composes with shake-to-locate (which captures the displayed cursors as its base).
static bool ApplyCursorTheme(const std::wstring& name) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return false;
    auto setStr = [&](const wchar_t* val, const std::wstring& data) {
        RegSetValueExW(k, val, 0, REG_SZ, (const BYTE*)data.c_str(),
                       (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    };
    bool ok = true;
    if (name.empty() || name == L"Windows Default") {      // revert to system cursors
        setStr(nullptr, L"");
        for (auto& r : kThemeRoles) setStr(r.reg, L"");
    } else {
        std::wstring dir = findThemeDir(name);
        if (dir.empty()) { ok = false; }
        else {
            std::wstring content = themeContentDir(dir, g_cfg.size);
            setStr(nullptr, name);
            for (auto& r : kThemeRoles) {
                std::wstring path = content + L"\\" + resolveFile(content, r);
                setStr(r.reg, fileExists(path) ? path : std::wstring());
            }
        }
    }
    RegCloseKey(k);
    if (ok) {
        SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        ApplyResidualScale();   // apply the chosen size (e.g. Small) on top of the base scheme
    }
    return ok;
}

static BtnMap* findMap(int button) {         // g_maps is GUI-thread only (hook + UI)
    for (auto& m : g_maps) if (m.button == button) return &m;
    return nullptr;
}
static int scrollActionFor(int button) { BtnMap* m = findMap(button); return m ? m->scroll : 0; }
static int nativeClick(int button) { return button == 3 ? 1 : button == 4 ? 4 : 5; }  // middle/back/forward
static void fireAct(int action, int combo, const std::wstring& text);

// ------------------------------------------------------------------ engine
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    static bool suppressUp = false;   // swallow the release of a click we turned into a gesture
    if (nCode != HC_ACTION) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (ms->dwExtraInfo == kInjectTag)             // our own synthetic input: pass through
        return CallNextHookEx(g_hook, nCode, wParam, lParam);

    switch (wParam) {
    case WM_MOUSEMOVE: {
        if (ms->flags & LLMHF_INJECTED) break;
        if (g_pBtn.load()) {                       // a managed button is held
            int da = g_pDragAction.load();
            if (da != 0) {
                int dx = ms->pt.x - g_dragLastX, dy = ms->pt.y - g_dragLastY;
                g_dragLastX = ms->pt.x; g_dragLastY = ms->pt.y;
                g_dragTotal += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (g_dragTotal >= kDragPx || g_pDragged.load()) {
                    bool first = !g_pDragged.exchange(true);
                    if (da == 1) { g_panV.fetch_add(dy * kDragScroll); g_panH.fetch_add(dx * kDragScroll); }
                    else if (da == 2) {
                        g_dragAccX += dx;
                        if (g_dragAccX >= kDeskPx) { g_dragCmd.store(2); g_dragAccX = 0; }
                        else if (g_dragAccX <= -kDeskPx) { g_dragCmd.store(1); g_dragAccX = 0; }
                    } else if (da == 3 && first) g_dragCmd.store(3);
                    return 1;                      // consume the move (freeze the cursor)
                }
            }
            break;                                 // held: pass move, don't shake
        }
        DetectShake(ms);
        break;
    }

    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: {
        int btn = (wParam == WM_MBUTTONDOWN) ? 3 : (HIWORD(ms->mouseData) == XBUTTON1 ? 4 : 5);
        BtnMap* m = findMap(btn);
        if (!m) break;                         // unmanaged -> native (lets the capture box register it)
        if (!m->click && !m->dbl && !m->hold && !m->scroll && !m->drag) break;   // all default -> native
        g_pBtn.store(btn); g_pDownAt.store(nowTicks());
        g_pHoldFired.store(false); g_pScrolled.store(false); g_pDragged.store(false);
        g_pHoldAction.store(m->hold); g_pHoldCombo.store(m->keyH); g_pDragAction.store(m->drag);
        { std::lock_guard<std::mutex> lock(g_mutex); g_pHoldText = m->txtH; }
        g_dragLastX = ms->pt.x; g_dragLastY = ms->pt.y; g_dragTotal = 0; g_dragAccX = 0;
        return 1;                              // swallow; decide click / hold / scroll / drag
    }
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: {
        int btn = (wParam == WM_MBUTTONUP) ? 3 : (HIWORD(ms->mouseData) == XBUTTON1 ? 4 : 5);
        if (g_pBtn.load() != btn) break;
        g_pBtn.store(0);
        if (g_pScrolled.load() || g_pHoldFired.load() || g_pDragged.load()) return 1;  // consumed
        BtnMap* m = findMap(btn);
        int clickAct = (m && m->click != 0) ? m->click : nativeClick(btn);
        int clickCombo = m ? m->keyC : 0; std::wstring clickText = m ? m->txtC : L"";
        int dblAct = m ? m->dbl : 0, dblCombo = m ? m->keyD : 0; std::wstring dblText = m ? m->txtD : L"";
        LONGLONG now = nowTicks();
        if (dblAct != 0 && btn == g_lastClickBtn && ticksToMs(now - g_lastClickAt) <= kDblMs) {
            g_lastClickBtn = 0; g_pendAction.store(0);             // second click -> double
            fireAct(dblAct, dblCombo, dblText);
        } else {
            g_lastClickBtn = btn; g_lastClickAt = now;
            if (dblAct != 0) {
                g_pendAction.store(clickAct); g_pendCombo.store(clickCombo);
                { std::lock_guard<std::mutex> lock(g_mutex); g_pendText = clickText; }
                g_pendDeadline.store(now + (LONGLONG)kDblMs * g_qpcFreq / 1000);
            } else fireAct(clickAct, clickCombo, clickText);
        }
        return 1;
    }

    case WM_LBUTTONDOWN:
        if (g_cfg.gestures) {
            bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
            bool alt   = GetAsyncKeyState(VK_MENU)  & 0x8000;
            // Shift+click is left native (text range-selection); only Alt gestures.
            int g = (alt && shift) ? 3 : alt ? 2 : 0;
            if (g) { g_gesture.store(g); suppressUp = true; return 1; }
        }
        break;

    case WM_LBUTTONUP:
        if (suppressUp) { suppressUp = false; return 1; }
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        bool  horizWheel = (wParam == WM_MOUSEHWHEEL);
        short delta      = (short)HIWORD(ms->mouseData);
        bool  ctrl       = GetAsyncKeyState(VK_CONTROL) & 0x8000;
        bool  shift      = GetAsyncKeyState(VK_SHIFT)   & 0x8000;

        int held = g_pBtn.load();                              // Click + Scroll gesture
        if (!horizWheel && held) {
            int sa = scrollActionFor(held);
            if (sa != 0) {
                g_pScrolled.store(true);
                if (sa == 1) { std::lock_guard<std::mutex> lock(g_mutex); g_remH += (delta > 0 ? -1.0 : 1.0) * kStepBySpeed[g_cfg.speed]; }
                else if (sa == 2) g_zoomDelta.fetch_add(delta);
                else if (sa == 3) { std::lock_guard<std::mutex> lock(g_mutex); int dir = delta > 0 ? 1 : -1; if (g_cfg.reverse) dir = -dir; g_remV += dir * kStepBySpeed[g_cfg.speed] * kSwiftMul; }
                return 1;
            }
        }

        { HWND wu = WindowFromPoint(ms->pt); if (wu && GetAncestor(wu, GA_ROOT) == g_wnd) break; }   // don't smooth our own UI

        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_cfg.enabled || g_cfg.smoothness == 0) break;
        if (horizWheel && !g_cfg.horizontal) break;

        bool toHoriz = horizWheel || (shift && g_cfg.shiftHoriz && g_cfg.horizontal);
        double step  = kStepBySpeed[g_cfg.speed];

        LONGLONG  now  = nowTicks();
        LONGLONG& last = toHoriz ? g_lastH : g_lastV;
        double&   burst= toHoriz ? g_burstH : g_burstV;
        double&   rem  = toHoriz ? g_remH : g_remV;
        double interval = last ? ticksToMs(now - last) : 1e9;
        last = now;

        double amount = kAccelByLevel[(g_cfg.accel >= 0 && g_cfg.accel < 4) ? g_cfg.accel : 0];
        double boost = 1.0;
        if (amount > 0.0) {
            double dtSec = interval / 1000.0; if (dtSec > 5.0) dtSec = 5.0;
            burst = burst * std::exp(-kAccelDecay * dtSec) + 1.0;   // rises with sustained scrolling
            double ramp = (burst - 1.0) / (kAccelRamp - 1.0);
            if (ramp < 0) ramp = 0; else if (ramp > 1) ramp = 1;
            boost = 1.0 + amount * ramp;
        }

        int dir;
        if (horizWheel)   dir = (delta > 0) ? 1 : -1;
        else if (toHoriz) dir = (delta > 0) ? -1 : 1;   // Shift+wheel: up = left, down = right
        else            { dir = (delta > 0) ? 1 : -1; if (g_cfg.reverse) dir = -dir; }

        double dist = dir * step * boost;
        double cap  = step * kMaxNotchStack;
        if (ctrl && g_cfg.ctrlTurbo) { dist *= kTurboFactor; cap *= kTurboFactor; }

        if ((dist > 0 && rem < 0) || (dist < 0 && rem > 0)) rem = 0;   // crisp reversal
        rem += dist;
        if (rem >  cap) rem =  cap;
        if (rem < -cap) rem = -cap;
        return 1;   // swallow the raw notch
    }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static void EmitWheel(bool horiz, int amount) {
    INPUT in = {};
    in.type           = INPUT_MOUSE;
    in.mi.dwFlags     = horiz ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    in.mi.mouseData   = (DWORD)amount;
    in.mi.dwExtraInfo = kInjectTag;
    SendInput(1, &in, sizeof(INPUT));
}

static void SendKey(WORD vk, bool up) {
    INPUT in = {};
    in.type    = INPUT_KEYBOARD;
    in.ki.wVk  = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}
static void MouseClick(DWORD flag) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags     = flag;
    in.mi.dwExtraInfo = kInjectTag;
    SendInput(1, &in, sizeof(INPUT));
}

// Turn a modifier+click into a selection: word (double-click), line (triple),
// or word+copy. The held modifiers are released after the first button-down so
// the synthetic clicks land as plain clicks (and Alt can't open the window menu).
static void PerformGesture(int type) {
    bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
    bool alt   = GetAsyncKeyState(VK_MENU)  & 0x8000;
    int clicks = (type == 2) ? 3 : 2;

    MouseClick(MOUSEEVENTF_LEFTDOWN);
    if (shift) SendKey(VK_SHIFT, true);
    if (alt)   SendKey(VK_MENU, true);
    MouseClick(MOUSEEVENTF_LEFTUP);
    for (int i = 1; i < clicks; i++) { MouseClick(MOUSEEVENTF_LEFTDOWN); MouseClick(MOUSEEVENTF_LEFTUP); }

    if (type == 3) {   // select word, then copy
        SendKey(VK_CONTROL, false); SendKey('C', false);
        SendKey('C', true);         SendKey(VK_CONTROL, true);
    }
}

static void MouseX(DWORD flag, int xb) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag; in.mi.mouseData = (DWORD)xb; in.mi.dwExtraInfo = kInjectTag;
    SendInput(1, &in, sizeof(INPUT));
}
static void PerformButtonAction(int a) {   // see kButtonActions (1..6; 0/7 handled elsewhere)
    switch (a) {
    case 1: MouseClick(MOUSEEVENTF_MIDDLEDOWN); MouseClick(MOUSEEVENTF_MIDDLEUP); break;
    case 2: SendKey(VK_CONTROL, false); SendKey('C', false); SendKey('C', true); SendKey(VK_CONTROL, true); break;
    case 3: SendKey(VK_CONTROL, false); SendKey('V', false); SendKey('V', true); SendKey(VK_CONTROL, true); break;
    case 4: MouseX(MOUSEEVENTF_XDOWN, XBUTTON1); MouseX(MOUSEEVENTF_XUP, XBUTTON1); break;
    case 5: MouseX(MOUSEEVENTF_XDOWN, XBUTTON2); MouseX(MOUSEEVENTF_XUP, XBUTTON2); break;
    }
}
static void PerformKeyCombo(int c) {       // packed (mods<<8)|vk
    if (!c) return;
    int vk = c & 0xFF, mods = c >> 8;
    if (mods & 1) SendKey(VK_CONTROL, false);
    if (mods & 2) SendKey(VK_SHIFT, false);
    if (mods & 4) SendKey(VK_MENU, false);
    if (mods & 8) SendKey(VK_LWIN, false);
    SendKey((WORD)vk, false); SendKey((WORD)vk, true);
    if (mods & 8) SendKey(VK_LWIN, true);
    if (mods & 4) SendKey(VK_MENU, true);
    if (mods & 2) SendKey(VK_SHIFT, true);
    if (mods & 1) SendKey(VK_CONTROL, true);
}
static void TypeText(const std::wstring& s) {   // type a string via Unicode key events
    for (wchar_t c : s) {
        INPUT in[2] = {};
        in[0].type = INPUT_KEYBOARD; in[0].ki.wScan = c; in[0].ki.dwFlags = KEYEVENTF_UNICODE; in[0].ki.dwExtraInfo = kInjectTag;
        in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
    }
}
// Run a resolved button action (on the animation thread).
static void performAct(int action, int combo, const std::wstring& text) {
    if (action == 7) PerformKeyCombo(combo);
    else if (action == 8) TypeText(text);
    else if (action >= 1) PerformButtonAction(action);
}
// Queue an action from the hook for the animation thread.
static void fireAct(int action, int combo, const std::wstring& text) {
    { std::lock_guard<std::mutex> lock(g_mutex); g_act.action = action; g_act.combo = combo; g_act.text = text; }
    g_actReady.store(true);
}

static void AnimationLoop() {
    timeBeginPeriod(1);
    LONGLONG prev = nowTicks();
    const double frameMs = 1000.0 / kFrameHz;

    while (g_running.load()) {
        LONGLONG cur = nowTicks();
        double dt = ticksToMs(cur - prev) / 1000.0;
        prev = cur;
        if (dt > 0.1) dt = 0.1;   // clamp scheduler hiccups

        if (int g = g_gesture.exchange(0)) PerformGesture(g);        // click selection gesture
        if (g_actReady.exchange(false)) {                            // queued button action
            int a, c; std::wstring t;
            { std::lock_guard<std::mutex> lock(g_mutex); a = g_act.action; c = g_act.combo; t = g_act.text; }
            performAct(a, c, t);
        }
        if (int zd = g_zoomDelta.exchange(0)) {                       // Click + Scroll = zoom
            SendKey(VK_CONTROL, false); EmitWheel(false, zd); SendKey(VK_CONTROL, true);
        }
        { int pv = g_panV.exchange(0), ph = g_panH.exchange(0);       // Click + Drag = pan
          if (pv) EmitWheel(false, pv); if (ph) EmitWheel(true, -ph); }
        if (int dc = g_dragCmd.exchange(0)) {                         // Click + Drag = navigate
            if (dc == 1) PerformKeyCombo(((1 | 8) << 8) | VK_LEFT);
            else if (dc == 2) PerformKeyCombo(((1 | 8) << 8) | VK_RIGHT);
            else if (dc == 3) PerformKeyCombo((8 << 8) | VK_TAB);
        }
        if (g_pBtn.load() && !g_pHoldFired.load() && !g_pScrolled.load() && !g_pDragged.load()) {   // hold
            if (g_pHoldAction.load() && ticksToMs(cur - g_pDownAt.load()) >= kHoldMs) {
                g_pHoldFired.store(true);
                std::wstring t; { std::lock_guard<std::mutex> lock(g_mutex); t = g_pHoldText; }
                performAct(g_pHoldAction.load(), g_pHoldCombo.load(), t);
            }
        }
        if (g_pendAction.load() && cur >= g_pendDeadline.load()) {    // deferred single-click
            int pa = g_pendAction.exchange(0), pc = g_pendCombo.exchange(0);
            std::wstring t; { std::lock_guard<std::mutex> lock(g_mutex); t = g_pendText; g_pendText.clear(); }
            performAct(pa, pc, t);
        }

        // shake-to-locate: smoothly zoom the cursor toward big / back to normal
        {
            bool wantBig  = g_cfg.shake && (g_bigUntil.load() > cur);
            double target = wantBig ? kShakeFactor : 1.0;
            double rate   = (target > g_shakeScale) ? kShakeGrowRate : kShakeShrinkRate;
            g_shakeScale += (target - g_shakeScale) * (1.0 - std::exp(-rate * dt));
            if (wantBig  && g_shakeScale > kShakeFactor - 0.03) g_shakeScale = kShakeFactor;
            if (!wantBig && g_shakeScale < 1.03)                g_shakeScale = 1.0;

            if (g_shakeScale <= 1.0) {
                if (g_cursorBig) { FreeShakeBase(); ReassertCursors(); g_cursorBig = false; g_othersBig = false; g_lastShakePx = 0; }
            } else {
                if (!g_cursorBig) { CaptureShakeBase(); g_cursorBig = true; }   // stable base, captured once
                int px = (int)(32 * g_shakeScale);   // reapply the arrow only when the size actually changes
                if (px != g_lastShakePx && g_shakeBase[0]) {
                    g_lastShakePx = px;
                    HCURSOR c = MakeScaledCursor(g_shakeBase[0], g_shakeScale);
                    if (c) SetSystemCursor(c, kThemeRoles[0].ocr);
                }
                if (!g_othersBig && g_shakeScale > kShakeFactor * 0.5) {   // other roles scaled once
                    for (int i = 1; i < kNumRoles; i++) {
                        if (kThemeRoles[i].anim || !g_shakeBase[i]) continue;
                        HCURSOR c = MakeScaledCursor(g_shakeBase[i], kShakeFactor);
                        if (c) SetSystemCursor(c, kThemeRoles[i].ocr);
                    }
                    g_othersBig = true;
                }
            }
        }

        double emitV = 0.0, emitH = 0.0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            double k = kRateBySmoothness[g_cfg.smoothness > 0 ? g_cfg.smoothness : 1];
            double eased = 1.0 - std::exp(-k * dt);   // fraction of the owed distance this frame
            double floorStep = kMinScrollSpeed * dt;  // but never slower than this

            auto pay = [&](double& rem) -> double {
                if (rem == 0.0) return 0.0;
                double move = rem * eased;
                double fl   = (rem > 0 ? floorStep : -floorStep);
                if (std::fabs(fl) > std::fabs(rem)) fl = rem;       // don't overshoot
                if (std::fabs(move) < std::fabs(fl)) move = fl;     // enforce min finishing speed
                rem -= move;
                if (std::fabs(rem) < 0.5) { move += rem; rem = 0.0; }
                return move;
            };
            emitV = pay(g_remV);
            emitH = pay(g_remH);
        }
        if (emitV != 0.0) { g_accV += emitV; int w = (int)g_accV; g_accV -= w; if (w) EmitWheel(false, w); }
        if (emitH != 0.0) { g_accH += emitH; int w = (int)g_accH; g_accH -= w; if (w) EmitWheel(true,  w); }

        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(frameMs));
    }
    if (g_cursorBig) { FreeShakeBase(); RestoreCursors(); g_cursorBig = false; }
    timeEndPeriod(1);
}

// ------------------------------------------------------------------ tray icon
static Gdiplus::Color gpc(COLORREF c);
static void drawSvgG(Gdiplus::Graphics& g, int x, int y, int size, COLORREF col, const SvgIcon& ic, double strokeW, bool solid);

// App/tray icon: accent rounded square + the white HugeIcons mouse on top.
static HICON CreateBrandIcon() {
    const int sz = 32;
    Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        Gdiplus::GraphicsPath p; float d = 13;
        p.AddArc(0.f, 0.f, d, d, 180, 90); p.AddArc(sz - d, 0.f, d, d, 270, 90);
        p.AddArc((float)sz - d, (float)sz - d, d, d, 0, 90); p.AddArc(0.f, (float)sz - d, d, d, 90, 90); p.CloseFigure();
        Gdiplus::SolidBrush b(gpc(RGB(0x2f, 0x8f, 0xff))); g.FillPath(&b, &p);
        drawSvgG(g, 6, 6, 20, RGB(255, 255, 255), kMouseIcon, 1.9, true);
    }
    Gdiplus::BitmapData bd; Gdiplus::Rect rc(0, 0, sz, sz);
    HICON icon = nullptr;
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = sz; bi.bmiHeader.biHeight = -sz;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr; HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (color && bits) {
            for (int y = 0; y < sz; y++) memcpy((BYTE*)bits + (size_t)y * sz * 4, (BYTE*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)sz * 4);
            HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
            ICONINFO ii = { TRUE, 0, 0, mask, color };
            icon = CreateIconIndirect(&ii);
            DeleteObject(mask);
        }
        if (color) DeleteObject(color);
        DeleteDC(dc);
        bmp.UnlockBits(&bd);
    }
    return icon;
}

// ------------------------------------------------------------------ GUI
static const int SB_W = 200, ROW_H = 46, CARD_GAP = 16, WIN_W = 720, WIN_H = 560;

static int* toggleCfg(int id) {
    switch (id) {
    case ID_ENABLE:  return &g_cfg.enabled;
    case ID_REVERSE: return &g_cfg.reverse;
    case ID_HORIZ:   return &g_cfg.horizontal;
    case ID_SHIFTH:  return &g_cfg.shiftHoriz;
    case ID_CTRLT:   return &g_cfg.ctrlTurbo;
    case ID_GESTURE: return &g_cfg.gestures;
    case ID_NOACCEL: return &g_cfg.noAccel;
    case ID_SHAKE:   return &g_cfg.shake;
    case ID_STARTUP: return &g_cfg.startup;
    }
    return nullptr;
}
struct DdInfo { int* val; const wchar_t* const* opts; int n; bool theme; };
static bool ddInfo(int id, DdInfo& d) {
    static const wchar_t* sm[] = { L"Off", L"Low", L"Medium", L"High", L"Very High", L"Buttery" };
    static const wchar_t* sp[] = { L"Low", L"Medium", L"High" };
    static const wchar_t* ac[] = { L"Off", L"Low", L"Medium", L"High" };
    d = {};
    if (id >= ID_DYN_BASE && id < ID_DYN_REMOVE) {
        int mi = (id - ID_DYN_BASE) / 8, tr = (id - ID_DYN_BASE) % 8;
        if (mi < 0 || mi >= (int)g_maps.size()) return false;
        BtnMap& m = g_maps[mi];
        switch (tr) {
        case 0: d.val = &m.click;  d.opts = kButtonActions; d.n = 9; return true;
        case 1: d.val = &m.dbl;    d.opts = kButtonActions; d.n = 9; return true;
        case 2: d.val = &m.hold;   d.opts = kButtonActions; d.n = 9; return true;
        case 3: d.val = &m.scroll; d.opts = kScrollActions; d.n = 4; return true;
        case 4: d.val = &m.drag;   d.opts = kDragActions;   d.n = 4; return true;
        }
        return false;
    }
    switch (id) {
    case ID_SMOOTH: d.val = &g_cfg.smoothness; d.opts = sm; d.n = 6; return true;
    case ID_SPEED:  d.val = &g_cfg.speed;      d.opts = sp; d.n = 3; return true;
    case ID_ACCEL:  d.val = &g_cfg.accel;      d.opts = ac; d.n = 4; return true;
    case ID_SIZE:   d.val = &g_cfg.size;       d.opts = kSizeNames;     d.n = 4; return true;
    case ID_THEME:  d.theme = true; return true;
    }
    return false;
}
static std::wstring comboText(int c) {
    if (!c) return L"Keyboard shortcut";
    int vk = c & 0xFF, mods = c >> 8;
    std::wstring s;
    if (mods & 1) s += L"Ctrl+"; if (mods & 4) s += L"Alt+";
    if (mods & 2) s += L"Shift+"; if (mods & 8) s += L"Win+";
    wchar_t ch = (wchar_t)MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
    const wchar_t* nm = nullptr;
    switch (vk) {
    case VK_LEFT: nm = L"Left"; break; case VK_RIGHT: nm = L"Right"; break;
    case VK_UP: nm = L"Up"; break; case VK_DOWN: nm = L"Down"; break;
    case VK_SPACE: nm = L"Space"; break; case VK_RETURN: nm = L"Enter"; break;
    case VK_TAB: nm = L"Tab"; break; case VK_ESCAPE: nm = L"Esc"; break;
    case VK_BACK: nm = L"Backspace"; break; case VK_DELETE: nm = L"Del"; break;
    case VK_HOME: nm = L"Home"; break; case VK_END: nm = L"End"; break;
    case VK_PRIOR: nm = L"PgUp"; break; case VK_NEXT: nm = L"PgDn"; break;
    }
    if (nm) s += nm;
    else if (vk >= VK_F1 && vk <= VK_F24) { wchar_t b[8]; wsprintfW(b, L"F%d", vk - VK_F1 + 1); s += b; }
    else if (ch > 32) s += ch;
    else { wchar_t b[8]; wsprintfW(b, L"0x%02X", vk); s += b; }
    return s;
}
static std::wstring ddText(int id) {
    DdInfo d;
    if (!ddInfo(id, d)) return L"";
    if (d.theme) return g_cfg.theme.empty() ? L"Windows Default" : g_cfg.theme;
    int v = *d.val; if (v < 0 || v >= d.n) v = 0;
    if ((v == 7 || v == 8) && id >= ID_DYN_BASE && id < ID_DYN_REMOVE) {
        int mi = (id - ID_DYN_BASE) / 8, tr = (id - ID_DYN_BASE) % 8;
        if (mi >= 0 && mi < (int)g_maps.size()) {
            const BtnMap& m = g_maps[mi];
            if (v == 7) return comboText(tr == 0 ? m.keyC : tr == 1 ? m.keyD : m.keyH);   // captured combo
            std::wstring t = (tr == 0 ? m.txtC : tr == 1 ? m.txtD : m.txtH);              // typed text
            if (t.empty()) return L"Type text";
            if (t.size() > 16) t = t.substr(0, 15) + L"…";
            return L"“" + t + L"”";
        }
    }
    return d.opts[v];
}

LRESULT CALLBACK KeyCapProc(int nc, WPARAM wp, LPARAM lp) {
    if (nc == HC_ACTION && (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN)) {
        int vk = ((KBDLLHOOKSTRUCT*)lp)->vkCode;
        switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN: return CallNextHookEx(g_keyCapHook, nc, wp, lp);
        case VK_ESCAPE: g_capturedKey.store(0); return 1;
        }
        int mods = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= 1;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= 2;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= 4;
        if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= 8;
        g_capturedKey.store((mods << 8) | (vk & 0xFF));
        return 1;
    }
    return CallNextHookEx(g_keyCapHook, nc, wp, lp);
}
static int CaptureKeyCombo() {
    g_capturedKey.store(-1);
    g_keyCapHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyCapProc, GetModuleHandleW(nullptr), 0);
    if (!g_keyCapHook) return 0;
    wchar_t old[64]; GetWindowTextW(g_wnd, old, 64);
    SetWindowTextW(g_wnd, L"Press a shortcut…  (Esc to cancel)");
    ULONGLONG start = GetTickCount64();
    while (g_capturedKey.load() == -1 && GetTickCount64() - start < 6000) {
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(8);
    }
    UnhookWindowsHookEx(g_keyCapHook); g_keyCapHook = nullptr;
    SetWindowTextW(g_wnd, old);
    int c = g_capturedKey.load();
    return c < 0 ? 0 : c;
}

// Modal dark text-input dialog (for the "Type text" action).
static HWND g_inputEdit = nullptr;
static int  g_inputState = 0;
LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: {
        static HBRUSH br = CreateSolidBrush(C_CARD);
        HDC dc = (HDC)w; SetTextColor(dc, C_TEXT); SetBkColor(dc, C_CARD); SetBkMode(dc, TRANSPARENT);
        return (LRESULT)br;
    }
    case WM_CLOSE: g_inputState = -1; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static std::wstring InputText(const wchar_t* title, const std::wstring& init) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    int W = P(380), H = P(150);
    int x = (GetSystemMetrics(SM_CXSCREEN) - W) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;
    HWND win = CreateWindowExW(WS_EX_TOPMOST, L"SmoolyInput", title,
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H, g_wnd, nullptr, hi, nullptr);
    if (!win) return L"";
    BOOL dark = TRUE; DwmSetWindowAttribute(win, 20, &dark, sizeof(dark));
    HWND lbl = CreateWindowW(L"STATIC", L"Text to type when the button fires:", WS_CHILD | WS_VISIBLE,
                             P(16), P(12), W - P(32), P(20), win, nullptr, hi, nullptr);
    g_inputEdit = CreateWindowW(L"EDIT", init.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                P(16), P(40), W - P(32), P(30), win, (HMENU)1, hi, nullptr);
    HWND hint = CreateWindowW(L"STATIC", L"Enter to save  ·  Esc to cancel", WS_CHILD | WS_VISIBLE,
                              P(16), P(80), W - P(32), P(18), win, nullptr, hi, nullptr);
    SendMessageW(lbl, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_inputEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(hint, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    ShowWindow(win, SW_SHOW); SetForegroundWindow(win);
    SetFocus(g_inputEdit); SendMessageW(g_inputEdit, EM_SETSEL, 0, -1);
    g_inputState = 0;
    MSG m;
    while (g_inputState == 0 && GetMessageW(&m, nullptr, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) { g_inputState = 1; continue; }
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { g_inputState = -1; continue; }
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    std::wstring result;
    if (g_inputState == 1) { wchar_t buf[512]; GetWindowTextW(g_inputEdit, buf, 512); result = buf; }
    DestroyWindow(win); g_inputEdit = nullptr;
    return result;
}

// No scrolling anywhere: every page fits the fixed window (the Buttons page shows
// one registered button at a time via a selector), so we only ever show/hide the
// current page's controls — nothing moves, so there is nothing to flicker.
static void relayout(HWND h) {
    for (auto& r : g_rows) ShowWindow(r.ctrl, r.page == g_page ? SW_SHOW : SW_HIDE);
    if (h) InvalidateRect(h, nullptr, TRUE);
}
static void ShowPage(int p) {
    g_page = p;
    relayout(g_wnd);
}

static Gdiplus::Color gpc(COLORREF c);
static void fillRound(HDC dc, RECT rc, int rad, COLORREF col);
static void fillEllipse(HDC dc, int x, int y, int w, int h, COLORREF col);

// Minimal dark slider (pointer speed 1..20). Pos lives in GWLP_USERDATA; drag
// notifies the parent via WM_HSCROLL(SB_THUMBTRACK, pos).
LRESULT CALLBACK SliderProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const int lo = 1, hi = 20, pad = 8;
    auto setFromX = [&](int x) {
        RECT rc; GetClientRect(h, &rc);
        int span = rc.right - 2 * pad; if (span < 1) span = 1;
        int pos = lo + (int)((double)(x - pad) / span * (hi - lo) + 0.5);
        if (pos < lo) pos = lo; if (pos > hi) pos = hi;
        SetWindowLongPtrW(h, GWLP_USERDATA, pos);
        InvalidateRect(h, nullptr, TRUE);
        SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(SB_THUMBTRACK, pos), (LPARAM)h);
    };
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: SetCapture(h); setFromX((short)LOWORD(l)); return 0;
    case WM_MOUSEMOVE:   if (w & MK_LBUTTON) setFromX((short)LOWORD(l)); return 0;
    case WM_LBUTTONUP:   ReleaseCapture(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(C_CARD); FillRect(dc, &rc, bg); DeleteObject(bg);
        int cy = rc.bottom / 2, span = rc.right - 2 * pad, th = P(4);
        int pos = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
        int px = pad + (int)((double)(pos - lo) / (hi - lo) * span);
        RECT track = { pad, cy - th / 2, rc.right - pad, cy + th / 2 };  fillRound(dc, track, th / 2, C_LINE);
        RECT fill  = { pad, cy - th / 2, px, cy + th / 2 };              fillRound(dc, fill,  th / 2, C_ACCENT);
        int r = P(9); fillEllipse(dc, px - r, cy - r, 2 * r, 2 * r, RGB(255, 255, 255));
        EndPaint(h, &ps); return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// Capture box: click a mouse button inside it to register that button.
LRESULT CALLBACK CaptureProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_MBUTTONDOWN: SendMessageW(GetParent(h), WM_APP + 2, 3, 0); return 0;
    case WM_XBUTTONDOWN: SendMessageW(GetParent(h), WM_APP + 2, (HIWORD(w) == XBUTTON1 ? 4 : 5), 0); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
        Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(gpc(C_LINE), (Gdiplus::REAL)P(2)); pen.SetDashStyle(Gdiplus::DashStyleDash);
        float x = (float)P(2), y = (float)P(2), ww = (float)(rc.right - P(4)), hh = (float)(rc.bottom - P(4)), d = (float)P(24);
        Gdiplus::GraphicsPath pth;
        pth.AddArc(x, y, d, d, 180, 90); pth.AddArc(x + ww - d, y, d, d, 270, 90);
        pth.AddArc(x + ww - d, y + hh - d, d, d, 0, 90); pth.AddArc(x, y + hh - d, d, d, 90, 90); pth.CloseFigure();
        g.DrawPath(&pen, &pth);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, C_TEXT2); SelectObject(dc, g_font);
        DrawTextW(dc, L"Click a mouse button here (middle / back / forward) to add it",
                  -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        EndPaint(h, &ps); return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static const wchar_t* cardHeader(int page, int card) {
    return nullptr;   // Buttons page identifies the selection via the pills; no card header needed
}

// Owner-draw BUTTONs erase to the default (light) background for one frame before
// WM_DRAWITEM repaints -> the white flash on show/create. WM_DRAWITEM fills the whole
// rect itself, so we subclass to swallow the erase entirely.
static WNDPROC g_btnProc = nullptr;
static LRESULT CALLBACK OwnerBtnProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    return CallWindowProcW(g_btnProc, h, m, w, l);
}
static HWND makeOwnerBtn(HWND parent, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowW(L"BUTTON", L"", WS_CHILD | BS_OWNERDRAW, x, y, w, h,
                           parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(c, L"", L"");
    if (!g_btnProc) g_btnProc = (WNDPROC)GetWindowLongPtrW(c, GWLP_WNDPROC);
    SetWindowLongPtrW(c, GWLP_WNDPROC, (LONG_PTR)OwnerBtnProc);
    return c;
}

// Create one control for a row + record its UiRow. Geometry is derived from `kind`
// so both the static pages and the dynamic Buttons page stay pixel-consistent.
//   kind: 0 toggle · 1 dropdown · 2 slider · 5 remove-button
static void addRow(HWND hwnd, int page, int card, int kind, int id, const wchar_t* label, int rowTop) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    const int right = P(WIN_W - 28), rowH = P(ROW_H);
    int cw = kind == 0 ? P(44) : kind == 1 ? P(194) : kind == 5 ? P(120) : P(250);
    int ch = kind == 0 ? P(26) : kind == 2 ? P(24) : P(28);
    int cx = right - (kind == 0 ? P(46) : cw);
    int cy = rowTop + (rowH - ch) / 2;
    HWND c;
    if (kind == 2) {
        c = CreateWindowW(L"SmoolySlider", L"", WS_CHILD, cx, cy, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SetWindowLongPtrW(c, GWLP_USERDATA, g_cfg.pointerSpeed > 0 ? g_cfg.pointerSpeed : g_sysSpeed);
    } else {
        c = makeOwnerBtn(hwnd, id, cx, cy, cw, ch);
    }
    g_rows.push_back({ page, card, kind, rowTop, rowH, cx, cy, ch, c, label });
}

static std::wstring btnPillName(int b) {
    return b == 3 ? L"Middle" : b == 4 ? L"Back" : b == 5 ? L"Forward" : L"Button";
}
// One button at a time: capture box, a selector pill per registered button, and the
// selected button's card. Nothing overflows the fixed window -> no scrolling, no flicker.
static void BuildButtonsPage(HWND hwnd) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    const int left = P(SB_W + 28), right = P(WIN_W - 28), capH = P(58);
    int y = P(60);
    HWND cap = CreateWindowW(L"SmoolyCapture", L"", WS_CHILD, left, y, right - left, capH,
                             hwnd, (HMENU)(INT_PTR)ID_CAPTURE, hi, nullptr);
    g_rows.push_back({ 2, -1, 4, y, capH, left, y, capH, cap, L"" });
    y += capH + P(20);

    int n = (int)g_maps.size();
    if (n == 0) return;                                  // nothing registered yet
    if (g_selBtn < 0) g_selBtn = 0;
    if (g_selBtn >= n) g_selBtn = n - 1;

    int px = left, ph = P(34);                           // selector pills (tabs)
    for (int i = 0; i < n; i++) {
        int pw = P(100);
        HWND c = makeOwnerBtn(hwnd, ID_SEL_BASE + i, px, y, pw, ph);
        g_rows.push_back({ 2, -2, 6, y, ph, px, y, ph, c, btnPillName(g_maps[i].button) });
        px += pw + P(10);
    }
    y += ph + P(22);

    static const wchar_t* trg[5] = { L"Click", L"Double-Click", L"Hold", L"Click + Scroll", L"Click + Drag" };
    int mi = g_selBtn;                                   // only the selected button's card
    for (int tr = 0; tr < 5; tr++) { addRow(hwnd, 2, mi, 1, ID_DYN_BASE + mi * 8 + tr, trg[tr], y); y += P(ROW_H); }
    addRow(hwnd, 2, mi, 5, ID_DYN_REMOVE + mi, L"Remove this button", y);
}
static void RebuildButtons(HWND hwnd) {
    for (auto it = g_rows.begin(); it != g_rows.end();) {
        if (it->page == 2) { DestroyWindow(it->ctrl); it = g_rows.erase(it); }
        else ++it;
    }
    BuildButtonsPage(hwnd);
    ShowPage(g_page);
}

static void BuildControls(HWND hwnd) {
    struct Def { int page, card, kind, id; const wchar_t* label; };
    static const Def defs[] = {
        { 0, 0, 0, ID_ENABLE,  L"Enable smooth scrolling" },
        { 0, 0, 1, ID_SMOOTH,  L"Smoothness" },
        { 0, 0, 1, ID_SPEED,   L"Speed" },
        { 0, 0, 1, ID_ACCEL,   L"Acceleration" },
        { 0, 1, 0, ID_REVERSE, L"Reverse scrolling" },
        { 0, 1, 0, ID_HORIZ,   L"Smooth horizontal (tilt) wheel" },
        { 0, 1, 0, ID_SHIFTH,  L"Shift + wheel scrolls horizontally" },
        { 1, 0, 2, ID_PSPEED,  L"Pointer speed" },
        { 1, 0, 0, ID_NOACCEL, L"Disable pointer acceleration" },
        { 1, 1, 0, ID_SHAKE,   L"Shake to locate (grow cursor)" },
        { 1, 2, 1, ID_THEME,   L"Cursor theme" },
        { 1, 2, 1, ID_SIZE,    L"Cursor size" },
        { 3, 0, 0, ID_CTRLT,   L"Ctrl + wheel = turbo scroll" },
        { 3, 1, 0, ID_GESTURE, L"Click gestures (Alt = line, Alt+Shift = copy)" },
        { 4, 0, 0, ID_STARTUP, L"Launch smooly when Windows starts" },
    };
    int py[kNumPages], lastCard[kNumPages];
    for (int i = 0; i < kNumPages; i++) { py[i] = P(64); lastCard[i] = -1; }

    for (auto& d : defs) {
        int& y = py[d.page];
        if (d.card != lastCard[d.page]) {
            if (lastCard[d.page] != -1) y += P(CARD_GAP);
            if (cardHeader(d.page, d.card)) y += P(22);
            y += P(8);
            lastCard[d.page] = d.card;
        }
        addRow(hwnd, d.page, d.card, d.kind, d.id, d.label, y);
        y += P(ROW_H);
    }
    BuildButtonsPage(hwnd);
    ShowPage(0);
}

static void ShowTrayMenu(HWND hwnd) {
    POINT p; GetCursorPos(&p);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_OPEN, L"Open settings");
    AppendMenuW(m, MF_STRING | (g_cfg.enabled ? MF_CHECKED : MF_UNCHECKED), IDM_TOGGLE, L"Enabled");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quit smooly");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(m);
}

static RECT sidebarItem(int i) {
    RECT r = { P(12), P(58) + i * P(46), P(SB_W - 12), P(58) + i * P(46) + P(40) };
    return r;
}
static Gdiplus::Color gpc(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }
static void fillRound(HDC dc, RECT rc, int rad, COLORREF col) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float x = (float)rc.left, y = (float)rc.top, w = (float)(rc.right - rc.left), h = (float)(rc.bottom - rc.top);
    float d = rad * 2.0f; if (d > w) d = w; if (d > h) d = h;
    Gdiplus::GraphicsPath p;
    p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90); p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    Gdiplus::SolidBrush b(gpc(col)); g.FillPath(&b, &p);
}
static void fillEllipse(HDC dc, int x, int y, int w, int h, COLORREF col) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush b(gpc(col)); g.FillEllipse(&b, x, y, w, h);
}

// ---- minimal SVG path renderer (HugeIcons: stroke, circular arcs only) ----
static const char* svgNum(const char* s, double& out) {
    while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    char* e; out = strtod(s, &e); return e;
}
static void svgArc(Gdiplus::GraphicsPath& gp, double x0, double y0, double rx, double ry,
                   double la, double sw, double x1, double y1) {
    const double PI = 3.14159265358979323846;
    double r = (rx + ry) / 2.0;
    double dx = (x0 - x1) / 2.0, dy = (y0 - y1) / 2.0, d2 = dx * dx + dy * dy;
    if (d2 < 1e-9) return;
    if (r * r < d2) r = sqrt(d2);
    double num = r * r - d2; if (num < 0) num = 0;
    double f = sqrt(num / d2); if ((int)la == (int)sw) f = -f;
    double cx = f * dy + (x0 + x1) / 2.0, cy = -f * dx + (y0 + y1) / 2.0;
    double a0 = atan2(y0 - cy, x0 - cx), a1 = atan2(y1 - cy, x1 - cx), sweep = a1 - a0;
    if ((int)sw == 0 && sweep > 0) sweep -= 2 * PI;
    if ((int)sw == 1 && sweep < 0) sweep += 2 * PI;
    gp.AddArc((float)(cx - r), (float)(cy - r), (float)(2 * r), (float)(2 * r),
              (float)(a0 * 180 / PI), (float)(sweep * 180 / PI));
}
static void svgBuild(Gdiplus::GraphicsPath& gp, const char* d) {
    double cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;
    char cmd = 0, prev = 0; const char* p = d;
    auto rd = [&](double& v) { p = svgNum(p, v); };
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (!*p) break;
        if (isalpha((unsigned char)*p)) { cmd = *p++; }
        switch (cmd) {
        case 'M': case 'm': { double x, y; rd(x); rd(y); if (cmd == 'm') { x += cx; y += cy; } cx = x; cy = y; sx = x; sy = y; gp.StartFigure(); cmd = (cmd == 'm') ? 'l' : 'L'; break; }
        case 'L': case 'l': { double x, y; rd(x); rd(y); if (cmd == 'l') { x += cx; y += cy; } gp.AddLine((float)cx, (float)cy, (float)x, (float)y); cx = x; cy = y; break; }
        case 'H': case 'h': { double x; rd(x); if (cmd == 'h') x += cx; gp.AddLine((float)cx, (float)cy, (float)x, (float)cy); cx = x; break; }
        case 'V': case 'v': { double y; rd(y); if (cmd == 'v') y += cy; gp.AddLine((float)cx, (float)cy, (float)cx, (float)y); cy = y; break; }
        case 'C': case 'c': { double x1, y1, x2, y2, x, y; rd(x1); rd(y1); rd(x2); rd(y2); rd(x); rd(y); if (cmd == 'c') { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; } gp.AddBezier((float)cx, (float)cy, (float)x1, (float)y1, (float)x2, (float)y2, (float)x, (float)y); lcx = x2; lcy = y2; cx = x; cy = y; break; }
        case 'S': case 's': { double x2, y2, x, y; rd(x2); rd(y2); rd(x); rd(y); if (cmd == 's') { x2 += cx; y2 += cy; x += cx; y += cy; } double x1, y1; if (prev == 'C' || prev == 'c' || prev == 'S' || prev == 's') { x1 = 2 * cx - lcx; y1 = 2 * cy - lcy; } else { x1 = cx; y1 = cy; } gp.AddBezier((float)cx, (float)cy, (float)x1, (float)y1, (float)x2, (float)y2, (float)x, (float)y); lcx = x2; lcy = y2; cx = x; cy = y; break; }
        case 'A': case 'a': { double rx, ry, xr, la, sw, x, y; rd(rx); rd(ry); rd(xr); rd(la); rd(sw); rd(x); rd(y); if (cmd == 'a') { x += cx; y += cy; } svgArc(gp, cx, cy, rx, ry, la, sw, x, y); cx = x; cy = y; break; }
        case 'Z': case 'z': gp.CloseFigure(); cx = sx; cy = sy; break;
        default: p++; break;
        }
        prev = cmd;
    }
}
static void drawSvgG(Gdiplus::Graphics& g, int x, int y, int size, COLORREF col, const SvgIcon& ic, double strokeW, bool solid) {
    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform((float)x, (float)y);
    g.ScaleTransform((float)(size / 24.0), (float)(size / 24.0));
    if (solid) {
        Gdiplus::GraphicsPath gp; gp.SetFillMode(Gdiplus::FillModeAlternate);
        for (int i = 0; i < ic.n; i++) svgBuild(gp, ic.paths[i]);
        Gdiplus::SolidBrush b(gpc(col)); g.FillPath(&b, &gp);
    } else {
        Gdiplus::Pen pen(gpc(col), (float)strokeW);
        pen.SetLineJoin(Gdiplus::LineJoinRound); pen.SetStartCap(Gdiplus::LineCapRound); pen.SetEndCap(Gdiplus::LineCapRound);
        for (int i = 0; i < ic.n; i++) { Gdiplus::GraphicsPath gp; svgBuild(gp, ic.paths[i]); g.DrawPath(&pen, &gp); }
    }
    g.Restore(st);
}
static void drawSvg(HDC dc, int x, int y, int size, COLORREF col, const SvgIcon& ic, double strokeW = 1.6, bool solid = false) {
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    drawSvgG(g, x, y, size, col, ic, strokeW, solid);
}
static void drawToggle(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT rc = d->rcItem;
    int* v = toggleCfg(d->CtlID); bool on = v && *v;
    HBRUSH bg = CreateSolidBrush(C_CARD); FillRect(dc, &rc, bg); DeleteObject(bg);
    int h = rc.bottom - rc.top;
    fillRound(dc, rc, h, on ? C_ACCENT : C_KNOB);
    int kr = h / 2 - P(3), cy = (rc.top + rc.bottom) / 2;
    int kx = on ? rc.right - h / 2 : rc.left + h / 2;
    fillEllipse(dc, kx - kr, cy - kr, 2 * kr, 2 * kr, RGB(255, 255, 255));
}
static void drawDropdown(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT rc = d->rcItem;
    HBRUSH bg = CreateSolidBrush(C_CARD); FillRect(dc, &rc, bg); DeleteObject(bg);
    fillRound(dc, rc, P(12), C_LINE);
    SetBkMode(dc, TRANSPARENT);
    std::wstring t = ddText(d->CtlID);
    RECT tr = rc; tr.left += P(12); tr.right -= P(28);
    SelectObject(dc, g_font); SetTextColor(dc, C_TEXT);
    DrawTextW(dc, t.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT cr = rc; cr.right -= P(9);
    SelectObject(dc, g_fontIcon); SetTextColor(dc, C_TEXT2);
    wchar_t chev[2] = { 0xE70D, 0 };
    DrawTextW(dc, chev, 1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}
static void drawRemove(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT rc = d->rcItem;
    HBRUSH bg = CreateSolidBrush(C_CARD); FillRect(dc, &rc, bg); DeleteObject(bg);
    fillRound(dc, rc, P(12), C_LINE);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0xe0, 0x6a, 0x6a));
    SelectObject(dc, g_font);
    DrawTextW(dc, L"Remove", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
static void drawSelPill(const DRAWITEMSTRUCT* d) {   // Buttons-page selector tab
    HDC dc = d->hDC; RECT rc = d->rcItem;
    bool sel = (int)d->CtlID - ID_SEL_BASE == g_selBtn;
    HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
    fillRound(dc, rc, rc.bottom - rc.top, sel ? C_ACCENT : C_CARD);
    std::wstring lbl; for (auto& r : g_rows) if (r.ctrl == d->hwndItem) { lbl = r.label; break; }
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_font);
    SetTextColor(dc, sel ? RGB(255, 255, 255) : C_TEXT2);
    DrawTextW(dc, lbl.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
// ---- custom dark dropdown popup ----
static std::vector<std::wstring> g_popItems;
static int g_popCur = 0, g_popHover = -1, g_popSel = -2, g_popItemH = 34, g_popTop = 8;
LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto idxAt = [&](int my) { int i = (my - P(g_popTop)) / g_popItemH; return (i >= 0 && i < (int)g_popItems.size()) ? i : -1; };
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: { int i = idxAt((short)HIWORD(l)); if (i != g_popHover) { g_popHover = i; InvalidateRect(h, nullptr, FALSE); } return 0; }
    case WM_LBUTTONUP: {
        int mx = (short)LOWORD(l), my = (short)HIWORD(l); RECT rc; GetClientRect(h, &rc);
        g_popSel = (mx < 0 || my < 0 || mx >= rc.right || my >= rc.bottom) ? -1 : idxAt(my);
        DestroyWindow(h); return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(l), my = (short)HIWORD(l); RECT rc; GetClientRect(h, &rc);
        if (mx < 0 || my < 0 || mx >= rc.right || my >= rc.bottom) { g_popSel = -1; DestroyWindow(h); }
        return 0;
    }
    case WM_CAPTURECHANGED: if ((HWND)l != h && IsWindow(h)) { if (g_popSel == -2) g_popSel = -1; DestroyWindow(h); } return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(hdc); HBITMAP bmp = CreateCompatibleBitmap(hdc, cr.right, cr.bottom); HGDIOBJ ob = SelectObject(dc, bmp);
        HBRUSH bg = CreateSolidBrush(RGB(0x20, 0x20, 0x26)); FillRect(dc, &cr, bg); DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        for (int i = 0; i < (int)g_popItems.size(); i++) {
            int y = P(g_popTop) + i * g_popItemH; RECT ir = { P(6), y, cr.right - P(6), y + g_popItemH };
            if (i == g_popHover) fillRound(dc, ir, P(8), C_ACCENT);
            SelectObject(dc, g_font); SetTextColor(dc, i == g_popHover ? RGB(255, 255, 255) : C_TEXT);
            RECT tr = { P(36), y, cr.right - P(10), y + g_popItemH };
            DrawTextW(dc, g_popItems[i].c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (i == g_popCur) {
                SelectObject(dc, g_fontIcon); SetTextColor(dc, i == g_popHover ? RGB(255, 255, 255) : C_ACCENT);
                RECT ck = { P(12), y, P(32), y + g_popItemH }; wchar_t chk[2] = { 0xE73E, 0 };
                DrawTextW(dc, chk, 1, &ck, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        BitBlt(hdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc);
        EndPaint(h, &ps); return 0;
    }
    case WM_DESTROY: ReleaseCapture(); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void doDropdown(HWND hwnd, int id) {
    DdInfo d; if (!ddInfo(id, d)) return;
    g_popItems.clear(); int cur = 0;
    if (d.theme) {
        g_popItems.push_back(L"Windows Default");
        for (auto& t : scanThemes()) g_popItems.push_back(t);
        for (size_t i = 0; i < g_popItems.size(); i++)
            if (g_popItems[i] == g_cfg.theme || (g_cfg.theme.empty() && i == 0)) cur = (int)i;
    } else {
        for (int i = 0; i < d.n; i++) g_popItems.push_back(d.opts[i]);
        cur = (*d.val >= 0 && *d.val < d.n) ? *d.val : 0;
    }
    g_popCur = cur; g_popHover = cur; g_popSel = -2; g_popItemH = P(34);
    RECT rc; GetWindowRect(GetDlgItem(hwnd, id), &rc);
    int w = rc.right - rc.left, ph = 2 * P(g_popTop) + (int)g_popItems.size() * g_popItemH;
    HWND pop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"SmoolyPopup", L"", WS_POPUP,
                               rc.left, rc.bottom + P(2), w, ph, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, ph + 1, P(12), P(12)); SetWindowRgn(pop, rgn, TRUE);
    ShowWindow(pop, SW_SHOWNA); SetCapture(pop);
    MSG msg; while (IsWindow(pop) && GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    int idx = g_popSel;
    if (idx < 0) return;
    if (d.theme) { std::wstring n = g_popItems[idx]; { std::lock_guard<std::mutex> lock(g_mutex); g_cfg.theme = n; } ApplyCursorTheme(n); }
    else {
        { std::lock_guard<std::mutex> lock(g_mutex); *d.val = idx; }
        if (id == ID_SIZE) ApplyCursorTheme(g_cfg.theme);
        if ((idx == 7 || idx == 8) && id >= ID_DYN_BASE && id < ID_DYN_REMOVE) {
            int mi = (id - ID_DYN_BASE) / 8, tr = (id - ID_DYN_BASE) % 8;
            if (idx == 7) {                                          // Keyboard shortcut -> capture combo
                int combo = CaptureKeyCombo();
                if (combo) { if (tr == 0) g_maps[mi].keyC = combo; else if (tr == 1) g_maps[mi].keyD = combo; else g_maps[mi].keyH = combo; }
                else *d.val = 0;
            } else {                                                 // Type text -> prompt for a string
                std::wstring cur = tr == 0 ? g_maps[mi].txtC : tr == 1 ? g_maps[mi].txtD : g_maps[mi].txtH;
                std::wstring t = InputText(L"Type text", cur);
                if (!t.empty()) { if (tr == 0) g_maps[mi].txtC = t; else if (tr == 1) g_maps[mi].txtD = t; else g_maps[mi].txtH = t; }
                else *d.val = 0;
            }
        }
    }
    SaveConfig();
    InvalidateRect(GetDlgItem(hwnd, id), nullptr, TRUE);
}
static void PaintWindow(HWND hwnd, HDC hdc) {
    RECT cr; GetClientRect(hwnd, &cr);
    HDC dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cr.right, cr.bottom);
    HGDIOBJ ob = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);

    FillRect(dc, &cr, g_brBg);
    RECT sb = { 0, 0, P(SB_W), cr.bottom }; FillRect(dc, &sb, g_brSide);

    SelectObject(dc, g_fontTitle); SetTextColor(dc, C_TEXT);
    TextOutW(dc, P(20), P(18), L"smooly", 6);

    for (int i = 0; i < kNumPages; i++) {
        RECT ir = sidebarItem(i);
        if (i == g_page)            fillRound(dc, ir, P(10), C_ACCENT);
        else if (i == g_hoverItem)  fillRound(dc, ir, P(10), C_HOVER);
        bool sel = (i == g_page);
        int iy = ir.top + (ir.bottom - ir.top - P(20)) / 2;
        drawSvg(dc, ir.left + P(14), iy, P(20), sel ? RGB(255, 255, 255) : C_TEXT2, kIcons[i], 1.6, true);
        RECT tr = { ir.left + P(46), ir.top, ir.right, ir.bottom };
        SelectObject(dc, g_font); SetTextColor(dc, sel ? RGB(255, 255, 255) : C_TEXT);
        DrawTextW(dc, kPageNames[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dc, g_fontTitle); SetTextColor(dc, C_TEXT);
    TextOutW(dc, P(SB_W + 28), P(22), kPageNames[g_page], lstrlenW(kPageNames[g_page]));

    int cardL = P(SB_W + 28), cardR = P(WIN_W - 28);
    auto isRow = [](const UiRow& r) { return r.kind != 4 && r.kind != 6; };   // capture/selector are standalone
    auto drawCard = [&](int card, int top, int bot) {
        if (const wchar_t* hdr = cardHeader(g_page, card)) {
            SelectObject(dc, g_fontSmall); SetTextColor(dc, C_TEXT2);
            TextOutW(dc, cardL + P(4), top - P(20), hdr, lstrlenW(hdr));
        }
        RECT c = { cardL, top, cardR, bot }; fillRound(dc, c, P(14), C_CARD);
    };
    int curCard = -999, top = 0, bot = 0;
    for (auto& r : g_rows) if (r.page == g_page && isRow(r)) {
        if (r.card != curCard) { if (curCard != -999) drawCard(curCard, top, bot); curCard = r.card; top = r.top - P(8); }
        bot = r.top + r.h + P(8);
    }
    if (curCard != -999) drawCard(curCard, top, bot);

    curCard = -999;
    HPEN line = CreatePen(PS_SOLID, 1, C_LINE);
    for (auto& r : g_rows) if (r.page == g_page && isRow(r)) {
        int ly = r.top;
        if (r.card == curCard) {
            HGDIOBJ op = SelectObject(dc, line);
            MoveToEx(dc, cardL + P(18), ly, nullptr); LineTo(dc, cardR - P(14), ly);
            SelectObject(dc, op);
        }
        curCard = r.card;
        int ctlW = r.kind == 0 ? P(46) : r.kind == 1 ? P(194) : r.kind == 5 ? P(120) : P(250);
        RECT lr = { cardL + P(18), ly, cardR - ctlW - P(12), ly + r.h };
        SelectObject(dc, g_font); SetTextColor(dc, C_TEXT);
        DrawTextW(dc, r.label.c_str(), -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    DeleteObject(line);

    SelectObject(dc, g_fontSmall); SetTextColor(dc, C_TEXT2);
    RECT fr = { cardL, cr.bottom - P(28), cardR, cr.bottom - P(8) };
    DrawTextW(dc, L"Closing this window keeps smooly running in the tray.", -1, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(hdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        BuildControls(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        PaintWindow(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* di = (const DRAWITEMSTRUCT*)lParam;
        for (auto& r : g_rows) if (r.ctrl == di->hwndItem) {
            if (r.kind == 0) drawToggle(di); else if (r.kind == 1) drawDropdown(di); else if (r.kind == 5) drawRemove(di); else if (r.kind == 6) drawSelPill(di);
            return TRUE;
        }
        return TRUE;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        for (int i = 0; i < kNumPages; i++) {
            RECT ir = sidebarItem(i);
            if (PtInRect(&ir, pt)) { if (i != g_page) ShowPage(i); break; }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
        int hov = -1;
        if (mx < P(SB_W)) { POINT pt = { mx, my }; for (int i = 0; i < kNumPages; i++) { RECT ir = sidebarItem(i); if (PtInRect(&ir, pt)) { hov = i; break; } } }
        if (hov != g_hoverItem) {
            g_hoverItem = hov;
            RECT s = { 0, 0, P(SB_W), 10000 }; InvalidateRect(hwnd, &s, TRUE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hoverItem != -1) { g_hoverItem = -1; RECT s = { 0, 0, P(SB_W), 10000 }; InvalidateRect(hwnd, &s, TRUE); }
        return 0;

    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hwnd, ID_PSPEED)) {
            int pos = HIWORD(wParam);
            { std::lock_guard<std::mutex> lock(g_mutex); g_cfg.pointerSpeed = pos; }
            ApplyPointerSpeed(pos);
            SaveConfig();
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == IDM_OPEN)   { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); return 0; }
        if (id == IDM_QUIT)   { DestroyWindow(hwnd); return 0; }
        if (id == IDM_TOGGLE) { { std::lock_guard<std::mutex> lock(g_mutex); g_cfg.enabled = !g_cfg.enabled; } SaveConfig(); InvalidateRect(GetDlgItem(hwnd, ID_ENABLE), nullptr, TRUE); return 0; }
        if (code != BN_CLICKED) return 0;
        if (id >= ID_SEL_BASE && id < ID_SEL_BASE + 100) {       // pick which button to show
            int mi = id - ID_SEL_BASE;
            if (mi >= 0 && mi < (int)g_maps.size() && mi != g_selBtn) { g_selBtn = mi; RebuildButtons(hwnd); }
            return 0;
        }
        if (id >= ID_DYN_REMOVE && id < ID_DYN_REMOVE + 100) {   // remove a registered button
            int mi = id - ID_DYN_REMOVE;
            if (mi >= 0 && mi < (int)g_maps.size()) { g_maps.erase(g_maps.begin() + mi); if (g_selBtn >= (int)g_maps.size()) g_selBtn = (int)g_maps.size() - 1; RebuildButtons(hwnd); SaveConfig(); }
            return 0;
        }
        if (int* v = toggleCfg(id)) {
            { std::lock_guard<std::mutex> lock(g_mutex); *v = !*v; }
            if (id == ID_NOACCEL) ApplyNoAccel(*v);
            if (id == ID_STARTUP) SetStartup(*v);
            SaveConfig();
            InvalidateRect(GetDlgItem(hwnd, id), nullptr, TRUE);
            return 0;
        }
        DdInfo dd; if (ddInfo(id, dd)) doDropdown(hwnd, id);
        return 0;
    }

    case WM_APP + 2: {   // capture box registered a button
        int btn = (int)wParam;
        if (btn == 3 || btn == 4 || btn == 5) {
            bool ex = false; for (auto& m : g_maps) if (m.button == btn) { ex = true; break; }
            if (!ex) { g_maps.push_back(BtnMap{ btn }); g_selBtn = (int)g_maps.size() - 1; RebuildButtons(hwnd); SaveConfig(); }
        }
        return 0;
    }

    case WM_TRAY:
        if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
        } else if (LOWORD(lParam) == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        return 0;

    case WM_CLOSE:                 // keep running in the tray instead of quitting
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcFreq = f.QuadPart;
    HDC sdc = GetDC(nullptr); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    S = g_dpi / 96.0;

    Gdiplus::GdiplusStartupInput gpsi;
    Gdiplus::GdiplusStartup(&g_gdiplus, &gpsi, nullptr);

    SystemParametersInfoW(SPI_GETMOUSE, 0, g_origMouse, 0);
    if (g_origMouse[2] == 0) { g_origMouse[0] = 6; g_origMouse[1] = 10; g_origMouse[2] = 1; }
    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &g_sysSpeed, 0);

    LoadConfig();
    if (g_cfg.startup) SetStartup(true);   // keep registry in sync
    RestoreCursors();                      // clear any leftover big cursor from a prior hard-kill
    if ((!g_cfg.theme.empty() && g_cfg.theme != L"Windows Default") || g_cfg.size != 1)
        ApplyCursorTheme(g_cfg.theme);     // re-assert theme + size (also fixes paths if exe moved)
    if (g_cfg.pointerSpeed > 0) ApplyPointerSpeed(g_cfg.pointerSpeed);
    if (g_cfg.noAccel)          ApplyNoAccel(true);

    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&ic);

    std::wstring fdir = exeDir() + L"\\fonts\\";   // load bundled Inter privately (works uninstalled)
    int loaded = AddFontResourceExW((fdir + L"Inter-Regular.ttf").c_str(), FR_PRIVATE, nullptr)
               + AddFontResourceExW((fdir + L"Inter-Medium.ttf").c_str(), FR_PRIVATE, nullptr)
               + AddFontResourceExW((fdir + L"Inter-SemiBold.ttf").c_str(), FR_PRIVATE, nullptr);
    // Inter static TTFs expose each weight as its OWN family; reference them exactly.
    const wchar_t* bodyFace  = loaded > 0 ? L"Inter Medium"   : L"Segoe UI";
    const wchar_t* titleFace = loaded > 0 ? L"Inter SemiBold" : L"Segoe UI Semibold";
    const wchar_t* smallFace = loaded > 0 ? L"Inter"          : L"Segoe UI";
    auto mkFont = [&](int pt, int weight, const wchar_t* f) {
        return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
    };
    g_font      = mkFont(10, FW_NORMAL, bodyFace);
    g_fontTitle = mkFont(15, FW_NORMAL, titleFace);
    g_fontSmall = mkFont(9,  FW_NORMAL, smallFace);
    g_fontIcon  = mkFont(13, FW_NORMAL, L"Segoe MDL2 Assets");
    g_brSide = CreateSolidBrush(C_SIDE);
    g_brBg   = CreateSolidBrush(C_BG);
    g_icon = CreateBrandIcon();

    WNDCLASSW sc = {};
    sc.lpfnWndProc   = SliderProc;
    sc.hInstance     = hInst;
    sc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    sc.lpszClassName = L"SmoolySlider";
    RegisterClassW(&sc);

    WNDCLASSW cc = {};
    cc.lpfnWndProc   = CaptureProc;
    cc.hInstance     = hInst;
    cc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    cc.lpszClassName = L"SmoolyCapture";
    RegisterClassW(&cc);

    WNDCLASSW pc = {};
    pc.lpfnWndProc   = PopupProc;
    pc.hInstance     = hInst;
    pc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    pc.lpszClassName = L"SmoolyPopup";
    RegisterClassW(&pc);

    WNDCLASSW ec = {};
    ec.lpfnWndProc   = InputProc;
    ec.hInstance     = hInst;
    ec.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    ec.hbrBackground = g_brBg;
    ec.lpszClassName = L"SmoolyInput";
    RegisterClassW(&ec);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = L"SmoolyWindow";
    wc.hIcon         = g_icon;
    RegisterClassW(&wc);

    RECT rc = { 0, 0, P(WIN_W), P(WIN_H) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&rc, style, FALSE);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_wnd = CreateWindowW(L"SmoolyWindow", L"smooly", style, x, y, w, h,
                          nullptr, nullptr, hInst, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_wnd, 20, &dark, sizeof(dark));   // dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE)

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_wnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = g_icon;
    lstrcpyW(g_nid.szTip, L"smooly — smooth scroll");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, hInst, 0);
    if (!g_hook) {
        MessageBoxW(g_wnd, L"Failed to install the mouse hook.", L"smooly", MB_ICONERROR);
        return 1;
    }

    std::thread anim(AnimationLoop);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {   // this thread pumps -> the LL hook fires here
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running.store(false);
    anim.join();
    UnhookWindowsHookEx(g_hook);
    Gdiplus::GdiplusShutdown(g_gdiplus);
    return 0;
}
