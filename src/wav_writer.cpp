#include "wav_writer.h"
#include <windows.h>
#include <fstream>
#include <cmath>
#include <cstdio>

namespace wav {

static constexpr uint32_t TARGET_SAMPLE_RATE = 16000;

// Downmix to mono by averaging channels
static std::vector<float> toMono(const std::vector<float>& samples, uint32_t channels) {
    if (channels == 1) return samples;
    size_t frameCount = samples.size() / channels;
    std::vector<float> mono(frameCount);
    for (size_t i = 0; i < frameCount; i++) {
        float sum = 0.0f;
        for (uint32_t ch = 0; ch < channels; ch++) {
            sum += samples[i * channels + ch];
        }
        mono[i] = sum / (float)channels;
    }
    return mono;
}

// Simple linear interpolation resampling
static std::vector<float> resample(const std::vector<float>& input, uint32_t srcRate, uint32_t dstRate) {
    if (srcRate == dstRate) return input;
    double ratio = (double)srcRate / (double)dstRate;
    size_t outLen = (size_t)((double)input.size() / ratio);
    std::vector<float> output(outLen);
    for (size_t i = 0; i < outLen; i++) {
        double srcIdx = (double)i * ratio;
        size_t idx0 = (size_t)srcIdx;
        size_t idx1 = idx0 + 1;
        if (idx1 >= input.size()) idx1 = input.size() - 1;
        double frac = srcIdx - (double)idx0;
        output[i] = (float)((1.0 - frac) * input[idx0] + frac * input[idx1]);
    }
    return output;
}

std::wstring writeTemp(const std::vector<float>& samples, uint32_t srcSampleRate, uint32_t srcChannels) {
    if (samples.empty()) return L"";

    // Downmix to mono
    auto mono = toMono(samples, srcChannels);

    // Resample to 16kHz
    auto resampled = resample(mono, srcSampleRate, TARGET_SAMPLE_RATE);

    // Convert float to int16
    std::vector<int16_t> pcm16(resampled.size());
    for (size_t i = 0; i < resampled.size(); i++) {
        float s = resampled[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        pcm16[i] = (int16_t)(s < 0 ? s * 32768.0f : s * 32767.0f);
    }

    // Generate temp file path
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempFile[MAX_PATH];
    swprintf_s(tempFile, L"%sspeakinto-%llu.wav", tempDir, (unsigned long long)GetTickCount64());

    // Write WAV file
    std::ofstream out(tempFile, std::ios::binary);
    if (!out.is_open()) return L"";

    uint32_t dataSize = (uint32_t)(pcm16.size() * sizeof(int16_t));
    uint32_t fileSize = 36 + dataSize;
    uint16_t numChannels = 1;
    uint32_t sampleRate = TARGET_SAMPLE_RATE;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    uint16_t blockAlign = numChannels * bitsPerSample / 8;

    // RIFF header
    out.write("RIFF", 4);
    out.write((char*)&fileSize, 4);
    out.write("WAVE", 4);

    // fmt chunk
    out.write("fmt ", 4);
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM
    out.write((char*)&fmtSize, 4);
    out.write((char*)&audioFormat, 2);
    out.write((char*)&numChannels, 2);
    out.write((char*)&sampleRate, 4);
    out.write((char*)&byteRate, 4);
    out.write((char*)&blockAlign, 2);
    out.write((char*)&bitsPerSample, 2);

    // data chunk
    out.write("data", 4);
    out.write((char*)&dataSize, 4);
    out.write((char*)pcm16.data(), dataSize);

    out.close();
    return tempFile;
}

}
