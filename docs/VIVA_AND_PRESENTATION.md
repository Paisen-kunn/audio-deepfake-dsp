# Viva and Presentation Guide

## Suggested slides

1. Motivation and the bounded question: can basic signal descriptors separate samples in this demo corpus?
2. Discrete-time representation, sampling rate, quantization, and channel mixing.
3. Local application architecture: C++17 DSP/classifier core, native Windows/macOS UI, bundled WAV examples, no runtime internet.
4. Waveform and time-domain measurements: RMS, short-time energy, zero-crossing rate, variance.
5. DFT/FFT and STFT: 25 ms Hann frame, 10 ms hop, 512-point FFT, spectrogram.
6. Frequency descriptors: centroid, bandwidth, 85% roll-off, flatness, dominant frequency, flux.
7. Dataset, classifier, six-sample hold-out metrics, and confusion matrix—clearly marked as a demo.
8. Limitations and future work: bigger licensed data, speaker-disjoint splits, external evaluation.

## Viva questions

**What is a discrete-time signal?**  
A sequence \(x[n]\) sampled at integer indices from a continuous signal.

**What does quantization do?**  
It maps an amplitude to one of a finite set of representable levels and introduces quantization error.

**What is the Nyquist frequency?**  
Half the sampling frequency; higher input components can alias after sampling unless attenuated.

**Why resample?**  
To standardize analysis rate. This implementation uses linear interpolation, which is simple but not ideal high-quality anti-alias resampling.

**What is the difference between the DFT and FFT?**  
The DFT is the mathematical transform; the FFT is an efficient algorithm to compute it.

**Why the STFT?**  
Speech changes over time. Windowed overlapping FFT frames show how frequency content evolves instead of collapsing the entire clip into one spectrum.

**What do centroid and flatness measure?**  
Centroid is the magnitude-weighted frequency center. Flatness is the geometric-to-arithmetic mean ratio of spectral power.

**Why report precision, recall, F1, and a confusion matrix?**  
Accuracy can hide which class is misclassified. Precision, recall, and F1 describe synthetic-class errors; the matrix shows counts for each actual/predicted pair.

**Can the app prove audio is fake?**  
No. It uses only 24 bundled examples and six hold-out recordings. It demonstrates a DSP workflow, not a universal or reliable detector. Dataset shortcuts, speaker overlap, codecs, languages, and unseen generators can change results.

**Does the app need internet?**  
No at runtime. Audio samples are included with the project. A local compiler and build tools are needed to build the native app.
