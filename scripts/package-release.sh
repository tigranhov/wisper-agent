#!/bin/bash
# Package speakinto for GitHub release
# Usage: bash scripts/package-release.sh
# Output: release/speakinto-win64.zip

set -e

RELEASE_DIR="release/speakinto"
ZIP_NAME="release/speakinto-win64.zip"

# Clean
rm -rf release/
mkdir -p "$RELEASE_DIR"

# Build
echo "Building..."
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# Copy app exe
cp build/Release/speakinto.exe "$RELEASE_DIR/"

# Copy whisper.cpp binaries (exe + DLLs)
cp bin/Release/whisper-cli.exe "$RELEASE_DIR/"
cp bin/Release/whisper.dll "$RELEASE_DIR/"
cp bin/Release/ggml.dll "$RELEASE_DIR/"
cp bin/Release/ggml-cpu.dll "$RELEASE_DIR/"
cp bin/Release/ggml-base.dll "$RELEASE_DIR/"

# Copy icon assets
mkdir -p "$RELEASE_DIR/assets/icons"
cp assets/icons/*.ico "$RELEASE_DIR/assets/icons/"

echo ""
echo "Contents of $RELEASE_DIR:"
ls -lh "$RELEASE_DIR/"
ls -lh "$RELEASE_DIR/assets/icons/"

# Create ZIP
cd release
zip -r "../$ZIP_NAME" speakinto/
cd ..

echo ""
echo "Package created: $ZIP_NAME"
ls -lh "$ZIP_NAME"
echo ""
echo "Upload this to GitHub Releases."
