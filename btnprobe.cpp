// btnprobe — diagnose what smooly's hooks actually see from your mouse.
// Installs the SAME low-level mouse + keyboard hooks smooly uses, plus a window
// to catch WM_APPCOMMAND, and prints every button / wheel / side-button event.
//
// Build:  g++ btnprobe.cpp -o btnprobe.exe -luser32 -municode
// Run:    .\btnprobe.exe   (then press Back/Forward/Middle, drag, scroll; Esc/close to quit)
//
// What to look for after pressing your side buttons (button 4 = Back, 5 = Forward):
//   * "MOUSE  XBUTTON1/2 down/up"      -> good: smooly CAN see & remap this button.
//   * "KEY    BROWSER_BACK/FORWARD"    -> mouse sends KEYBOARD keys, not mouse buttons.
//   * "APPCOMMAND BROWSER_*"           -> mouse sends app-commands.
// If a side button prints only KEY/APPCOMMAND (no XBUTTON), smooly's mouse hook
// never receives it — that's why remapping does nothing.

#include <windows.h>
#include <cstdio>

static HHOOK g_mh, g_kh;

static void stamp() { printf("[%6lu] ", GetTickCount() % 1000000); }

static LRESULT CALLBACK MouseProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        auto* m = (MSLLHOOKSTRUCT*)l;
        bool inj = (m->flags & LLMHF_INJECTED) != 0;
        const char* tag = inj ? " (injected)" : "";
        switch (w) {
        case WM_MBUTTONDOWN: stamp(); printf("MOUSE  MIDDLE down%s\n", tag); break;
        case WM_MBUTTONUP:   stamp(); printf("MOUSE  MIDDLE up%s\n", tag); break;
        case WM_XBUTTONDOWN: stamp(); printf("MOUSE  XBUTTON%d down%s\n", HIWORD(m->mouseData), tag); break;
        case WM_XBUTTONUP:   stamp(); printf("MOUSE  XBUTTON%d up%s\n", HIWORD(m->mouseData), tag); break;
        case WM_MOUSEWHEEL:  stamp(); printf("MOUSE  WHEEL  v delta=%d%s\n", (short)HIWORD(m->mouseData), tag); break;
        case WM_MOUSEHWHEEL: stamp(); printf("MOUSE  WHEEL  h delta=%d%s\n", (short)HIWORD(m->mouseData), tag); break;
        }
        fflush(stdout);
    }
    return CallNextHookEx(g_mh, code, w, l);
}

static const char* keyName(DWORD vk) {
    switch (vk) {
    case VK_BROWSER_BACK:    return "BROWSER_BACK";
    case VK_BROWSER_FORWARD: return "BROWSER_FORWARD";
    case VK_BROWSER_REFRESH: return "BROWSER_REFRESH";
    case VK_BROWSER_HOME:    return "BROWSER_HOME";
    case VK_VOLUME_UP:       return "VOLUME_UP";
    case VK_VOLUME_DOWN:     return "VOLUME_DOWN";
    default: return nullptr;
    }
}

static LRESULT CALLBACK KeyProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
        auto* k = (KBDLLHOOKSTRUCT*)l;
        const char* nm = keyName(k->vkCode);
        if (nm) { stamp(); printf("KEY    %s down (vk=%lu)%s\n", nm, k->vkCode,
                                  (k->flags & LLKHF_INJECTED) ? " (injected)" : ""); fflush(stdout); }
    }
    return CallNextHookEx(g_kh, code, w, l);
}

static const char* appCmdName(int c) {
    switch (c) {
    case APPCOMMAND_BROWSER_BACKWARD: return "BROWSER_BACKWARD";
    case APPCOMMAND_BROWSER_FORWARD:  return "BROWSER_FORWARD";
    case APPCOMMAND_BROWSER_REFRESH:  return "BROWSER_REFRESH";
    default: return nullptr;
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_APPCOMMAND) {
        int cmd = GET_APPCOMMAND_LPARAM(l);
        const char* nm = appCmdName(cmd);
        stamp(); printf("APPCOMMAND %s (cmd=%d)\n", nm ? nm : "other", cmd); fflush(stdout);
    }
    return DefWindowProcW(h, msg, w, l);
}

int wmain() {
    printf("btnprobe running. Press your Back/Forward/Middle buttons, scroll, drag.\n");
    printf("Watch whether side buttons show as MOUSE XBUTTON (good) or KEY/APPCOMMAND (bad).\n");
    printf("Close this window or press Esc to quit.\n\n");
    fflush(stdout);

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BtnProbeWnd"; RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"BtnProbeWnd", L"btnprobe", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    g_mh = SetWindowsHookExW(WH_MOUSE_LL,    MouseProc, GetModuleHandleW(nullptr), 0);
    g_kh = SetWindowsHookExW(WH_KEYBOARD_LL, KeyProc,   GetModuleHandleW(nullptr), 0);
    if (!g_mh || !g_kh) { printf("Failed to install hooks (err %lu)\n", GetLastError()); return 1; }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) break;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(g_mh); UnhookWindowsHookEx(g_kh);
    return 0;
}
