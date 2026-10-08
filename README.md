# AUDIO//FORENSICS — offline C++ desktop app

A native desktop application for local analysis of audio signal characteristics. The core is standard C++17: it reads PCM WAV, resamples to 16 kHz, computes time-domain statistics and an FFT/STFT spectrogram, trains a small Random Forest demo classifier, and evaluates it on a stratified recording-level hold-out set.

**No server, browser, Python, API, model download, or network connection is used at runtime.** The 24 labeled public audio examples are included under `data/demo_dataset/` as WAV files. The app trains its demonstration classifier from those local files when it starts.

> The bundled dataset subset and model are only for demonstrating the software. The score and six-clip hold-out metrics are not official benchmark results, proof of authenticity, or evidence of general deepfake detection.

## Start on Windows

1. Install **CMake** and **Visual Studio 2022** with the **Desktop development with C++** workload once. This is the build setup; it is not a runtime internet dependency.
2. Double-click [`Start Windows.bat`](./Start%20Windows.bat).

The script builds the native Windows GUI from the included C++ sources and opens it. After that first local build, launch `build\Release\audio_forensics.exe` directly. The executable and its bundled `data` folder must stay together. No Python or network access is needed.

## Start on macOS

Install Xcode Command Line Tools and CMake once, then double-click [`Start macOS.command`](./Start%20macOS.command), or run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
open build/audio_forensics.app
```

No internet connection is needed to build or run this app from the included source/data after the local C++ toolchain is installed. Keep the `.app` bundle intact; its `Contents/MacOS/data` folder contains the example audio.

The native user interface uses Win32 on Windows and Cocoa on macOS. The DSP/classifier engine is shared C++17. Upload/open supports PCM WAV files (8/16/24/32-bit integer or 32-bit float) and MP3 files through the native Windows Media Foundation or macOS AudioToolbox decoder. The bundled examples are WAV.

## Use the GUI

- Choose any bundled `REAL` or `AI-GENERATED` sample from the dropdown.
- Click **Analyze Sample** to display/play the signal analysis.
- Click **Open Audio** to examine another local WAV or MP3 recording.
- Click **Play** to listen to the selected recording.
- Read the waveform, STFT spectrogram, 15-feature table, demo classifier output, and hold-out metrics.

## DSP details

Audio channels are averaged to mono, then linearly resampled to 16 kHz. The analysis uses 25 ms (400-sample) Hann frames, 10 ms (160-sample) hops, and a 512-point radix-2 FFT. Per-recording features include frame RMS/energy, zero-crossing rate, variance, temporal variation, spectral centroid and variation, bandwidth, 85% power roll-off, flatness, dominant frequency, and spectral flux.

The classifier is a small C++ Random Forest trained from a deterministic stratified split of the **24 bundled samples** (18 training clips, 6 test clips). The score is uncalibrated. Dataset/source shortcuts, speaker overlap, and tiny sample size make performance estimates unreliable. The detector is bounded to this sample set and must not be described as universal or used as a forensic verdict.

## Dataset and attribution

The included clips are PCM WAV conversions of 12 real and 12 synthetic samples from [Deepfake Audio Detection Dataset v4](https://huggingface.co/datasets/garystafford/deepfake-audio-detection), by Gary Stafford. The selected upstream row IDs are retained in the filenames `sample_NNNN.wav`. See [ATTRIBUTION.md](ATTRIBUTION.md) for the dataset revision, credit, and CC BY 4.0 license details. The upstream dataset card documents data provenance and limitations; review it before redistributing the audio.

## Build and test

Requires CMake 3.20+, a C++17 compiler, and (for the GUI build) the macOS SDK or Windows SDK/Visual Studio C++ tools.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The C++ test checks WAV decoding, the bundled class balance, DSP feature values, model training, predictions, and held-out confusion-matrix totals.
