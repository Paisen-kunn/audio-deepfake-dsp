# Project Report: Offline C++ DSP Audio Analysis

## Abstract

This project is a local native desktop application for inspecting sampled audio and demonstrating a DSP-feature-based synthetic-speech classifier. Its shared C++17 core reads PCM WAV recordings, averages channels, resamples to 16 kHz, computes waveform and FFT-based STFT analysis, extracts interpretable time/spectral features, and trains a small Random Forest from 24 bundled, labeled public examples. The app reports accuracy, precision, recall, F1, and a six-recording stratified hold-out confusion matrix. These metrics demonstrate software functionality only; the tiny sample split is not an official benchmark or evidence of general detection. No network access, remote inference, or hosted service is used.

## Problem statement and objectives

Study discrete-time and spectral characteristics in genuine and synthetic speech while implementing a locally runnable desktop tool. Demonstrate sampling/resampling, time/frequency features, STFT visualization, a transparent classifier baseline, and evaluation. Do not claim that a compact feature classifier universally detects audio deepfakes.

## DSP background

Sampling represents a continuous-time waveform as discrete values \(x[n]\), with sample interval \(T_s=1/f_s\). Quantization maps amplitudes to finite levels and introduces error. Downsampling requires interpolation and anti-alias filtering; this demo performs simple linear resampling to 16 kHz and should not be treated as a high-fidelity conversion tool.

The discrete Fourier transform (DFT) maps a finite sample block to frequency bins; the fast Fourier transform (FFT) computes it efficiently. The short-time Fourier transform (STFT) repeats windowed transforms at overlapping hops:

\[
X(m,k)=\sum_n x[n]w[n-mH]e^{-j2\pi kn/N}
\]

Here the app uses a 400-sample Hann frame (25 ms), a 160-sample hop (10 ms), and a 512-point radix-2 FFT. The spectrogram displays log magnitude over time and frequency.

Frame statistics include RMS, short-time energy, zero-crossing rate, and variance. Spectral statistics include centroid, bandwidth, 85% power roll-off, flatness, dominant frequency, and spectral flux.

## Dataset and method

The local demo includes 12 real and 12 synthetic PCM WAV conversions from Gary Stafford's *Deepfake Audio Detection Dataset v4*, CC BY 4.0. Attribution and upstream revision are in [ATTRIBUTION.md](../ATTRIBUTION.md). Sample IDs are in the WAV filenames. Review the dataset card for source provenance and any constituent-source conditions before redistribution.

The C++ engine uses a stratified recording-level split: 18 training recordings and 6 held-out recordings, followed by a 31-tree, depth-limited Random Forest. Each startup computes the features and trains locally from the bundled WAV data. Metrics characterize only that tiny sample split, not the entire upstream set.

## Results

Use the metrics shown by the application only as an implementation demonstration. Do not present them as official ASVspoof/dataset scores, evidence of generalization, or a calibrated confidence of deception. For academic claims, obtain permissioned larger data, keep speaker/source groups separated across splits, use external evaluation, report uncertainty and per-class errors, and compare with simple baselines.

## Limitations

The small hold-out set produces highly uncertain metrics. Recording-level splitting can leak source or speaker characteristics across partitions. DSP features respond to speech content, speaker, language, codec, microphone, room, source platform, and synthesis system. The Random Forest score is uncalibrated and is not a forensic verdict. Resampling is linear interpolation. Uploaded formats are limited to PCM WAV.

## Viva topics

Explain sampled and quantized signals, Nyquist frequency, resampling and aliasing, DFT versus FFT, STFT windows and hop overlap, RMS/energy/ZCR, spectral descriptors, stratified splits, confusion matrices, and why a small dataset cannot validate universal deepfake detection.
