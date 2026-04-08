#include "cuda_manager.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <fstream>
#include <cstdio>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace cuda {

// whisper.cpp CUDA release — contains whisper-cli.exe, all DLLs including cuBLAS
static const wchar_t* DOWNLOAD_HOST = L"github.com";
static const wchar_t* DOWNLOAD_PATH = L"/ggml-org/whisper.cpp/releases/download/v1.8.4/whisper-cublas-12.4.0-bin-x64.zip";

static void log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    fprintf(stderr, "%s\n", buf);
}

static std::wstring getCudaDir() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        return L"";
    }
    return std::wstring(appdata) + L"\\speakinto\\cuda";
}

static bool ensureDirectory(const std::wstring& dir) {
    DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return true;
    size_t pos = dir.find_last_of(L'\\');
    if (pos != std::wstring::npos) {
        ensureDirectory(dir.substr(0, pos));
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool fileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring getWhisperExePath() {
    auto dir = getCudaDir();
    if (dir.empty()) return L"";
    return dir + L"\\whisper-cli.exe";
}

bool isReady() {
    auto dir = getCudaDir();
    if (dir.empty()) return false;
    // Check for essential files
    return fileExists(dir + L"\\whisper-cli.exe") &&
           fileExists(dir + L"\\whisper.dll") &&
           fileExists(dir + L"\\ggml-cuda.dll") &&
           fileExists(dir + L"\\cublas64_12.dll");
}

// --- HTTP download ---

static bool downloadFile(const wchar_t* host, const wchar_t* urlPath,
                          const std::wstring& destPath,
                          std::function<void(int percent)> onProgress) {
    HINTERNET hSession = WinHttpOpen(L"SpeakInto/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host,
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD maxRedirects = 10;
    for (DWORD redirect = 0; redirect < maxRedirects; redirect++) {
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(hRequest, nullptr)) {
            break;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                            WINHTTP_NO_HEADER_INDEX);

        if (statusCode == 200) break;

        if (statusCode == 301 || statusCode == 302 || statusCode == 307) {
            wchar_t newUrl[2048] = {};
            DWORD urlSize = sizeof(newUrl);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                                WINHTTP_HEADER_NAME_BY_INDEX, newUrl, &urlSize,
                                WINHTTP_NO_HEADER_INDEX);

            URL_COMPONENTS urlComp = {};
            urlComp.dwStructSize = sizeof(urlComp);
            wchar_t hostBuf[256] = {};
            wchar_t pathBuf[2048] = {};
            urlComp.lpszHostName = hostBuf;
            urlComp.dwHostNameLength = 256;
            urlComp.lpszUrlPath = pathBuf;
            urlComp.dwUrlPathLength = 2048;

            if (!WinHttpCrackUrl(newUrl, 0, 0, &urlComp)) break;

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);

            hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
            if (!hConnect) break;

            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", pathBuf,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!hRequest) break;
            continue;
        }
        break;
    }

    ULONGLONG contentLength = 0;
    wchar_t clStr[64] = {};
    DWORD clStrSize = sizeof(clStr);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, clStr, &clStrSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        contentLength = _wcstoui64(clStr, nullptr, 10);
    }

    std::wstring tempPath = destPath + L".tmp";
    std::ofstream outFile(tempPath, std::ios::binary);
    if (!outFile.is_open()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    ULONGLONG totalRead = 0;
    int lastPercent = -1;
    char buffer[65536];
    DWORD bytesRead = 0;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
        totalRead += bytesRead;

        if (contentLength > 0 && onProgress) {
            int percent = (int)(totalRead * 100 / contentLength);
            if (percent != lastPercent) {
                lastPercent = percent;
                onProgress(percent);
            }
        }
    }

    outFile.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    DeleteFileW(destPath.c_str());
    if (!MoveFileW(tempPath.c_str(), destPath.c_str())) {
        DeleteFileW(tempPath.c_str());
        return false;
    }

    return true;
}

bool ensureSetup(std::function<void(int percent)> onProgress) {
    if (isReady()) return true;

    auto cudaDir = getCudaDir();
    if (cudaDir.empty()) return false;
    if (!ensureDirectory(cudaDir)) return false;

    log("Downloading CUDA whisper setup for GPU acceleration...");

    auto zipPath = cudaDir + L"\\whisper-cuda-download.zip";
    bool ok = downloadFile(DOWNLOAD_HOST, DOWNLOAD_PATH, zipPath, onProgress);
    if (!ok) {
        log("Failed to download CUDA package");
        return false;
    }

    // Extract all files using PowerShell
    std::wstring cmd = L"powershell -NoProfile -Command \"";
    cmd += L"Add-Type -AssemblyName System.IO.Compression.FileSystem; ";
    cmd += L"$zip = [System.IO.Compression.ZipFile]::OpenRead('";
    cmd += zipPath;
    cmd += L"'); ";
    cmd += L"foreach ($entry in $zip.Entries) { ";
    cmd += L"if ($entry.Name -ne '' -and ($entry.Name -like '*.exe' -or $entry.Name -like '*.dll')) { ";
    cmd += L"$dest = Join-Path '";
    cmd += cudaDir;
    cmd += L"' $entry.Name; ";
    cmd += L"[System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true); ";
    cmd += L"} } $zip.Dispose()\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    bool extracted = false;
    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 300000); // 5 min timeout
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        extracted = (exitCode == 0);
    }

    DeleteFileW(zipPath.c_str());

    if (!extracted || !isReady()) {
        log("Failed to extract CUDA setup");
        return false;
    }

    log("CUDA whisper setup installed successfully");
    return true;
}

}
