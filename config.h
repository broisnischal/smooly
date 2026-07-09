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
    int hspeed       = 1;   // horizontal scroll speed: 0=Slow 1=Medium 2=Fast
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
// keyC/keyD/keyH/keyT hold a captured shortcut (packed: (mods<<8)|vk) used when the
// matching Click/Double/Hold/Triple action is "Keyboard shortcut".
struct BtnMap {
    int button = 0;
    int click = 0, dbl = 0, hold = 0, scroll = 0, drag = 0, triple = 0;
    int keyC = 0, keyD = 0, keyH = 0, keyT = 0;      // captured combo when action = Keyboard shortcut
    std::wstring txtC, txtD, txtH, txtT;              // text to type when action = Type text
    int gU = 0, gD = 0, gL = 0, gR = 0;              // gesture direction actions (when drag = Gesture)
};

// Actions for Click / Double-Click / Hold / Triple-Click.  7 captures a key combo, 8 types a string.
//   0 Default (keep native) · 1 Middle · 2 Copy · 3 Paste · 4 Back · 5 Forward · 6 None (do nothing) · 7 Keyboard shortcut · 8 Type text
//   9 Next track · 10 Previous track · 11 Play/Pause · 12 Volume up · 13 Volume down
static const wchar_t* const kButtonActions[14] = {
    L"Default", L"Middle click", L"Copy", L"Paste", L"Back", L"Forward", L"None", L"Keyboard shortcut", L"Type text",
    L"Next track", L"Previous track", L"Play / Pause", L"Volume up", L"Volume down"
};
// Action while the button is held and the wheel is turned (Click and Scroll).
//   0 None · 1 Horizontal scroll · 2 Zoom (Ctrl+wheel) · 3 Fast (swift) scroll
static const wchar_t* const kScrollActions[4] = { L"None", L"Horizontal scroll", L"Zoom", L"Fast scroll" };
// Action while the button is held and the mouse is dragged (Click and Drag).
//   0 None · 1 Scroll (pan) · 2 Grab · 3 Switch desktop · 4 Task View · 5 Navigate · 6 Precision · 7 Gesture
static const wchar_t* const kDragActions[8] = { L"None", L"Scroll (drag to pan)", L"Grab (hand pan)", L"Switch desktop", L"Task View", L"Navigate (back/forward)", L"Precision (slow pointer)", L"Gesture (flick)" };

// Actions a gesture direction (Up/Down/Left/Right) can trigger. A curated system
// set — no per-direction key capture needed (keeps the UI simple).
static const wchar_t* const kGestureActions[] = {
    L"None", L"Back", L"Forward", L"Desktop left", L"Desktop right", L"Task View",
    L"Show desktop", L"Next track", L"Previous track", L"Play / Pause",
    L"Volume up", L"Volume down", L"Copy", L"Paste", L"Middle click"
};
static const int kNumGestureActions = 15;

static const int    kGestureMinPx   = 24;    // min net travel (px) for a flick to register
static const double kPrecisionFactor = 0.35; // OS pointer speed multiplier while a Precision button is held

static const int kHoldMs   = 220;   // press longer than this = Hold gesture
static const int kDblMs    = 260;   // second click within this = Double-Click
static const int kDragPx     = 6;   // movement past this while held = a drag
static const int kDeskPx     = 180; // horizontal drag per desktop switch
static const int kNavPx      = 120; // horizontal drag per back/forward navigation
static const int kSwiftMul   = 4;   // Fast (swift) scroll distance multiplier

// Drag-to-scroll (pan/grab) feel — routes through the smooth engine, MMF-style.
static const double kDragPanUnits  = 2.2;   // wheel units emitted per pixel of drag (~content vs hand speed)
static const double kDragGlideMs   = 16.0;  // SHORT glide while dragging — tight tracking, no lag balloon
static const double kInertiaMs     = 650.0; // coast duration after release (momentum)
static const double kInertiaMinVel = 1.2;   // min release speed (units/ms) to start a coast
static const double kInertiaCap    = 4000.0;// max coast distance (wheel units)
static const int    kTouchpadWindowMs = 150; // after touchpad activity, pass wheel through untouched this long

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

// Scroll-gesture duration (ms) per smoothness level. Every notch re-plans a
// cubic ease that pays out the whole owed distance over this window and lands
// at exactly zero velocity — so a scroll can neither creep at the tail nor stop
// on a cliff. New notches restart the window and carry the current velocity, so
// sustained scrolling reads as one continuous glide. Index 0 (Off) = passthrough.
//   0 Off · 1 Low · 2 Medium · 3 High · 4 Very High · 5 Buttery
static const double kDurBySmoothness[6] = { 0.0, 110.0, 160.0, 220.0, 300.0, 400.0 };

// Base scroll distance per physical notch (a native notch is 120 units).
static const double kStepBySpeed[3] = { 90.0, 150.0, 240.0 };

// Horizontal scroll speed multiplier (tilt wheel + Shift+wheel). Horizontal tends
// to feel slow, so even "Slow" is 1:1 and the default steps it up.
static const wchar_t* const kHSpeedNames[3] = { L"Slow", L"Medium", L"Fast" };
static const double         kHSpeedMul[3]   = { 1.0, 1.8, 3.0 };

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
