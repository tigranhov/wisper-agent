#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include "model_manager.h"

namespace settings {

enum class RepeatPressMode { Queue, Flash, Cancel };

struct Settings {
    RepeatPressMode repeatPressMode = RepeatPressMode::Queue;
    int selectedMicIndex = -1;
    model::ModelSize modelSize = model::ModelSize::Small;
    bool processorEnabled = false;
    bool vocabPromptEnabled = false;
    std::string language = "en";  // whisper language code, or "auto"
};

// Callbacks for processor dependency management (provided by main.cpp)
struct ProcessorCallbacks {
    std::function<bool()> isReady;                // check if deps exist
    std::function<void()> requestDownload;        // trigger async download
    std::function<void()> requestRemove;          // delete deps
};

// Callbacks for update operations (provided by main.cpp)
struct UpdateCallbacks {
    std::function<void()> requestCheck;           // trigger async update check
    std::function<void()> requestInstall;         // download + launch installer
};

Settings load();
void save(const Settings& s);
bool isDialogOpen();
void closeDialog();

// Called from main.cpp when async processor download completes
void notifyProcessorDownloadComplete(bool success);

// Called from main.cpp when async update check completes
void notifyUpdateCheckComplete(bool available, const char* version, const char* changelog);

// Called from main.cpp when async installer download completes
void notifyUpdateDownloadComplete(bool success);

bool showSettingsDialog(HINSTANCE hInstance, Settings& s,
                        const wchar_t* backendInfo = L"",
                        ProcessorCallbacks processorCb = {},
                        UpdateCallbacks updateCb = {});

}
