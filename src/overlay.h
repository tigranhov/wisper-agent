#pragma once
#include <windows.h>

namespace overlay {

enum class State {
    Idle,
    Initializing,
    Recording,
    Transcribing,
};

void create(HINSTANCE hInstance);
void destroy();
void setState(State state);
void flash();

}
