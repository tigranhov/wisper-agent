#include <windows.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include "keyboard_hook.h"
#include "tray.h"
#include "audio_capture.h"
#include "wav_writer.h"
#include "transcriber.h"
#include "text_injector.h"
#include "model_manager.h"
#include "overlay.h"

// App state
enum class AppState { Idle, Recording, Transcribing };
static AppState g_state = AppState::Idle;
static HWND g_hwnd = nullptr;
static std::chrono::steady_clock::time_point g_recordStartTime;
static std::vector<tray::AudioDevice> g_devices;
static int g_selectedDeviceIndex = -1; // -1 = default
static constexpr int MIN_RECORDING_MS = 500;

// Custom messages for async operations
constexpr UINT WM_TRANSCRIPTION_DONE = WM_APP + 20;

// Paths
static std::wstring g_whisperExe;
static std::wstring g_modelPath;

static void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    // Also write to stderr for console debugging
    fprintf(stderr, "%s\n", buf);
}

static void refreshDevices() {
    g_devices = audio::enumerateDevices();
    log("Found %d audio devices", (int)g_devices.size());
    for (size_t i = 0; i < g_devices.size(); i++) {
        log("  [%d] %ls", (int)i, g_devices[i].name.c_str());
    }
}

static void findWhisperPaths() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    dir = dir.substr(0, dir.find_last_of(L'\\') + 1);

    // Try release layout: whisper-cli.exe next to wisper-agent.exe
    g_whisperExe = dir + L"whisper-cli.exe";
    if (GetFileAttributesW(g_whisperExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Try dev layout: build/Release/wisper-agent.exe → ../../bin/Release/whisper-cli.exe
        g_whisperExe = dir + L"..\\..\\bin\\Release\\whisper-cli.exe";
    }

    // Model path from model manager
    g_modelPath = model::getModelPath();

    log("Whisper exe: %ls", g_whisperExe.c_str());
    log("Model path: %ls", g_modelPath.c_str());
}

static void onComboDown() {
    if (g_state != AppState::Idle) return;

    std::wstring deviceId = L"";
    if (g_selectedDeviceIndex >= 0 && g_selectedDeviceIndex < (int)g_devices.size()) {
        deviceId = g_devices[g_selectedDeviceIndex].id;
    }

    if (!audio::startCapture(deviceId)) {
        log("Failed to start audio capture");
        tray::showBalloon(L"Wisper Agent", L"Failed to start microphone capture");
        return;
    }

    g_state = AppState::Recording;
    g_recordStartTime = std::chrono::steady_clock::now();
    tray::setState(tray::State::Recording);
    overlay::setState(overlay::State::Recording);
    log("Recording started");
}

static void onComboUp() {
    if (g_state != AppState::Recording) return;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_recordStartTime).count();

    auto result = audio::stopCapture();

    if (duration < MIN_RECORDING_MS) {
        log("Recording discarded (%lldms < %dms)", duration, MIN_RECORDING_MS);
        g_state = AppState::Idle;
        tray::setState(tray::State::Idle);
        overlay::setState(overlay::State::Idle);
        return;
    }

    log("Recording stopped: %zu samples, %uHz, %uch, %lldms",
        result.samples.size(), result.sampleRate, result.channels, duration);

    if (result.samples.empty()) {
        log("No audio data captured");
        g_state = AppState::Idle;
        tray::setState(tray::State::Idle);
        overlay::setState(overlay::State::Idle);
        return;
    }

    g_state = AppState::Transcribing;
    tray::setState(tray::State::Transcribing);
    overlay::setState(overlay::State::Transcribing);

    // Run transcription on background thread
    HWND hwnd = g_hwnd;
    std::thread([result = std::move(result), hwnd]() {
        // Write WAV
        auto wavPath = wav::writeTemp(result.samples, result.sampleRate, result.channels);
        if (wavPath.empty()) {
            log("Failed to write WAV file");
            PostMessage(hwnd, WM_TRANSCRIPTION_DONE, 0, 0);
            return;
        }

        log("WAV written: %ls", wavPath.c_str());

        // Transcribe
        std::string text = transcriber::transcribe(wavPath, g_whisperExe, g_modelPath);

        // Delete temp file
        DeleteFileW(wavPath.c_str());

        if (text.empty()) {
            log("Empty transcription");
            PostMessage(hwnd, WM_TRANSCRIPTION_DONE, 0, 0);
            return;
        }

        log("Transcribed: %s", text.c_str());

        // Inject text (must happen on this thread since SendInput is thread-agnostic)
        injector::injectText(text);

        // Return to Idle immediately so user can record again
        PostMessage(hwnd, WM_TRANSCRIPTION_DONE, 0, 0);

        // Restore clipboard after target app has read it
        injector::restoreClipboard();
    }).detach();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case keyboard::WM_COMBO_DOWN:
            onComboDown();
            return 0;

        case keyboard::WM_COMBO_UP:
            onComboUp();
            return 0;

        case WM_TRANSCRIPTION_DONE:
            g_state = AppState::Idle;
            tray::setState(tray::State::Idle);
            overlay::setState(overlay::State::Idle);
            return 0;

        case tray::WM_TRAY_ICON:
            if (lParam == WM_RBUTTONUP) {
                tray::showContextMenu(hwnd, g_devices, g_selectedDeviceIndex);
            }
            return 0;

        case WM_COMMAND: {
            UINT id = LOWORD(wParam);
            if (id == tray::IDM_QUIT) {
                PostQuitMessage(0);
            } else if (id >= tray::IDM_MIC_BASE && id < tray::IDM_MIC_BASE + 100) {
                g_selectedDeviceIndex = id - tray::IDM_MIC_BASE;
                log("Selected mic: %ls", g_devices[g_selectedDeviceIndex].name.c_str());
                tray::showBalloon(L"Microphone", g_devices[g_selectedDeviceIndex].name.c_str());
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single instance check
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"WisperAgentMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"WisperAgentClass";
    RegisterClassExW(&wc);

    // Create hidden message-only window
    g_hwnd = CreateWindowExW(0, L"WisperAgentClass", L"Wisper Agent",
                              0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    findWhisperPaths();
    refreshDevices();

    tray::create(g_hwnd);
    overlay::create(hInstance);

    // Check and download model if needed
    if (!model::modelExists()) {
        log("Model not found, downloading...");
        tray::showBalloon(L"Wisper Agent", L"Downloading speech model (~150MB)...");
        bool downloaded = model::downloadModel([](int percent) {
            if (percent % 10 == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Model download: %d%%", percent);
                OutputDebugStringA(buf);
                OutputDebugStringA("\n");
                fprintf(stderr, "%s\n", buf);
            }
        });
        if (downloaded) {
            tray::showBalloon(L"Wisper Agent", L"Model downloaded successfully.");
            g_modelPath = model::getModelPath();
        } else {
            tray::showBalloon(L"Wisper Agent", L"Failed to download model. Transcription won't work.");
        }
    }

    keyboard::start(g_hwnd);

    log("Wisper Agent is running. Hold Ctrl+Win to record.");

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    keyboard::stop();
    overlay::destroy();
    tray::destroy();
    audio::cleanup();
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);

    return 0;
}
