# SpeakInto

Local, offline speech-to-text for Windows. Hold a hotkey, speak, release — your words are typed into whatever app has focus. No cloud, no API keys, no subscription.

Powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp) with optional CUDA GPU acceleration.

## How it works

1. Press and hold **Ctrl+`** (backtick)
2. A red **REC** pill appears at the top of your screen
3. Speak naturally
4. Release the hotkey — text is transcribed and pasted into the focused app

Works in any application: editors, browsers, terminals, chat apps, email — anywhere you can type.

## Features

- **Fully offline** — all processing happens on your machine, nothing leaves your device
- **Global hotkey** — works from any app, no window switching needed
- **GPU accelerated** — automatic CUDA support for NVIDIA GPUs, with CPU fallback
- **Multiple model sizes** — Tiny (~75MB), Base (~150MB), Small (~500MB), Medium (~1.5GB)
- **Auto language detection** — transcribes in any language Whisper supports
- **Microphone selection** — choose any audio input device from the system tray
- **Smart text processing** — optional local LLM (Qwen 2.5) cleans up grammar and punctuation
- **Overlay indicators** — visual feedback for recording, transcribing, and errors
- **Auto-updates** — checks for new releases from GitHub
- **Lightweight** — native C++ app, no Python or Node.js runtime required

## Installation

### Download installer

Grab the latest release from [GitHub Releases](https://github.com/tigranhov/speakinto/releases):

- **Universal** (`speakinto-setup-universal-win64.exe`) — CPU only, works on any Windows 10/11 PC
- **NVIDIA** (`speakinto-nvidia-setup-win64.exe`) — includes CUDA acceleration for NVIDIA GPUs

### Build from source

Requirements: Visual Studio 2022, CMake 3.20+

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The built executable will be at `build/Release/speakinto.exe`. You'll also need the whisper.cpp binaries — see `scripts/download-whisper.sh` (CPU) or `scripts/download-whisper-cuda.sh` (CUDA).

## Usage

After installation, SpeakInto runs in the system tray.

### Default hotkey

**Ctrl+`** (Ctrl + backtick) — hold to record, release to transcribe.

Recordings shorter than 500ms are discarded to prevent accidental activations.

### System tray menu

Right-click the tray icon to:

- **Microphone** — select which audio input device to use
- **Settings** — configure model size, repeat press behavior, text processing, and updates
- **Quit** — exit the app

### Overlay states

| Color | Text | Meaning |
|-------|------|---------|
| Red | REC | Recording your voice |
| Blue | ... | Transcribing (pulsing animation) |
| Gray | Loading... | Initializing model or downloading |
| Green | DL X% | Downloading a model |
| Amber | No Mic | No microphone detected |

### Repeat press behavior

If you press the hotkey while a transcription is still running:

- **Queue** (default) — starts a new recording immediately; previous transcription continues in the background
- **Flash** — flashes the overlay to acknowledge, but doesn't start a new recording
- **Cancel** — cancels the running transcription and starts recording immediately

### Text processing (optional)

Enable in Settings to run transcribed text through a local LLM that fixes grammar, punctuation, and formatting. This downloads and runs [Qwen 2.5 1.5B](https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF) via [llama.cpp](https://github.com/ggml-org/llama.cpp) — entirely local, no cloud involved.

## Configuration

Settings are stored in `%APPDATA%\speakinto\settings.json`:

| Setting | Options | Default |
|---------|---------|---------|
| Model size | tiny, base, small, medium | small |
| Repeat press mode | queue, flash, cancel | queue |
| Microphone | any detected device | system default |
| Text processor | on / off | off |

Models are downloaded automatically on first use and stored in `%APPDATA%\speakinto\models\`.

## Requirements

- Windows 10 or 11 (64-bit)
- A microphone
- For GPU acceleration: NVIDIA GPU with CUDA 12.4+ support

## License

MIT
