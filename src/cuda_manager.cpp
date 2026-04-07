#include "cuda_manager.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <fstream>
#include <cstdio>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace cuda {

// cuBLAS DLLs needed for GPU acceleration
static const wchar_t* CUBLAS_DLL = L"cublas64_12.dll";
static const wchar_t* CUBLASLT_DLL = L"cublasLt64_12.dll";
static constexpr size_t CUBLAS_MIN_SIZE = 50 * 1024 * 1024;     // 50MB minimum
static constexpr size_t CUBLASLT_MIN_SIZE = 200 * 1024 * 1024;  // 200MB minimum

// Download source: whisper.cpp CUDA release (contains cuBLAS DLLs)
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

static std::wstring getCuBlasDir() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        return L"";
    }
    return std::wstring(appdata) + L"\\wisper-agent\\cuda";
}

static std::wstring getAppDir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    return dir.substr(0, dir.find_last_of(L'\\') + 1);
}

static bool fileExistsWithMinSize(const std::wstring& path, size_t minSize) {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return false;
    }
    ULONGLONG fileSize = ((ULONGLONG)fileInfo.nFileSizeHigh << 32) | fileInfo.nFileSizeLow;
    return fileSize >= minSize;
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

// Check a directory for both cuBLAS DLLs
static bool hasCuBlasIn(const std::wstring& dir) {
    return fileExistsWithMinSize(dir + CUBLAS_DLL, CUBLAS_MIN_SIZE) &&
           fileExistsWithMinSize(dir + CUBLASLT_DLL, CUBLASLT_MIN_SIZE);
}

bool isCuBlasAvailable() {
    // Check app directory first
    auto appDir = getAppDir();
    if (hasCuBlasIn(appDir)) return true;

    // Check %APPDATA% cuda directory
    auto cublasDir = getCuBlasDir();
    if (!cublasDir.empty() && hasCuBlasIn(cublasDir + L"\\")) return true;

    return false;
}

void addCuBlasToPath() {
    // Determine which directory has cuBLAS
    std::wstring cublasDir;

    auto appDir = getAppDir();
    if (hasCuBlasIn(appDir)) {
        cublasDir = appDir;
    } else {
        cublasDir = getCuBlasDir();
        if (cublasDir.empty() || !hasCuBlasIn(cublasDir + L"\\")) return;
    }

    // Prepend to PATH
    DWORD pathLen = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    std::vector<wchar_t> oldPath(pathLen);
    GetEnvironmentVariableW(L"PATH", oldPath.data(), pathLen);

    std::wstring newPath = cublasDir + L";" + oldPath.data();
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    log("Added cuBLAS directory to PATH: %ls", cublasDir.c_str());
}

// --- HTTP download (same pattern as model_manager/processor_llm) ---

static bool downloadFile(const wchar_t* host, const wchar_t* urlPath,
                          const std::wstring& destPath,
                          std::function<void(int percent)> onProgress) {
    HINTERNET hSession = WinHttpOpen(L"WisperAgent/1.0",
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

    // Follow redirects
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

    // Get content length
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

bool ensureCuBlas(std::function<void(int percent)> onProgress) {
    if (isCuBlasAvailable()) return true;

    auto cublasDir = getCuBlasDir();
    if (cublasDir.empty()) return false;
    if (!ensureDirectory(cublasDir)) return false;

    log("Downloading cuBLAS DLLs for GPU acceleration...");

    // Download the whisper.cpp CUDA release zip
    auto zipPath = cublasDir + L"\\cublas-download.zip";
    bool ok = downloadFile(DOWNLOAD_HOST, DOWNLOAD_PATH, zipPath, onProgress);
    if (!ok) {
        log("Failed to download cuBLAS package");
        return false;
    }

    // Extract only the cuBLAS DLLs using PowerShell
    std::wstring cmd = L"powershell -NoProfile -Command \"";
    cmd += L"Add-Type -AssemblyName System.IO.Compression.FileSystem; ";
    cmd += L"$zip = [System.IO.Compression.ZipFile]::OpenRead('";
    cmd += zipPath;
    cmd += L"'); ";
    cmd += L"foreach ($entry in $zip.Entries) { ";
    cmd += L"if ($entry.Name -eq 'cublas64_12.dll' -or $entry.Name -eq 'cublasLt64_12.dll') { ";
    cmd += L"$dest = Join-Path '";
    cmd += cublasDir;
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
        WaitForSingleObject(pi.hProcess, 120000); // 2 min timeout for extraction
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        extracted = (exitCode == 0);
    }

    // Clean up zip
    DeleteFileW(zipPath.c_str());

    if (!extracted || !isCuBlasAvailable()) {
        log("Failed to extract cuBLAS DLLs");
        return false;
    }

    log("cuBLAS DLLs installed successfully");
    return true;
}

}
