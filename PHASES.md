# SpeakInto — Development Phases

## Phase 1: Core MVP (Windows) — LOCAL ONLY, PUSH-TO-TALK
**Goal:** Electron tray app running as background service. Hold keybind → record → local STT via whisper.cpp → paste into active window.

- [x] Initialize Electron + TypeScript project
- [x] System tray app with start/quit controls + auto-start on login
- [x] Push-to-talk keyboard hook via uiohook-napi (hold to record, release to stop)
- [x] Configurable keybind (default Ctrl+Alt+Space), min 500ms recording duration
- [x] Raw PCM audio capture via AudioWorklet (16kHz mono)
- [x] whisper.cpp sidecar binary for local transcription (pluggable Transcriber interface)
- [x] Model manager (auto-download ggml-base.bin from HuggingFace, checksum verification)
- [x] WAV encoding (Float32 PCM → 16-bit PCM WAV)
- [x] Text injection via clipboard (save → paste via uiohook keyTap → restore)
- [x] Tray icon states (idle/recording/transcribing) + balloon notifications for errors
- [x] Temp file cleanup on startup
- [ ] End-to-end testing + packaging with electron-builder

**Deliverable:** Installable Windows app that transcribes speech locally and types it anywhere.

---

## Phase 2: Real-Time Streaming & Overlay UI
**Goal:** Live transcription feedback as the user speaks.

- [ ] Streaming local STT (chunked whisper.cpp or alternative)
- [ ] Floating overlay window showing partial transcription in real-time
- [ ] Voice Activity Detection (VAD) to auto-stop on silence
- [ ] Audio level meter / waveform visualization in overlay
- [ ] Smooth transition from partial → final transcription
- [ ] Configurable auto-stop timeout

**Deliverable:** User sees words appear as they speak, with auto-stop.

---

## Phase 3: STT Engine Improvements
**Goal:** Better accuracy and performance.

- [ ] Support multiple whisper models (tiny, base, small, medium)
- [ ] GPU acceleration support (CUDA on Windows)
- [ ] Alternative transcriber implementations (WhisperX, faster-whisper via PyInstaller sidecar)
- [ ] Performance benchmarking and optimization
- [ ] Language selection for transcription

**Deliverable:** Faster, more accurate transcription with model choice.

---

## Phase 4: macOS Port
**Goal:** Feature parity on macOS.

- [ ] macOS text injection via CGEventPost / AppleScript
- [ ] Accessibility permission request flow
- [ ] Microphone permission handling (macOS-specific)
- [ ] macOS global hotkey (e.g., Cmd+Option+Space)
- [ ] macOS code signing and notarization
- [ ] Platform-specific installer (DMG)

**Deliverable:** Same app running natively on macOS.

---

## Phase 5: Settings & Polish
**Goal:** User-friendly configuration and quality-of-life features.

- [ ] Settings window (keybind, STT engine, language, model size)
- [ ] Keybind customization UI (with Win key warning)
- [ ] Update mechanism (electron-updater)
- [ ] Usage statistics (local only — transcription count, time saved)
- [ ] Onboarding wizard (first-run setup)

**Deliverable:** Polished, configurable app ready for daily use.
