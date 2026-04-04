#include "transcriber.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <regex>

namespace transcriber {

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
    std::vector<std::regex> patterns = {
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
    cleaned = std::regex_replace(cleaned, std::regex(R"(\s+)"), " ");

    // Trim
    size_t start = cleaned.find_first_not_of(" \t\r\n");
    size_t end = cleaned.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return cleaned.substr(start, end - start + 1);
}

std::string transcribe(const std::wstring& wavPath, const std::wstring& whisperExe, const std::wstring& modelPath) {
    // Build command line
    std::wstring cmdLine = L"\"" + whisperExe + L"\" -m \"" + modelPath + L"\" -f \"" + wavPath + L"\" --no-timestamps -l auto --no-prints";

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
    si.hStdError = stdoutWrite;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return "";
    }

    CloseHandle(stdoutWrite); // Close write end in parent

    std::string output = readPipe(stdoutRead);
    CloseHandle(stdoutRead);

    DWORD waitResult = WaitForSingleObject(pi.hProcess, 30000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) return "";

    return cleanOutput(output);
}

}
