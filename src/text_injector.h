#pragma once
#include <string>

namespace injector {

// Save clipboard, set to text, send Ctrl+V. Returns after paste is sent.
void injectText(const std::string& text);

// Restore clipboard to what it was before injectText. Call after a delay.
void restoreClipboard();

}
