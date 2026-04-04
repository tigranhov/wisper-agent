#pragma once
#include <windows.h>
#include <string>

namespace settings {

enum class RepeatPressMode { Queue, Flash, Cancel };

struct Settings {
    RepeatPressMode repeatPressMode = RepeatPressMode::Queue;
    int selectedMicIndex = -1;
};

Settings load();
void save(const Settings& s);
bool isDialogOpen();

// Show modal settings dialog. Returns true if user clicked OK.
// Updates `s` in-place with new values.
bool showSettingsDialog(HINSTANCE hInstance, Settings& s);

}
