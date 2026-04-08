#include "settings.h"
#include "version.h"
#include <shlobj.h>
#include <fstream>
#include <string>
#include <vector>

namespace settings {

static std::atomic<bool> g_dialogOpen{false};

bool isDialogOpen() { return g_dialogOpen; }

// --- Paths ---

static std::wstring getSettingsDir() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        return L"";
    }
    return std::wstring(appdata) + L"\\wisper-agent";
}

static std::wstring getSettingsPath() {
    auto dir = getSettingsDir();
    if (dir.empty()) return L"";
    return dir + L"\\settings.json";
}

// --- Minimal JSON helpers ---

static std::string findValue(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    } else {
        auto end = json.find_first_of(",}\n\r ", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

static RepeatPressMode parseModeString(const std::string& s) {
    if (s == "flash") return RepeatPressMode::Flash;
    if (s == "cancel") return RepeatPressMode::Cancel;
    return RepeatPressMode::Queue;
}

static const char* modeToString(RepeatPressMode mode) {
    switch (mode) {
        case RepeatPressMode::Flash: return "flash";
        case RepeatPressMode::Cancel: return "cancel";
        default: return "queue";
    }
}

// --- Load / Save ---

Settings load() {
    Settings s;
    auto path = getSettingsPath();
    if (path.empty()) return s;

    std::ifstream file(path);
    if (!file.is_open()) return s; // fresh install — struct defaults apply (Small)

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    file.close();

    auto modeStr = findValue(json, "repeatPressMode");
    if (!modeStr.empty()) {
        s.repeatPressMode = parseModeString(modeStr);
    }

    auto micStr = findValue(json, "selectedMicIndex");
    if (!micStr.empty()) {
        try { s.selectedMicIndex = std::stoi(micStr); }
        catch (...) {}
    }

    auto modelStr = findValue(json, "modelSize");
    if (!modelStr.empty()) {
        s.modelSize = model::modelSizeFromString(modelStr);
    } else {
        // Existing user with old settings file — keep Base to avoid surprise download
        s.modelSize = model::ModelSize::Base;
    }

    auto procStr = findValue(json, "processorEnabled");
    if (procStr == "true") s.processorEnabled = true;

    auto vocabStr = findValue(json, "vocabPromptEnabled");
    if (vocabStr == "true") s.vocabPromptEnabled = true;

    return s;
}

void save(const Settings& s) {
    auto dir = getSettingsDir();
    if (dir.empty()) return;

    CreateDirectoryW(dir.c_str(), nullptr);

    auto path = getSettingsPath();

    auto tmpPath = path + L".tmp";
    std::ofstream file(tmpPath);
    if (!file.is_open()) return;

    file << "{\n";
    file << "  \"repeatPressMode\": \"" << modeToString(s.repeatPressMode) << "\",\n";
    file << "  \"selectedMicIndex\": " << s.selectedMicIndex << ",\n";
    file << "  \"modelSize\": \"" << model::modelSizeString(s.modelSize) << "\",\n";
    file << "  \"processorEnabled\": " << (s.processorEnabled ? "true" : "false") << ",\n";
    file << "  \"vocabPromptEnabled\": " << (s.vocabPromptEnabled ? "true" : "false") << "\n";
    file << "}\n";
    file.close();

    MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// --- Settings Dialog ---

static const wchar_t* SETTINGS_CLASS = L"WisperSettingsClass";
static Settings* g_dlgSettings = nullptr;
static bool g_dlgResult = false;
static bool g_dlgClosed = false;
static HWND g_dlgHwnd = nullptr;

void closeDialog() {
    if (g_dlgHwnd) {
        DestroyWindow(g_dlgHwnd);
    }
}

// Control IDs — repeat press mode
static constexpr int ID_RADIO_FLASH  = 101;
static constexpr int ID_RADIO_QUEUE  = 102;
static constexpr int ID_RADIO_CANCEL = 103;
// Control IDs — model size
static constexpr int ID_RADIO_TINY   = 201;
static constexpr int ID_RADIO_BASE   = 202;
static constexpr int ID_RADIO_SMALL  = 203;
static constexpr int ID_RADIO_MEDIUM = 204;
// Control IDs — vocab prompt
static constexpr int ID_CHECK_VOCAB     = 205;
// Control IDs — processor
static constexpr int ID_CHECK_PROCESSOR = 301;
static constexpr int ID_BTN_PROCESSOR   = 302;
static constexpr int ID_BTN_SHOW_LOG    = 303;
// Control IDs — updates
static constexpr int ID_BTN_CHECK_UPDATE   = 401;
static constexpr int ID_BTN_INSTALL_UPDATE = 402;
static constexpr int ID_BTN_VIEW_CHANGELOG = 403;
static constexpr int ID_STATIC_UPDATE_STATUS = 404;
// Buttons
static constexpr int ID_OK           = IDOK;
static constexpr int ID_CANCEL       = IDCANCEL;

// --- Log viewer ---

static const wchar_t* LOG_VIEWER_CLASS = L"WisperLogViewerClass";
static HFONT g_logFont = nullptr;

static LRESULT CALLBACK LogViewerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        if (g_logFont) { DeleteObject(g_logFont); g_logFont = nullptr; }
        return 0;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void showLogViewer(HINSTANCE hInstance) {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) return;
    std::wstring logPath = std::wstring(appdata) + L"\\wisper-agent\\transcription.log";

    std::ifstream file(logPath);
    if (!file.is_open()) return;

    // Read lines — format: [timestamp] | raw | refined | mode
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    file.close();

    // Keep last 50
    size_t start = lines.size() > 50 ? lines.size() - 50 : 0;

    // Build formatted display
    std::string display;
    for (size_t i = start; i < lines.size(); i++) {
        auto& l = lines[i];
        // Parse: [timestamp] | raw | refined | mode
        auto p1 = l.find(" | ");
        if (p1 == std::string::npos) { display += l + "\r\n"; continue; }
        auto p2 = l.find(" | ", p1 + 3);
        if (p2 == std::string::npos) { display += l + "\r\n"; continue; }
        auto p3 = l.find(" | ", p2 + 3);

        std::string timestamp = l.substr(0, p1);
        std::string raw = l.substr(p1 + 3, p2 - p1 - 3);
        std::string refined = l.substr(p2 + 3, p3 != std::string::npos ? p3 - p2 - 3 : std::string::npos);
        std::string mode = p3 != std::string::npos ? l.substr(p3 + 3) : "";

        // Unescape \\n back to newlines for display
        auto unescape = [](const std::string& s) {
            std::string r;
            for (size_t j = 0; j < s.size(); j++) {
                if (s[j] == '\\' && j + 1 < s.size() && s[j+1] == 'n') {
                    r += "\r\n          ";
                    j++;
                } else {
                    r += s[j];
                }
            }
            return r;
        };

        display += timestamp + "  [" + mode + "]\r\n";
        display += "  Raw:     " + unescape(raw) + "\r\n";
        if (raw != refined) {
            display += "  Refined: " + unescape(refined) + "\r\n";
        } else {
            display += "  Refined: (unchanged)\r\n";
        }
        display += "\r\n";
    }

    if (display.empty()) display = "No transcription entries yet.";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, display.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wtext(wlen);
    MultiByteToWideChar(CP_UTF8, 0, display.c_str(), -1, wtext.data(), wlen);

    // Register log viewer class (separate from settings to avoid message conflicts)
    WNDCLASSEXW lwc = {};
    lwc.cbSize = sizeof(lwc);
    lwc.lpfnWndProc = LogViewerWndProc;
    lwc.hInstance = hInstance;
    lwc.lpszClassName = LOG_VIEWER_CLASS;
    lwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    lwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&lwc);

    HWND hLog = CreateWindowExW(
        WS_EX_TOPMOST,
        LOG_VIEWER_CLASS,
        L"Processing Log (last 50 entries)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 500,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!hLog) return;

    if (g_logFont) DeleteObject(g_logFont);
    g_logFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    HWND hEdit = CreateWindowExW(0, L"EDIT", wtext.data(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, 684, 462,
        hLog, nullptr, hInstance, nullptr);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_logFont, TRUE);

    // Scroll to bottom
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)display.size(), (LPARAM)display.size());
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);

    ShowWindow(hLog, SW_SHOW);
    SetForegroundWindow(hLog);
}

// Processor state for dialog
static ProcessorCallbacks g_processorCb;
static HWND g_hProcCheck = nullptr;
static HWND g_hProcBtn = nullptr;

// Update state for dialog
static UpdateCallbacks g_updateCb;
static HWND g_hUpdateBtn = nullptr;
static HWND g_hUpdateStatus = nullptr;
static HWND g_hInstallBtn = nullptr;
static HWND g_hChangelogBtn = nullptr;
static std::string g_updateChangelog;
static std::string g_updateVersion;

static const wchar_t* CHANGELOG_VIEWER_CLASS = L"WisperChangelogViewerClass";
static HFONT g_changelogFont = nullptr;

static LRESULT CALLBACK ChangelogViewerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        if (g_changelogFont) { DeleteObject(g_changelogFont); g_changelogFont = nullptr; }
        return 0;
    }
    if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void showChangelogViewer(HINSTANCE hInstance, const std::string& version, const std::string& changelog) {
    std::string display = "Version " + version + " Release Notes\r\n";
    display += std::string(40, '-') + "\r\n\r\n";
    // Convert \n to \r\n for Windows edit control
    for (size_t i = 0; i < changelog.size(); i++) {
        if (changelog[i] == '\n') {
            display += "\r\n";
        } else if (changelog[i] != '\r') {
            display += changelog[i];
        }
    }
    if (display.empty()) display = "No release notes available.";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, display.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wtext(wlen);
    MultiByteToWideChar(CP_UTF8, 0, display.c_str(), -1, wtext.data(), wlen);

    WNDCLASSEXW cwc = {};
    cwc.cbSize = sizeof(cwc);
    cwc.lpfnWndProc = ChangelogViewerWndProc;
    cwc.hInstance = hInstance;
    cwc.lpszClassName = CHANGELOG_VIEWER_CLASS;
    cwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&cwc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        CHANGELOG_VIEWER_CLASS,
        L"What's New",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 550, 400,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!hWnd) return;

    if (g_changelogFont) DeleteObject(g_changelogFont);
    g_changelogFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    HWND hEdit = CreateWindowExW(0, L"EDIT", wtext.data(),
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        0, 0, 534, 362,
        hWnd, nullptr, hInstance, nullptr);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)g_changelogFont, TRUE);

    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_OK) {
                // Read repeat-press mode
                if (SendDlgItemMessageW(hwnd, ID_RADIO_FLASH, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->repeatPressMode = RepeatPressMode::Flash;
                else if (SendDlgItemMessageW(hwnd, ID_RADIO_CANCEL, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->repeatPressMode = RepeatPressMode::Cancel;
                else
                    g_dlgSettings->repeatPressMode = RepeatPressMode::Queue;

                // Read model size
                if (SendDlgItemMessageW(hwnd, ID_RADIO_TINY, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->modelSize = model::ModelSize::Tiny;
                else if (SendDlgItemMessageW(hwnd, ID_RADIO_BASE, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->modelSize = model::ModelSize::Base;
                else if (SendDlgItemMessageW(hwnd, ID_RADIO_MEDIUM, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    g_dlgSettings->modelSize = model::ModelSize::Medium;
                else
                    g_dlgSettings->modelSize = model::ModelSize::Small;

                // Read vocab prompt checkbox
                g_dlgSettings->vocabPromptEnabled =
                    SendDlgItemMessageW(hwnd, ID_CHECK_VOCAB, BM_GETCHECK, 0, 0) == BST_CHECKED;

                // Read processor checkbox
                g_dlgSettings->processorEnabled =
                    SendDlgItemMessageW(hwnd, ID_CHECK_PROCESSOR, BM_GETCHECK, 0, 0) == BST_CHECKED;

                g_dlgResult = true;
                DestroyWindow(hwnd);
            } else if (id == ID_CANCEL) {
                g_dlgResult = false;
                DestroyWindow(hwnd);
            } else if (id == ID_BTN_SHOW_LOG) {
                showLogViewer(g_processorCb.isReady ? (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE) : nullptr);
            } else if (id == ID_BTN_CHECK_UPDATE) {
                if (g_updateCb.requestCheck) {
                    g_updateCb.requestCheck();
                    EnableWindow(g_hUpdateBtn, FALSE);
                    SetWindowTextW(g_hUpdateStatus, L"Checking...");
                }
            } else if (id == ID_BTN_INSTALL_UPDATE) {
                if (g_updateCb.requestInstall) {
                    g_updateCb.requestInstall();
                    EnableWindow(g_hInstallBtn, FALSE);
                    SetWindowTextW(g_hUpdateStatus, L"Downloading update...");
                }
            } else if (id == ID_BTN_VIEW_CHANGELOG) {
                if (!g_updateChangelog.empty()) {
                    showChangelogViewer(
                        (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE),
                        g_updateVersion, g_updateChangelog);
                }
            } else if (id == ID_BTN_PROCESSOR) {
                if (g_processorCb.isReady && g_processorCb.isReady()) {
                    // Remove dependencies
                    if (g_processorCb.requestRemove) g_processorCb.requestRemove();
                    // Uncheck and disable the checkbox
                    SendMessageW(g_hProcCheck, BM_SETCHECK, BST_UNCHECKED, 0);
                    EnableWindow(g_hProcCheck, FALSE);
                    SetWindowTextW(g_hProcBtn, L"Download");
                } else {
                    // Download dependencies (async — dialog stays open)
                    if (g_processorCb.requestDownload) g_processorCb.requestDownload();
                    EnableWindow(g_hProcBtn, FALSE);
                    SetWindowTextW(g_hProcBtn, L"Downloading...");
                }
            }
            return 0;
        }

        case WM_CLOSE:
            g_dlgResult = false;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_dlgHwnd = nullptr;
            g_hProcCheck = nullptr;
            g_hProcBtn = nullptr;
            g_hUpdateBtn = nullptr;
            g_hUpdateStatus = nullptr;
            g_hInstallBtn = nullptr;
            g_hChangelogBtn = nullptr;
            g_dlgClosed = true;
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void notifyProcessorDownloadComplete(bool success) {
    if (g_hProcBtn) {
        EnableWindow(g_hProcBtn, TRUE);
        if (success) {
            SetWindowTextW(g_hProcBtn, L"Remove");
            if (g_hProcCheck) EnableWindow(g_hProcCheck, TRUE);
        } else {
            SetWindowTextW(g_hProcBtn, L"Download");
        }
    }
}

void notifyUpdateCheckComplete(bool available, const char* version, const char* changelog) {
    g_updateChangelog = changelog ? changelog : "";
    g_updateVersion = version ? version : "";

    if (g_hUpdateBtn) EnableWindow(g_hUpdateBtn, TRUE);

    if (g_hUpdateStatus) {
        if (available && version) {
            std::string msg = "Version " + std::string(version) + " available!";
            int wlen = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
            std::vector<wchar_t> wmsg(wlen);
            MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, wmsg.data(), wlen);
            SetWindowTextW(g_hUpdateStatus, wmsg.data());
        } else {
            SetWindowTextW(g_hUpdateStatus, L"You are up to date.");
        }
    }

    if (available) {
        if (g_hInstallBtn) ShowWindow(g_hInstallBtn, SW_SHOW);
        if (g_hChangelogBtn) ShowWindow(g_hChangelogBtn, SW_SHOW);
    } else {
        if (g_hInstallBtn) ShowWindow(g_hInstallBtn, SW_HIDE);
        if (g_hChangelogBtn) ShowWindow(g_hChangelogBtn, SW_HIDE);
    }
}

void notifyUpdateDownloadComplete(bool success) {
    if (g_hInstallBtn) {
        EnableWindow(g_hInstallBtn, TRUE);
        if (success) ShowWindow(g_hInstallBtn, SW_HIDE);
    }
    if (g_hUpdateStatus) {
        SetWindowTextW(g_hUpdateStatus, success
            ? L"Update downloaded! Installing..."
            : L"Download failed.");
    }
}

bool showSettingsDialog(HINSTANCE hInstance, Settings& s, const wchar_t* backendInfo,
                        ProcessorCallbacks processorCb,
                        UpdateCallbacks updateCb) {
    g_processorCb = processorCb;
    g_updateCb = updateCb;
    g_updateChangelog.clear();
    g_updateVersion.clear();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = SETTINGS_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    g_dlgSettings = &s;
    g_dlgResult = false;

    int dlgW = 340, dlgH = 630;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - dlgW) / 2;
    int y = (screenH - dlgH) / 2;

    g_dlgHwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        SETTINGS_CLASS,
        L"Wisper Agent Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_dlgHwnd) return false;

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // --- Repeat press mode group ---
    HWND hGroup1 = CreateWindowExW(0, L"BUTTON", L"When hotkey pressed during transcription:",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 10, 310, 130,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hGroup1, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hFlash = CreateWindowExW(0, L"BUTTON", L"Flash overlay (acknowledge only)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        25, 35, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_FLASH, hInstance, nullptr);
    SendMessageW(hFlash, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hQueue = CreateWindowExW(0, L"BUTTON", L"Queue next recording",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 65, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_QUEUE, hInstance, nullptr);
    SendMessageW(hQueue, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hCancelRb = CreateWindowExW(0, L"BUTTON", L"Cancel and re-record",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 95, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_CANCEL, hInstance, nullptr);
    SendMessageW(hCancelRb, WM_SETFONT, (WPARAM)hFont, TRUE);

    switch (s.repeatPressMode) {
        case RepeatPressMode::Flash:
            SendMessageW(hFlash, BM_SETCHECK, BST_CHECKED, 0); break;
        case RepeatPressMode::Cancel:
            SendMessageW(hCancelRb, BM_SETCHECK, BST_CHECKED, 0); break;
        default:
            SendMessageW(hQueue, BM_SETCHECK, BST_CHECKED, 0); break;
    }

    // --- Model size group ---
    HWND hGroup2 = CreateWindowExW(0, L"BUTTON", L"Transcription model:",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 150, 310, 170,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hGroup2, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hTiny = CreateWindowExW(0, L"BUTTON", L"Tiny (~75MB)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        25, 175, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_TINY, hInstance, nullptr);
    SendMessageW(hTiny, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBase = CreateWindowExW(0, L"BUTTON", L"Base (~150MB)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 200, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_BASE, hInstance, nullptr);
    SendMessageW(hBase, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hSmall = CreateWindowExW(0, L"BUTTON", L"Small (~500MB, recommended)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 225, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_SMALL, hInstance, nullptr);
    SendMessageW(hSmall, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hMedium = CreateWindowExW(0, L"BUTTON", L"Medium (~1.5GB)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        25, 250, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_RADIO_MEDIUM, hInstance, nullptr);
    SendMessageW(hMedium, WM_SETFONT, (WPARAM)hFont, TRUE);

    switch (s.modelSize) {
        case model::ModelSize::Tiny:
            SendMessageW(hTiny, BM_SETCHECK, BST_CHECKED, 0); break;
        case model::ModelSize::Base:
            SendMessageW(hBase, BM_SETCHECK, BST_CHECKED, 0); break;
        case model::ModelSize::Medium:
            SendMessageW(hMedium, BM_SETCHECK, BST_CHECKED, 0); break;
        default:
            SendMessageW(hSmall, BM_SETCHECK, BST_CHECKED, 0); break;
    }

    // Vocab prompt checkbox (inside model group)
    HWND hVocabCheck = CreateWindowExW(0, L"BUTTON",
        L"Programming vocabulary hint",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        25, 280, 280, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_CHECK_VOCAB, hInstance, nullptr);
    SendMessageW(hVocabCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (s.vocabPromptEnabled) {
        SendMessageW(hVocabCheck, BM_SETCHECK, BST_CHECKED, 0);
    }

    // --- AI Processing group ---
    bool procReady = g_processorCb.isReady && g_processorCb.isReady();

    HWND hGroup3 = CreateWindowExW(0, L"BUTTON", L"AI Text Processing:",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 330, 310, 80,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hGroup3, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hProcCheck = CreateWindowExW(0, L"BUTTON",
        L"Enable (~1GB download)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        25, 352, 170, 22,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_CHECK_PROCESSOR, hInstance, nullptr);
    SendMessageW(g_hProcCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    if (s.processorEnabled && procReady) {
        SendMessageW(g_hProcCheck, BM_SETCHECK, BST_CHECKED, 0);
    }
    if (!procReady) {
        EnableWindow(g_hProcCheck, FALSE);
    }

    g_hProcBtn = CreateWindowExW(0, L"BUTTON",
        procReady ? L"Remove" : L"Download",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        220, 350, 90, 26,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_BTN_PROCESSOR, hInstance, nullptr);
    SendMessageW(g_hProcBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hLogBtn = CreateWindowExW(0, L"BUTTON", L"Show Log",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 380, 90, 24,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_BTN_SHOW_LOG, hInstance, nullptr);
    SendMessageW(hLogBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Backend info line
    HWND hInfo = CreateWindowExW(0, L"STATIC", backendInfo,
        WS_CHILD | WS_VISIBLE,
        10, 420, 310, 18,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hInfo, WM_SETFONT, (WPARAM)hFont, TRUE);

    // --- Updates group ---
    HWND hGroup4 = CreateWindowExW(0, L"BUTTON", L"Updates:",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 442, 310, 90,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hGroup4, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Current version label
    std::wstring verLabel = L"Current version: " WISPER_AGENT_VERSION_W;
    HWND hVerLabel = CreateWindowExW(0, L"STATIC", verLabel.c_str(),
        WS_CHILD | WS_VISIBLE,
        25, 462, 280, 18,
        g_dlgHwnd, nullptr, hInstance, nullptr);
    SendMessageW(hVerLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Check for Updates button
    g_hUpdateBtn = CreateWindowExW(0, L"BUTTON", L"Check for Updates",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        25, 486, 130, 26,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_BTN_CHECK_UPDATE, hInstance, nullptr);
    SendMessageW(g_hUpdateBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Update status label
    g_hUpdateStatus = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        160, 490, 155, 18,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_STATIC_UPDATE_STATUS, hInstance, nullptr);
    SendMessageW(g_hUpdateStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

    // View Changes button (hidden until update found)
    g_hChangelogBtn = CreateWindowExW(0, L"BUTTON", L"View Changes",
        WS_CHILD | BS_PUSHBUTTON,
        25, 512, 100, 24,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_BTN_VIEW_CHANGELOG, hInstance, nullptr);
    SendMessageW(g_hChangelogBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Download & Install button (hidden until update found)
    g_hInstallBtn = CreateWindowExW(0, L"BUTTON", L"Download && Install",
        WS_CHILD | BS_PUSHBUTTON,
        180, 512, 130, 24,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_BTN_INSTALL_UPDATE, hInstance, nullptr);
    SendMessageW(g_hInstallBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    // OK / Cancel buttons
    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        150, 520, 80, 28,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_OK, hInstance, nullptr);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        240, 520, 80, 28,
        g_dlgHwnd, (HMENU)(INT_PTR)ID_CANCEL, hInstance, nullptr);
    SendMessageW(hCancelBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_dialogOpen = true;
    ShowWindow(g_dlgHwnd, SW_SHOW);
    SetForegroundWindow(g_dlgHwnd);

    g_dlgClosed = false;
    MSG msg;
    while (!g_dlgClosed) {
        if (!GetMessage(&msg, nullptr, 0, 0)) break;
        if (g_dlgHwnd && IsDialogMessageW(g_dlgHwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_dialogOpen = false;
    g_dlgSettings = nullptr;

    return g_dlgResult;
}

}
