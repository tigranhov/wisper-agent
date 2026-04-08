#include "tray.h"
#include "version.h"
#include <shellapi.h>

namespace tray {

static NOTIFYICONDATAW g_nid = {};
static HICON g_iconIdle = nullptr;
static HICON g_iconRecording = nullptr;
static HICON g_iconTranscribing = nullptr;

static HICON loadIconFromFile(const wchar_t* filename) {
    wchar_t exeDir[MAX_PATH];
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    // Try release layout: <exe>/assets/icons/
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sassets\\icons\\%s", exeDir, filename);
    HICON icon = (HICON)LoadImageW(nullptr, path, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (icon) return icon;

    // Try dev layout: <exe>/../../assets/icons/
    swprintf_s(path, L"%s..\\..\\assets\\icons\\%s", exeDir, filename);
    return (HICON)LoadImageW(nullptr, path, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
}

void create(HWND hwnd) {
    g_iconIdle = loadIconFromFile(L"tray-idle.ico");
    g_iconRecording = loadIconFromFile(L"tray-recording.ico");
    g_iconTranscribing = loadIconFromFile(L"tray-transcribing.ico");

    // Fallback to default app icon if files not found
    if (!g_iconIdle) g_iconIdle = LoadIconW(nullptr, IDI_APPLICATION);
    if (!g_iconRecording) g_iconRecording = g_iconIdle;
    if (!g_iconTranscribing) g_iconTranscribing = g_iconIdle;

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = g_iconIdle;
    swprintf_s(g_nid.szTip, L"SpeakInto v" SPEAKINTO_VERSION_W L" \u2014 Ready (Ctrl+`)");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void destroy() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void setState(State state) {
    switch (state) {
        case State::Idle:
            g_nid.hIcon = g_iconIdle;
            swprintf_s(g_nid.szTip, L"SpeakInto v" SPEAKINTO_VERSION_W L" \u2014 Ready (Ctrl+`)");
            break;
        case State::Initializing:
            g_nid.hIcon = g_iconIdle;
            swprintf_s(g_nid.szTip, L"SpeakInto v" SPEAKINTO_VERSION_W L" \u2014 Initializing...");
            break;
        case State::Recording:
            g_nid.hIcon = g_iconRecording;
            wcscpy_s(g_nid.szTip, L"SpeakInto \u2014 Recording...");
            break;
        case State::Transcribing:
            g_nid.hIcon = g_iconTranscribing;
            wcscpy_s(g_nid.szTip, L"SpeakInto \u2014 Transcribing...");
            break;
        case State::Downloading:
            g_nid.hIcon = g_iconIdle;
            wcscpy_s(g_nid.szTip, L"SpeakInto \u2014 Downloading model...");
            break;
        case State::Error:
            g_nid.hIcon = g_iconIdle;
            wcscpy_s(g_nid.szTip, L"SpeakInto \u2014 No microphone detected");
            break;
    }
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void showBalloon(const wchar_t* title, const wchar_t* message) {
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title);
    wcscpy_s(g_nid.szInfo, message);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void showContextMenu(HWND hwnd, const std::vector<AudioDevice>& devices, int selectedDeviceIndex) {
    HMENU hMenu = CreatePopupMenu();
    HMENU hMicMenu = CreatePopupMenu();

    if (devices.empty()) {
        AppendMenuW(hMicMenu, MF_STRING | MF_GRAYED, 0, L"No microphones found");
    } else {
        for (size_t i = 0; i < devices.size(); i++) {
            UINT flags = MF_STRING;
            if ((int)i == selectedDeviceIndex) flags |= MF_CHECKED;
            AppendMenuW(hMicMenu, flags, IDM_MIC_BASE + i, devices[i].name.c_str());
        }
    }

    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"SpeakInto");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hMicMenu, L"Microphone");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(hMicMenu);
    DestroyMenu(hMenu);
}

}
