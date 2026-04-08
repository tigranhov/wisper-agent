#include "transcriber.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <regex>
#include <mutex>

namespace transcriber {

static HANDLE g_currentProcess = nullptr;
static std::mutex g_processMutex;
static std::atomic<bool> g_cancelRequested{false};
static HANDLE g_hNul = INVALID_HANDLE_VALUE;

bool isCancelRequested() { return g_cancelRequested; }
void resetCancelFlag() { g_cancelRequested = false; }

static HANDLE getNulHandle() {
    if (g_hNul == INVALID_HANDLE_VALUE) {
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        g_hNul = CreateFileW(L"NUL", GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, nullptr);
    }
    return g_hNul;
}

static std::string readPipe(HANDLE pipe) {
    std::string output;
    char buffer[4096];
    DWORD bytesRead;
    while (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    return output;
}

static std::string cleanOutput(const std::string& text) {
    std::string cleaned = text;

    // Remove common whisper artifacts
    static const std::vector<std::regex> patterns = {
        std::regex(R"(\[BLANK_AUDIO\])", std::regex::icase),
        std::regex(R"(\(blank audio\))", std::regex::icase),
        std::regex(R"(\[silence\])", std::regex::icase),
        std::regex(R"(\[inaudible\])", std::regex::icase),
        std::regex(R"(\[music\])", std::regex::icase),
        std::regex(R"(\[Music\])"),
    };

    for (auto& pattern : patterns) {
        cleaned = std::regex_replace(cleaned, pattern, "");
    }

    // Collapse whitespace
    static const std::regex whitespace(R"(\s+)");
    cleaned = std::regex_replace(cleaned, whitespace, " ");

    // Trim
    size_t start = cleaned.find_first_not_of(" \t\r\n");
    size_t end = cleaned.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return cleaned.substr(start, end - start + 1);
}

TranscribeResult transcribe(const std::wstring& wavPath, const std::wstring& whisperExe, const std::wstring& modelPath, bool useVocabPrompt) {
    // Build command line
    std::wstring cmdLine = L"\"" + whisperExe + L"\" -m \"" + modelPath + L"\" -f \"" + wavPath + L"\" --no-timestamps -l auto --no-prints";

    if (useVocabPrompt) {
        cmdLine +=
            L" --prompt \"JavaScript TypeScript Python C++ C# Rust Go Java Kotlin Swift"
            L" React Angular Vue Node.js Django Flask"
            L" API REST GraphQL HTTP HTTPS JSON XML HTML CSS SCSS WebSocket OAuth JWT endpoint middleware webhook"
            L" Docker Kubernetes AWS Azure GCP GitHub GitLab CI/CD pipeline deploy Nginx Redis PostgreSQL MongoDB MySQL"
            L" function variable boolean integer string array object null undefined async await promise callback"
            L" interface enum class struct commit merge branch pull request"
            L" refactor debug compile runtime linter formatter ESLint component module dependency import\"";
    }

    // Create pipes for stdout
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.hStdOutput = stdoutWrite;
    si.hStdError = getNulHandle(); // Discard stderr (CUDA/ggml diagnostic output)
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return {"", false};
    }

    CloseHandle(stdoutWrite); // Close write end in parent

    {
        std::lock_guard<std::mutex> lock(g_processMutex);
        g_currentProcess = pi.hProcess;
    }

    std::string output = readPipe(stdoutRead);
    CloseHandle(stdoutRead);

    DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    {
        std::lock_guard<std::mutex> lock(g_processMutex);
        g_currentProcess = nullptr;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) return {"", false};

    return {cleanOutput(output), true};
}

void cancelCurrent() {
    g_cancelRequested = true;
    std::lock_guard<std::mutex> lock(g_processMutex);
    if (g_currentProcess) {
        TerminateProcess(g_currentProcess, 1);
    }
}

}
