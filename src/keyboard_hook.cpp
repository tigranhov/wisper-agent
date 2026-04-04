#include "keyboard_hook.h"
#include <thread>
#include <atomic>

namespace keyboard {

static HHOOK g_hook = nullptr;
static std::thread g_hookThread;
static std::atomic<bool> g_running{false};
static std::atomic<bool> g_ctrlHeld{false};
static std::atomic<bool> g_winHeld{false};
static std::atomic<bool> g_comboActive{false};
static DWORD g_hookThreadId = 0;
static HWND g_hwndMain = nullptr;

static void checkComboState() {
    bool bothHeld = g_ctrlHeld.load() && g_winHeld.load();

    if (bothHeld && !g_comboActive.load()) {
        g_comboActive = true;
        PostMessage(g_hwndMain, WM_COMBO_DOWN, 0, 0);
    } else if (!bothHeld && g_comboActive.load()) {
        g_comboActive = false;
        PostMessage(g_hwndMain, WM_COMBO_UP, 0, 0);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) {
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    DWORD vk = kb->vkCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    // Track Ctrl state
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) {
        if (isKeyDown && !g_ctrlHeld.load()) {
            g_ctrlHeld = true;

            // Check if Win is already physically held (pressed before Ctrl)
            if (!g_winHeld.load() &&
                ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000))) {
                g_winHeld = true;
            }
            checkComboState();
        }
        if (isKeyUp) {
            g_ctrlHeld = false;
            checkComboState();
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    // Track Win key
    if (vk == VK_LWIN || vk == VK_RWIN) {
        if (isKeyDown) {
            if (!g_winHeld.load()) {
                g_winHeld = true;
                checkComboState();
            }
            // Suppress Win key when combo is active to prevent Start menu
            if (g_comboActive.load()) {
                return 1;
            }
        }
        if (isKeyUp) {
            g_winHeld = false;
            // Suppress Win release if combo was active (prevents Start menu)
            bool wasActive = g_comboActive.load();
            checkComboState();
            if (wasActive) {
                return 1;
            }
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static void hookThreadProc() {
    g_hookThreadId = GetCurrentThreadId();
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hook) return;

    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

void start(HWND hwndMain) {
    if (g_running) return;
    g_hwndMain = hwndMain;
    g_running = true;
    g_ctrlHeld = false;
    g_winHeld = false;
    g_comboActive = false;
    g_hookThread = std::thread(hookThreadProc);
}

void stop() {
    if (!g_running) return;
    g_running = false;
    if (g_hookThreadId != 0) {
        PostThreadMessage(g_hookThreadId, WM_QUIT, 0, 0);
    }
    if (g_hookThread.joinable()) {
        g_hookThread.join();
    }
    g_hookThreadId = 0;
}

}
