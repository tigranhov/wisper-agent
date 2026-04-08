---
description: Build, package, and publish a new GitHub release with ZIP and installer assets. Use when the user asks to release, publish, or ship a new version.
disable-model-invocation: true
allowed-tools: Bash Read Glob Grep
argument-hint: "[version-bump: patch|minor|major]"
---

# Release Version

Build, package, and publish a new release of SpeakInto to GitHub.

## Step 1: Determine version

1. Get the latest release tag: `gh release list --limit 1`
2. Parse the current version (format: `vMAJOR.MINOR.PATCH`)
3. Increment based on `$ARGUMENTS` or default to `patch`:
   - `patch` (default): bug fixes, hotkey changes, small improvements
   - `minor`: new features (e.g., new UI, new audio backend, new model support)
   - `major`: breaking changes or full rewrites
4. Confirm the new version with the user before proceeding

## Step 2: Update version in installers

Update `AppVersion` in **both** `installer-universal.iss` and `installer-nvidia.iss` to match the new version (without the `v` prefix).

## Step 3: Build

```
taskkill /f /im speakinto.exe 2>/dev/null
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Verify the build succeeds with no errors.

## Step 4: Package ZIPs

**Universal ZIP (CPU only):**
```
rm -rf release/speakinto-universal
mkdir -p release/speakinto-universal/assets/icons
cp build/Release/speakinto.exe release/speakinto-universal/
cp bin/Release/whisper-cli.exe release/speakinto-universal/
cp bin/Release/whisper.dll release/speakinto-universal/
cp bin/Release/ggml.dll release/speakinto-universal/
cp bin/Release/ggml-cpu.dll release/speakinto-universal/
cp bin/Release/ggml-base.dll release/speakinto-universal/
cp assets/icons/*.ico release/speakinto-universal/assets/icons/
cd release && powershell -Command "Compress-Archive -Path 'speakinto-universal' -DestinationPath 'speakinto-universal-win64.zip' -Force"
```

**NVIDIA ZIP (CUDA + CPU fallback):**
```
rm -rf release/speakinto-nvidia
mkdir -p release/speakinto-nvidia/assets/icons
cp build/Release/speakinto.exe release/speakinto-nvidia/
# CPU fallback exe + shared DLLs (no cuBLAS dependency)
cp bin/Release/whisper-cli.exe release/speakinto-nvidia/
cp bin/Release/whisper.dll release/speakinto-nvidia/
cp bin/Release/ggml.dll release/speakinto-nvidia/
cp bin/Release/ggml-cpu.dll release/speakinto-nvidia/
cp bin/Release/ggml-base.dll release/speakinto-nvidia/
# CUDA exe (rename so app detects it) + CUDA plugin
cp bin/cuda/whisper-cli.exe release/speakinto-nvidia/whisper-cli-cuda.exe
cp bin/cuda/ggml-cuda.dll release/speakinto-nvidia/
cp bin/cuda/cudart64_12.dll release/speakinto-nvidia/
# Icons
cp assets/icons/*.ico release/speakinto-nvidia/assets/icons/
cd release && powershell -Command "Compress-Archive -Path 'speakinto-nvidia' -DestinationPath 'speakinto-nvidia-win64.zip' -Force"
```

## Step 5: Build installers

```
"$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" installer-universal.iss
"$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe" installer-nvidia.iss
```

This produces:
- `release/speakinto-setup-universal-win64.exe`
- `release/speakinto-nvidia-setup-win64.exe`

## Step 6: Generate changelog

1. Run `git log --oneline <previous-tag>..HEAD` to get commits since last release
2. If previous tag doesn't exist as a ref, find its commit: `gh release view <previous-tag> --json targetCommitish`
3. Write a changelog grouped by type:
   - **Features** — new capabilities
   - **Fixes** — bug fixes
   - **Changes** — behavioral changes, refactors
4. Each entry should describe the user-facing impact, not implementation details
5. Present the changelog to the user for approval before publishing

## Step 7: Commit, push, and publish

1. If installer files were modified (version bump), commit them:
   ```
   git add installer-universal.iss installer-nvidia.iss
   git commit -m "Bump version to vX.Y.Z"
   ```
2. Push to origin: `git push origin main`
3. Create the GitHub release:
   ```
   gh release create vX.Y.Z \
     release/speakinto-universal-win64.zip \
     release/speakinto-nvidia-win64.zip \
     release/speakinto-setup-universal-win64.exe \
     release/speakinto-nvidia-setup-win64.exe \
     --title "vX.Y.Z — <short title>" \
     --notes "<changelog from Step 6>"
   ```
4. Report the release URL to the user

## Release assets checklist

Every release MUST include all four:
- `speakinto-universal-win64.zip` — portable, CPU only
- `speakinto-nvidia-win64.zip` — portable, CUDA + CPU fallback
- `speakinto-setup-universal-win64.exe` — installer, CPU only
- `speakinto-nvidia-setup-win64.exe` — installer, CUDA + CPU fallback

## Notes

- Never publish a release without building fresh from the current HEAD
- Always verify build succeeds before packaging
- `installer-universal.iss` and `installer-nvidia.iss` source files from `build/Release/` and `bin/Release/` — these paths must exist
- `bin/Release/` contains pre-built whisper.cpp CPU binaries (not built by cmake)
- `bin/cuda/` contains pre-built whisper.cpp CUDA binaries (not built by cmake)
- The NVIDIA variant renames `bin/cuda/whisper-cli.exe` to `whisper-cli-cuda.exe` so the app detects it
