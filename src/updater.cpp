#include "updater.h"
#include "version.h"
#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <cstdio>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace updater {

// --- Logging ---

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

// --- Version comparison ---

static bool isNewerVersion(const std::string& current, const std::string& latest) {
    auto split = [](const std::string& s) {
        std::vector<int> parts;
        size_t start = 0;
        while (start < s.size()) {
            auto dot = s.find('.', start);
            if (dot == std::string::npos) dot = s.size();
            try { parts.push_back(std::stoi(s.substr(start, dot - start))); }
            catch (...) { parts.push_back(0); }
            start = dot + 1;
        }
        return parts;
    };

    auto cur = split(current);
    auto lat = split(latest);

    size_t len = cur.size() > lat.size() ? cur.size() : lat.size();
    for (size_t i = 0; i < len; i++) {
        int c = i < cur.size() ? cur[i] : 0;
        int l = i < lat.size() ? lat[i] : 0;
        if (l > c) return true;
        if (l < c) return false;
    }
    return false;
}

// --- Minimal JSON helpers ---

static std::string findValue(const std::string& json, const std::string& key, size_t startPos = 0) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle, startPos);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;

    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // Find matching close quote, handling escaped quotes
        std::string result;
        pos++; // skip opening quote
        while (pos < json.size()) {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                char next = json[pos + 1];
                if (next == '"') { result += '"'; pos += 2; }
                else if (next == 'n') { result += '\n'; pos += 2; }
                else if (next == 'r') { result += '\r'; pos += 2; }
                else if (next == 't') { result += '\t'; pos += 2; }
                else if (next == '\\') { result += '\\'; pos += 2; }
                else { result += json[pos]; pos++; }
            } else if (json[pos] == '"') {
                break;
            } else {
                result += json[pos];
                pos++;
            }
        }
        return result;
    } else {
        auto end = json.find_first_of(",}\n\r ", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    }
}

// Find browser_download_url matching a variant substring (e.g. "nvidia" or "universal")
static std::string findAssetUrl(const std::string& json, const std::string& variantMatch) {
    std::string fallback;
    std::string needle = "\"browser_download_url\"";
    size_t pos = 0;

    while (true) {
        pos = json.find(needle, pos);
        if (pos == std::string::npos) break;

        auto url = findValue(json, "browser_download_url", pos);
        pos += needle.size();

        if (!url.empty() && url.find(variantMatch) != std::string::npos) {
            // Prefer the .exe installer over .zip
            if (url.find(".exe") != std::string::npos || url.find("setup") != std::string::npos) {
                return url;
            }
            if (fallback.empty()) fallback = url;
        }
    }

    return fallback;
}

// --- Variant detection ---

static std::string detectVariant() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    dir = dir.substr(0, dir.find_last_of(L'\\') + 1);

    std::wstring cudaPath = dir + L"whisper-cli-cuda.exe";
    if (GetFileAttributesW(cudaPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return "nvidia";
    }
    return "universal";
}

// --- HTTP GET (returns response body as string) ---

static std::string httpGet(const wchar_t* host, const wchar_t* path, std::string& error) {
    HINTERNET hSession = WinHttpOpen(L"SpeakInto/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "Failed to initialize HTTP";
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host,
                                         INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        error = "Failed to connect to server";
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        error = "Failed to create request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Follow redirects
    DWORD maxRedirects = 5;
    DWORD statusCode = 0;
    for (DWORD redirect = 0; redirect < maxRedirects; redirect++) {
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            error = "Failed to send request";
            break;
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            error = "Failed to receive response";
            break;
        }

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

        error = "HTTP status " + std::to_string(statusCode);
        break;
    }

    std::string body;
    if (statusCode == 200) {
        char buffer[8192];
        DWORD bytesRead = 0;
        while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            body.append(buffer, bytesRead);
        }
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return body;
}

// --- Public API ---

UpdateInfo checkForUpdates() {
    UpdateInfo info;

    log("Checking for updates...");

    std::string error;
    auto json = httpGet(L"api.github.com",
                        L"/repos/tigranhov/speakinto/releases/latest",
                        error);

    if (json.empty()) {
        info.error = error.empty() ? "No response from GitHub" : error;
        log("Update check failed: %s", info.error.c_str());
        return info;
    }

    // Parse tag_name (strip leading 'v' if present)
    auto tagName = findValue(json, "tag_name");
    if (tagName.empty()) {
        info.error = "Could not parse release info";
        log("Update check failed: no tag_name in response");
        return info;
    }

    std::string version = tagName;
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) {
        version = version.substr(1);
    }

    info.latestVersion = version;
    info.changelog = findValue(json, "body");
    info.htmlUrl = findValue(json, "html_url");

    // Find the correct download URL for our variant
    auto variant = detectVariant();
    info.downloadUrl = findAssetUrl(json, variant);

    log("Latest version: %s (current: %s, variant: %s)",
        version.c_str(), SPEAKINTO_VERSION, variant.c_str());

    if (isNewerVersion(SPEAKINTO_VERSION, version)) {
        info.available = true;
        log("Update available: %s -> %s", SPEAKINTO_VERSION, version.c_str());
    } else {
        log("Already up to date");
    }

    return info;
}

std::wstring downloadInstaller(const std::string& url,
                               std::function<void(int percent)> onProgress) {
    if (url.empty()) return L"";

    log("Downloading installer: %s", url.c_str());

    // Parse URL into host and path
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wurl(wlen);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);

    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[2048] = {};
    urlComp.lpszHostName = hostBuf;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = pathBuf;
    urlComp.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(wurl.data(), 0, 0, &urlComp)) {
        log("Failed to parse download URL");
        return L"";
    }

    // Determine output path in %TEMP%
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring tempPath = std::wstring(tempDir) + L"speakinto-update.exe";
    std::wstring downloadPath = tempPath + L".tmp";

    HINTERNET hSession = WinHttpOpen(L"SpeakInto/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log("WinHttpOpen failed: %lu", GetLastError());
        return L"";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostBuf, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return L"";
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", pathBuf,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"";
    }

    // Follow redirects (GitHub redirects to CDN)
    DWORD maxRedirects = 10;
    DWORD statusCode = 0;
    for (DWORD redirect = 0; redirect < maxRedirects; redirect++) {
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) break;
        if (!WinHttpReceiveResponse(hRequest, nullptr)) break;

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

            URL_COMPONENTS nc = {};
            nc.dwStructSize = sizeof(nc);
            wchar_t nh[256] = {};
            wchar_t np[2048] = {};
            nc.lpszHostName = nh;
            nc.dwHostNameLength = 256;
            nc.lpszUrlPath = np;
            nc.dwUrlPathLength = 2048;

            if (!WinHttpCrackUrl(newUrl, 0, 0, &nc)) break;

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);

            hConnect = WinHttpConnect(hSession, nh, nc.nPort, 0);
            if (!hConnect) break;

            DWORD f = (nc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            hRequest = WinHttpOpenRequest(hConnect, L"GET", np,
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, f);
            if (!hRequest) break;
            continue;
        }

        log("Download failed with HTTP status %lu", statusCode);
        break;
    }

    if (statusCode != 200) {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"";
    }

    // Get content length for progress
    DWORD contentLength = 0;
    DWORD clSize = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &clSize,
                        WINHTTP_NO_HEADER_INDEX);

    std::ofstream outFile(downloadPath, std::ios::binary);
    if (!outFile.is_open()) {
        log("Failed to create temp file: %ls", downloadPath.c_str());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"";
    }

    DWORD totalRead = 0;
    int lastPercent = -1;
    char buffer[65536];
    DWORD bytesRead = 0;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
        totalRead += bytesRead;

        if (contentLength > 0 && onProgress) {
            int percent = (int)((ULONGLONG)totalRead * 100 / contentLength);
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

    if (totalRead == 0) {
        log("Downloaded 0 bytes");
        DeleteFileW(downloadPath.c_str());
        return L"";
    }

    // Rename temp to final
    DeleteFileW(tempPath.c_str());
    if (!MoveFileW(downloadPath.c_str(), tempPath.c_str())) {
        log("Failed to rename downloaded file: %lu", GetLastError());
        DeleteFileW(downloadPath.c_str());
        return L"";
    }

    log("Installer downloaded: %lu bytes -> %ls", totalRead, tempPath.c_str());
    return tempPath;
}

}
