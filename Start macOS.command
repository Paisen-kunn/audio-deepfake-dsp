#!/bin/bash
set -e
cd "$(dirname "$0")"
command -v cmake >/dev/null || { echo "Install CMake and Xcode Command Line Tools first."; read -r; exit 1; }
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
exec "$PWD/build/audio_forensics.app/Contents/MacOS/audio_forensics"
