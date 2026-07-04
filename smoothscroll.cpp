// smoothscroll.cpp — minimal Windows smooth-scroll core (Milestone 1)
// MSVC:  cl /EHsc /O2 smoothscroll.cpp user32.lib winmm.lib
// MinGW: g++ -O2 smoothscroll.cpp -o smoothscroll.exe -luser32 -lwinmm

#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")

// Tag stamped onto events WE synthesize, so our hook can recognize and
// pass them through instead of re-capturing them (which would loop forever).
static const ULONG_PTR kInjectTag = 0xC0FFEE;

static std::mutex        g_mutex;
static double            g_remaining = 0.0;   // scroll distance still owed
static std::atomic<bool> g_running{true};
static DWORD             g_mainThreadId = 0;
static HHOOK             g_hook = nullptr;

// Tunables — this is where the "feel" lives.
static const double kMultiplier    = 1.0;   // worth of each physical notch
static const double kEasing        = 0.18;  // fraction of remaining emitted per frame
static const int    kFrameHz       = 120;   // animation frame rate
static const double kStopThreshold = 0.5;   // snap remaining to 0 below this

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        // Let our own synthesized events pass straight to the app.
        if (ms->dwExtraInfo == kInjectTag)
            return CallNextHookEx(g_hook, nCode, wParam, lParam);

        // A real physical notch: +120 up, -120 down.
        short delta = (short)HIWORD(ms->mouseData);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_remaining += (double)delta * kMultiplier;
        }
        // Swallow it — Windows won't deliver the raw notch.
        return 1;
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

void EmitWheel(int amount) {
    INPUT in = {};
    in.type          = INPUT_MOUSE;
    in.mi.dwFlags    = MOUSEEVENTF_WHEEL;
    in.mi.mouseData  = (DWORD)amount;   // signed wheel delta
    in.mi.dwExtraInfo = kInjectTag;
    SendInput(1, &in, sizeof(INPUT));
}

void AnimationLoop() {
    timeBeginPeriod(1);  // ask the OS for ~1ms sleep granularity
    const double frameMs = 1000.0 / kFrameHz;
    double emitAccum = 0.0;  // carries the sub-unit remainder between frames

    while (g_running.load()) {
        double step = 0.0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (std::fabs(g_remaining) > 0.0) {
                step = g_remaining * kEasing;   // ease-out: take a slice of what's left
                g_remaining -= step;
                if (std::fabs(g_remaining) < kStopThreshold) {
                    step += g_remaining;        // flush the tail so we land clean
                    g_remaining = 0.0;
                }
            }
        }

        if (step != 0.0) {
            emitAccum += step;
            int whole = (int)emitAccum;   // wheel deltas are integers
            emitAccum -= whole;           // keep the fraction for next frame
            if (whole != 0) EmitWheel(whole);
        }

        std::this_thread::sleep_for(
            std::chrono::duration<double, std::milli>(frameMs));
    }
    timeEndPeriod(1);
}

BOOL WINAPI ConsoleHandler(DWORD sig) {
    if (sig == CTRL_C_EVENT || sig == CTRL_CLOSE_EVENT) {
        g_running.store(false);
        PostThreadMessage(g_mainThreadId, WM_QUIT, 0, 0);
        return TRUE;
    }
    return FALSE;
}

int main() {
    g_mainThreadId = GetCurrentThreadId();
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    g_hook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(nullptr), 0);
    if (!g_hook) { printf("hook failed: %lu\n", GetLastError()); return 1; }

    std::thread anim(AnimationLoop);
    printf("Smooth scroll running. Scroll the wheel. Ctrl+C to quit.\n");

    // The low-level hook only fires while THIS thread pumps messages.
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running.store(false);
    anim.join();
    UnhookWindowsHookEx(g_hook);
    return 0;
}
