#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace tray {

constexpr UINT WM_TRAY_ICON = WM_APP + 10;
constexpr UINT ID_TRAY_ICON = 1;

// Context menu IDs
constexpr UINT IDM_QUIT = 1000;
constexpr UINT IDM_AUTOSTART = 1001;
constexpr UINT IDM_SETTINGS = 1002;
constexpr UINT IDM_MIC_BASE = 2000; // mic devices start at 2000

enum class State {
    Idle,
    Initializing,
    Recording,
    Transcribing,
};

struct AudioDevice {
    std::wstring id;
    std::wstring name;
};

void create(HWND hwnd);
void destroy();
void setState(State state);
void showBalloon(const wchar_t* title, const wchar_t* message);
void showContextMenu(HWND hwnd, const std::vector<AudioDevice>& devices, int selectedDeviceIndex);

}
