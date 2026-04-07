#pragma once
#include <string>
#include <functional>

namespace cuda {

// Check if cuBLAS DLLs are available (in app dir or %APPDATA%\wisper-agent\cuda\)
bool isCuBlasAvailable();

// Download cuBLAS DLLs to %APPDATA%\wisper-agent\cuda\. Blocking — call from background thread.
bool ensureCuBlas(std::function<void(int percent)> onProgress = nullptr);

// Add cuBLAS directory to PATH so child processes can find the DLLs.
// Call once after cuBLAS is confirmed available.
void addCuBlasToPath();

}
