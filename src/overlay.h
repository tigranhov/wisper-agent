#pragma once
#include <windows.h>

namespace overlay {

enum class State {
    Idle,
    Initializing,
    Recording,
    Transcribing,
    Downloading,
    Error,
};

void create(HINSTANCE hInstance);
void destroy();
void setState(State state, int percent = 0);
void flash();

}
