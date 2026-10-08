#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace forensic {

constexpr int kSampleRate = 16000;
constexpr std::size_t kFeatureCount = 15;

inline constexpr std::array<const char*, kFeatureCount> kFeatureNames{
    "RMS energy (mean)",
    "RMS energy (variation)",
    "Short-time energy (mean)",
    "Short-time energy (variation)",
    "Zero-crossing rate",
    "ZCR (variation)",
    "Signal variance",
    "Temporal variation",
    "Spectral centroid (Hz)",
    "Centroid (variation)",
    "Spectral bandwidth (Hz)",
    "85% spectral roll-off (Hz)",
    "Spectral flatness",
    "Dominant frequency (Hz)",
    "Spectral flux",
};

enum class Label { Real = 0, Synthetic = 1 };

struct Audio {
    int sample_rate{};
    int source_sample_rate{};
    int channels{};
    std::vector<float> samples;
    std::filesystem::path path;
    Label label{Label::Real};
};

struct Analysis {
    std::array<double, kFeatureCount> features{};
    std::vector<float> waveform;
    std::vector<std::vector<float>> spectrogram_db;
    double duration_seconds{};
};

struct Evaluation {
    double accuracy{};
    double precision{};
    double recall{};
    double f1{};
    std::array<std::array<int, 2>, 2> confusion{};
    std::size_t training_count{};
    std::size_t test_count{};
};

struct Prediction {
    Label label{Label::Real};
    double score{};
};

std::vector<Audio> load_dataset(const std::filesystem::path& dataset_root);
Audio load_wav(const std::filesystem::path& path);
Audio load_audio(const std::filesystem::path& path);
Analysis analyze(const Audio& audio);

class RandomForest {
public:
    void train(const std::vector<Analysis>& analyses, const std::vector<Label>& labels);
    Prediction predict(const std::array<double, kFeatureCount>& features) const;
    bool ready() const noexcept;

private:
    struct Node {
        int feature{-1};
        double threshold{};
        double synthetic_probability{};
        int left{-1};
        int right{-1};
    };
    struct Tree {
        std::vector<Node> nodes;
    };
    std::vector<Tree> trees_;
};

struct TrainedDemo {
    RandomForest model;
    Evaluation evaluation;
    std::vector<Audio> samples;
};

TrainedDemo train_demo(const std::vector<Audio>& dataset);
std::string label_text(Label label);
std::string format_features(const Analysis& analysis);
std::string format_evaluation(const Evaluation& evaluation);

}  // namespace forensic
