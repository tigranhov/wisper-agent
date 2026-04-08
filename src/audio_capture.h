#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include "tray.h"

namespace audio {

struct CaptureResult {
    std::vector<float> samples;
    UINT32 sampleRate;
    UINT32 channels;
};

// Enumerate available audio input devices
std::vector<tray::AudioDevice> enumerateDevices();

// Start capturing audio from the given device (prepares the device automatically)
bool startCapture(const std::wstring& deviceId = L"");

// Stop capturing and return the accumulated PCM samples
CaptureResult stopCapture();

// Cleanup COM resources
void cleanup();

}
