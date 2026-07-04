// config.h — tunables + persisted settings for smooly.
#pragma once
#include <string>

struct Config {
    int enabled      = 1;   // master on/off
    int smoothness   = 3;   // 0=Off 1=Low 2=Medium 3=High 4=Very High 5=Buttery
    int speed        = 1;   // 0=Low 1=Medium 2=High         (distance per notch)
    int accel        = 2;   // 0=Off 1=Low 2=Medium 3=High   (builds with sustained scrolling)
    int reverse      = 0;   // natural (reversed) vertical direction
    int horizontal   = 1;   // smooth the horizontal (tilt) wheel too
    int shiftHoriz   = 1;   // Shift + vertical wheel scrolls horizontally
    int ctrlTurbo    = 0;   // Ctrl + wheel = turbo scroll (overrides Ctrl+zoom while on)
    int gestures     = 0;   // Shift/Alt/Alt+Shift + click selection gestures
    int shake        = 1;   // shake the mouse to locate: cursor grows big
    int startup      = 0;   // launch with Windows
    std::wstring theme;     // cursor theme name ("" / "Windows Default" = system default)
    int size         = 1;   // cursor size: 0=Small 1=Regular 2=Large 3=Extra-Large
    int pointerSpeed = 0;   // OS pointer speed 1..20 (0 = leave the system value alone)
    int noAccel      = 0;   // disable "enhance pointer precision" (linear movement)
};

// A dynamically-registered mouse button and the actions bound to its triggers.
//   button: 3 = Middle, 4 = Back (X1), 5 = Forward (X2)
// keyC/keyD/keyH hold a captured shortcut (packed: (mods<<8)|vk) used when the
// matching Click/Double/Hold action is "Keyboard shortcut".
struct BtnMap {
    int button = 0;
    int click = 0, dbl = 0, hold = 0, scroll = 0, drag = 0;
    int keyC = 0, keyD = 0, keyH = 0;                // captured combo when action = Keyboard shortcut
    std::wstring txtC, txtD, txtH;                   // text to type when action = Type text
};

// Actions for Click / Double-Click / Hold.  7 captures a key combo, 8 types a string.
//   0 Default · 1 Middle · 2 Copy · 3 Paste · 4 Back · 5 Forward · 6 Disabled · 7 Keyboard shortcut · 8 Type text
static const wchar_t* const kButtonActions[9] = {
    L"Default", L"Middle click", L"Copy", L"Paste", L"Back", L"Forward", L"Disabled", L"Keyboard shortcut", L"Type text"
};
// Action while the button is held and the wheel is turned (Click and Scroll).
//   0 None · 1 Horizontal scroll · 2 Zoom (Ctrl+wheel) · 3 Fast (swift) scroll
static const wchar_t* const kScrollActions[4] = { L"None", L"Horizontal scroll", L"Zoom", L"Fast scroll" };
// Action while the button is held and the mouse is dragged (Click and Drag).
//   0 None · 1 Scroll (drag to pan) · 2 Switch desktop · 3 Task View
static const wchar_t* const kDragActions[4] = { L"None", L"Scroll (drag to pan)", L"Switch desktop", L"Task View" };

static const int kHoldMs   = 220;   // press longer than this = Hold gesture
static const int kDblMs    = 260;   // second click within this = Double-Click
static const int kDragPx     = 6;   // movement past this while held = a drag
static const int kDeskPx     = 180; // horizontal drag per desktop switch
static const int kSwiftMul   = 4;   // Fast (swift) scroll distance multiplier
static const int kDragScroll = 3;   // drag-to-pan sensitivity (wheel delta per pixel)

// Cursor sizes. Regular/Large/Extra-Large are bundled folders; "Small" has no
// folder and is produced by runtime-downscaling. kSizeScale is the scale applied
// when there's no native folder for the chosen size (always for Small, and for
// every non-Regular size of the bare Windows scheme).
static const wchar_t* const kSizeNames[4] = { L"Small", L"Regular", L"Large", L"Extra-Large" };
static const double         kSizeScale[4] = { 0.72,     1.0,        1.4,      1.85 };

// Shake-to-locate: detect rapid back-and-forth wiggling and grow the cursor.
static const double kShakeFactor       = 2.9;   // how many times bigger the cursor gets
static const int    kShakeMinDx        = 6;     // px of horizontal travel that counts as a move
static const int    kShakeWindowMs     = 600;   // reversals must land within this window
static const int    kShakeMinReversals = 3;     // direction flips needed to trigger
static const int    kShakeHoldMs       = 900;   // stay big this long after the last wiggle
static const double kShakeGrowRate     = 22.0;  // ease-in speed of the zoom (per second)
static const double kShakeShrinkRate   = 15.0;  // ease-out speed back to normal (per second)

// Ease rate (per second) at which the owed scroll distance is paid out. Higher =
// snappier arrival; lower = longer, silkier glide. Index 0 (Off) is a passthrough.
//   0 Off · 1 Low · 2 Medium · 3 High · 4 Very High · 5 Buttery
static const double kRateBySmoothness[6] = { 0.0, 25.0, 18.0, 13.0, 9.5, 7.0 };

// The owed distance always clears at >= this speed (units/sec). This caps the
// tail so a scroll never creeps before stopping. It mainly affects SHORT scrolls
// (a notch or two) — keeping them crisp instead of floaty — while long flings are
// dominated by the eased head and stay smooth.
static const double kMinScrollSpeed = 620.0;

// Base scroll distance per physical notch (a native notch is 120 units).
static const double kStepBySpeed[3] = { 90.0, 150.0, 240.0 };

// Acceleration builds with *sustained* fast scrolling rather than reacting to a
// single notch, so a short quick flick no longer overshoots. A per-axis counter
// rises with each notch and decays over time; the boost ramps in with it.
//   accel level -> extra multiplier at full ramp (Off/Low/Medium/High)
static const double kAccelByLevel[4] = { 0.0, 0.8, 1.6, 2.6 };
static const double kAccelRamp   = 5.0;    // rapid notches needed to reach full boost
static const double kAccelDecay  = 5.0;    // per-second decay of the built-up scroll rate
static const double kTurboFactor = 3.0;    // Ctrl-scroll distance multiplier

static const double kMaxNotchStack = 9.0;  // owed-distance ceiling, in notch-equivalents
static const int    kFrameHz       = 120;  // animation frame rate
