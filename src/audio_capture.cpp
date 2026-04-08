#include "audio_capture.h"
#include <combaseapi.h>

namespace audio {

static IMMDeviceEnumerator* g_enumerator = nullptr;
static IAudioClient* g_audioClient = nullptr;
static IAudioCaptureClient* g_captureClient = nullptr;
static std::thread g_captureThread;
static std::atomic<bool> g_capturing{false};
static std::mutex g_mutex;
static std::vector<float> g_buffer;
static UINT32 g_sampleRate = 0;
static UINT32 g_channels = 0;
static bool g_comInitialized = false;

static void ensureCom() {
    if (!g_comInitialized) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        g_comInitialized = true;
    }
}

static IMMDeviceEnumerator* getEnumerator() {
    if (!g_enumerator) {
        ensureCom();
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                         __uuidof(IMMDeviceEnumerator), (void**)&g_enumerator);
    }
    return g_enumerator;
}

std::vector<tray::AudioDevice> enumerateDevices() {
    std::vector<tray::AudioDevice> result;
    auto* enumerator = getEnumerator();
    if (!enumerator) return result;

    IMMDeviceCollection* collection = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) {
        return result;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = nullptr;
        if (FAILED(collection->Item(i, &device))) continue;

        LPWSTR id = nullptr;
        device->GetId(&id);

        IPropertyStore* props = nullptr;
        device->OpenPropertyStore(STGM_READ, &props);

        PROPVARIANT name;
        PropVariantInit(&name);
        props->GetValue(PKEY_Device_FriendlyName, &name);

        tray::AudioDevice ad;
        ad.id = id ? id : L"";
        ad.name = name.pwszVal ? name.pwszVal : L"Unknown";

        result.push_back(ad);

        PropVariantClear(&name);
        if (props) props->Release();
        if (id) CoTaskMemFree(id);
        device->Release();
    }
    collection->Release();
    return result;
}

static void captureThreadProc() {
    // COM must be initialized per-thread
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Start() blocks ~500ms in shared mode — runs here to avoid blocking UI
    g_audioClient->Start();

    UINT32 bufferFrameCount = 0;
    g_audioClient->GetBufferSize(&bufferFrameCount);

    while (g_capturing) {
        // Wait for audio data (10ms intervals)
        Sleep(10);

        UINT32 packetLength = 0;
        g_captureClient->GetNextPacketSize(&packetLength);

        while (packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            if (FAILED(g_captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr))) {
                break;
            }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && numFrames > 0) {
                float* floatData = reinterpret_cast<float*>(data);
                std::lock_guard<std::mutex> lock(g_mutex);
                g_buffer.insert(g_buffer.end(), floatData, floatData + numFrames * g_channels);
            }

            g_captureClient->ReleaseBuffer(numFrames);
            g_captureClient->GetNextPacketSize(&packetLength);
        }
    }

    CoUninitialize();
}

static bool prepare(const std::wstring& deviceId) {
    // Release previous client if any
    if (g_captureClient) { g_captureClient->Release(); g_captureClient = nullptr; }
    if (g_audioClient) { g_audioClient->Release(); g_audioClient = nullptr; }

    auto* enumerator = getEnumerator();
    if (!enumerator) return false;

    IMMDevice* device = nullptr;
    if (deviceId.empty()) {
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))) {
            return false;
        }
    } else {
        if (FAILED(enumerator->GetDevice(deviceId.c_str(), &device))) {
            return false;
        }
    }

    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_audioClient))) {
        device->Release();
        return false;
    }

    WAVEFORMATEX* mixFormat = nullptr;
    g_audioClient->GetMixFormat(&mixFormat);

    g_sampleRate = mixFormat->nSamplesPerSec;
    g_channels = mixFormat->nChannels;

    if (FAILED(g_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                          10000000, // 1 second buffer
                                          0, mixFormat, nullptr))) {
        CoTaskMemFree(mixFormat);
        g_audioClient->Release();
        g_audioClient = nullptr;
        device->Release();
        return false;
    }

    CoTaskMemFree(mixFormat);

    if (FAILED(g_audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&g_captureClient))) {
        g_audioClient->Release();
        g_audioClient = nullptr;
        device->Release();
        return false;
    }

    device->Release();
    return true;
}

bool startCapture(const std::wstring& deviceId) {
    // Always re-prepare to detect disconnected devices
    if (!prepare(deviceId)) return false;

    // Clear buffer
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_buffer.clear();
    }

    // Start on background thread — IAudioClient::Start() blocks ~500ms in shared mode
    g_capturing = true;
    g_captureThread = std::thread(captureThreadProc);

    return true;
}

CaptureResult stopCapture() {
    CaptureResult result;

    if (g_capturing) {
        g_capturing = false;
        if (g_captureThread.joinable()) {
            g_captureThread.join();
        }
        if (g_audioClient) {
            g_audioClient->Stop();
            g_audioClient->Reset();
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        result.samples = std::move(g_buffer);
        result.sampleRate = g_sampleRate;
        result.channels = g_channels;
        g_buffer.clear();
    }

    return result;
}

void cleanup() {
    if (g_capturing) stopCapture();
    if (g_enumerator) { g_enumerator->Release(); g_enumerator = nullptr; }
    if (g_comInitialized) { CoUninitialize(); g_comInitialized = false; }
}

}
