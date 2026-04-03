# Wisper Agent — Development Phases

## Phase 1: Core MVP (Windows)
**Goal:** Electron tray app with global hotkey → record → cloud STT → paste into active window.

- [ ] Initialize Electron + TypeScript project
- [ ] System tray app with start/quit controls
- [ ] Global hotkey registration (configurable, default `Ctrl+Alt+Space`)
- [ ] Microphone audio capture (start/stop on hotkey toggle)
- [ ] OpenAI Whisper API integration for transcription
- [ ] Text injection into active window via clipboard (save → paste → restore)
- [ ] Visual recording indicator (small overlay or tray icon change)
- [ ] Basic error handling (no mic, API key missing, network error)
- [ ] `.env` config for API key

**Deliverable:** Installable Windows app that transcribes speech and types it anywhere.

---

## Phase 2: Real-Time Streaming & Overlay UI
**Goal:** Live transcription feedback as the user speaks.

- [ ] Streaming STT integration (Deepgram or Azure Speech)
- [ ] Floating overlay window showing partial transcription in real-time
- [ ] Voice Activity Detection (VAD) to auto-stop on silence
- [ ] Audio level meter / waveform visualization in overlay
- [ ] Smooth transition from partial → final transcription
- [ ] Configurable auto-stop timeout

**Deliverable:** User sees words appear as they speak, with auto-stop.

---

## Phase 3: Local/Offline STT
**Goal:** No internet required, no API costs.

- [ ] Bundle `whisper.cpp` as native addon or sidecar binary
- [ ] Model download manager (tiny, base, small, medium)
- [ ] Toggle between cloud and local STT in settings
- [ ] GPU acceleration support (CUDA on Windows)
- [ ] Performance benchmarking and optimization
- [ ] Graceful fallback: local → cloud if local fails

**Deliverable:** Fully offline voice-to-text with no API dependency.

---

## Phase 4: macOS Port
**Goal:** Feature parity on macOS.

- [ ] macOS text injection via `CGEventPost` / AppleScript
- [ ] Accessibility permission request flow
- [ ] Microphone permission handling (macOS-specific)
- [ ] macOS global hotkey (e.g., `Cmd+Option+Space`)
- [ ] macOS code signing and notarization
- [ ] Platform-specific installer (DMG)

**Deliverable:** Same app running natively on macOS.

---

## Phase 5: Settings & Polish
**Goal:** User-friendly configuration and quality-of-life features.

- [ ] Settings window (hotkey, STT engine, language, model size)
- [ ] Language selection for transcription
- [ ] Hotkey customization UI
- [ ] Auto-start on login
- [ ] Update mechanism (electron-updater)
- [ ] Usage statistics (local only — transcription count, time saved)
- [ ] Onboarding wizard (first-run setup)

**Deliverable:** Polished, configurable app ready for daily use.
