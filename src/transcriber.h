#pragma once
#include <string>

namespace transcriber {

struct TranscribeResult {
    std::string text;
    bool processOk; // true if whisper-cli ran successfully (exit code 0)
};

// Transcribe a WAV file using whisper-cli.exe.
TranscribeResult transcribe(const std::wstring& wavPath, const std::wstring& whisperExe, const std::wstring& modelPath, const std::string& language = "en", bool useVocabPrompt = false);

// Kill the running whisper-cli process (if any)
void cancelCurrent();

bool isCancelRequested();
void resetCancelFlag();

}
