#include "audio_core.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace forensic {
namespace {

constexpr std::size_t kFrameLength = 400;
constexpr std::size_t kHopLength = 160;
constexpr std::size_t kFftSize = 512;
constexpr std::size_t kTreeCount = 31;
constexpr double kPi = 3.14159265358979323846;

std::uint16_t read_u16(const unsigned char* data) {
    return static_cast<std::uint16_t>(data[0])
        | static_cast<std::uint16_t>(data[1] << 8);
}

std::uint32_t read_u32(const unsigned char* data) {
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

void fft(std::array<std::complex<double>, kFftSize>& values) {
    for (std::size_t i = 1, j = 0; i < kFftSize; ++i) {
        std::size_t bit = kFftSize >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(values[i], values[j]);
        }
    }
    for (std::size_t length = 2; length <= kFftSize; length <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(length);
        const std::complex<double> root(std::cos(angle), std::sin(angle));
        for (std::size_t begin = 0; begin < kFftSize; begin += length) {
            std::complex<double> factor(1.0, 0.0);
            const std::size_t half = length / 2;
            for (std::size_t j = 0; j < half; ++j) {
                const auto even = values[begin + j];
                const auto odd = values[begin + j + half] * factor;
                values[begin + j] = even + odd;
                values[begin + j + half] = even - odd;
                factor *= root;
            }
        }
    }
}

double mean(const std::vector<double>& values) {
    return values.empty() ? 0.0
        : std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double standard_deviation(const std::vector<double>& values, double average) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto value : values) {
        const double difference = value - average;
        sum += difference * difference;
    }
    return std::sqrt(sum / values.size());
}

std::vector<double> resample_linear(const std::vector<double>& input, int from, int to) {
    if (from == to) {
        return input;
    }
    const std::size_t output_size = static_cast<std::size_t>(
        std::llround(static_cast<double>(input.size()) * to / from));
    std::vector<double> output(output_size);
    for (std::size_t i = 0; i < output_size; ++i) {
        const double position = static_cast<double>(i) * from / to;
        const auto left = std::min(static_cast<std::size_t>(position), input.size() - 1);
        const auto right = std::min(left + 1, input.size() - 1);
        const double fraction = position - left;
        output[i] = input[left] * (1.0 - fraction) + input[right] * fraction;
    }
    return output;
}

double gini(const std::vector<std::size_t>& indices, const std::vector<Label>& labels) {
    if (indices.empty()) {
        return 0.0;
    }
    std::size_t synthetic = 0;
    for (const auto index : indices) {
        synthetic += labels[index] == Label::Synthetic ? 1U : 0U;
    }
    const double probability = static_cast<double>(synthetic) / indices.size();
    return 2.0 * probability * (1.0 - probability);
}

}  // namespace

Audio load_wav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open audio file: " + path.string());
    }
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::string(bytes.begin(), bytes.begin() + 4) != "RIFF"
        || std::string(bytes.begin() + 8, bytes.begin() + 12) != "WAVE") {
        throw std::runtime_error("Unsupported or invalid WAV file: " + path.string());
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t sample_rate = 0;
    const unsigned char* audio_data = nullptr;
    std::size_t audio_size = 0;
    for (std::size_t pos = 12; pos + 8 <= bytes.size();) {
        const std::string chunk(bytes.begin() + pos, bytes.begin() + pos + 4);
        const auto chunk_size = read_u32(bytes.data() + pos + 4);
        const std::size_t chunk_data = pos + 8;
        if (chunk_size > bytes.size() - chunk_data) {
            throw std::runtime_error("Truncated WAV chunk in: " + path.string());
        }
        if (chunk == "fmt " && chunk_size >= 16) {
            format = read_u16(bytes.data() + chunk_data);
            channels = read_u16(bytes.data() + chunk_data + 2);
            sample_rate = read_u32(bytes.data() + chunk_data + 4);
            bits = read_u16(bytes.data() + chunk_data + 14);
        } else if (chunk == "data") {
            audio_data = bytes.data() + chunk_data;
            audio_size = chunk_size;
        }
        pos = chunk_data + chunk_size + (chunk_size & 1U);
    }
    if (audio_data == nullptr || channels == 0 || sample_rate == 0) {
        throw std::runtime_error("WAV is missing a format or audio chunk: " + path.string());
    }
    if ((format != 1 && format != 3) || (format == 3 && bits != 32)
        || (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        throw std::runtime_error("WAV must use PCM or 32-bit float encoding: " + path.string());
    }
    const std::size_t bytes_per_sample = bits / 8;
    const std::size_t frame_bytes = bytes_per_sample * channels;
    if (frame_bytes == 0 || audio_size / frame_bytes == 0) {
        throw std::runtime_error("WAV contains no complete audio frames: " + path.string());
    }

    const std::size_t frames = audio_size / frame_bytes;
    std::vector<double> mono(frames, 0.0);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        double sum = 0.0;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto* sample = audio_data + frame * frame_bytes + channel * bytes_per_sample;
            double value = 0.0;
            if (format == 3) {
                float float_value = 0.0F;
                std::memcpy(&float_value, sample, sizeof(float_value));
                value = float_value;
            } else if (bits == 8) {
                value = (static_cast<int>(sample[0]) - 128) / 128.0;
            } else if (bits == 16) {
                value = static_cast<std::int16_t>(read_u16(sample)) / 32768.0;
            } else if (bits == 24) {
                std::int32_t integer = static_cast<std::int32_t>(sample[0])
                    | (static_cast<std::int32_t>(sample[1]) << 8)
                    | (static_cast<std::int32_t>(sample[2]) << 16);
                if ((integer & 0x00800000) != 0) {
                    integer |= static_cast<std::int32_t>(0xff000000);
                }
                value = integer / 8388608.0;
            } else {
                value = static_cast<std::int32_t>(read_u32(sample)) / 2147483648.0;
            }
            if (!std::isfinite(value)) {
                throw std::runtime_error("WAV contains non-finite samples: " + path.string());
            }
            sum += value;
        }
        mono[frame] = sum / channels;
    }
    if (std::all_of(mono.begin(), mono.end(), [](double x) { return std::abs(x) < 1e-8; })) {
        throw std::runtime_error("Audio is silent: " + path.string());
    }

    const auto resampled = resample_linear(mono, static_cast<int>(sample_rate), kSampleRate);
    Audio audio;
    audio.sample_rate = kSampleRate;
    audio.source_sample_rate = static_cast<int>(sample_rate);
    audio.channels = channels;
    audio.path = path;
    audio.samples.reserve(resampled.size());
    for (const auto sample : resampled) {
        audio.samples.push_back(static_cast<float>(sample));
    }
    return audio;
}

std::vector<Audio> load_dataset(const std::filesystem::path& dataset_root) {
    std::vector<Audio> result;
    for (const auto& item : std::filesystem::recursive_directory_iterator(dataset_root)) {
        if (!item.is_regular_file() || item.path().extension() != ".wav") {
            continue;
        }
        const auto relative = item.path().lexically_relative(dataset_root);
        const auto class_directory = relative.begin() == relative.end()
            ? std::string{} : relative.begin()->string();
        if (class_directory != "real" && class_directory != "synthetic") {
            continue;
        }
        auto audio = load_wav(item.path());
        audio.label = class_directory == "real" ? Label::Real : Label::Synthetic;
        result.push_back(std::move(audio));
    }
    std::sort(result.begin(), result.end(), [](const Audio& a, const Audio& b) {
        return a.path.generic_string() < b.path.generic_string();
    });
    if (result.size() < 8) {
        throw std::runtime_error("Bundled demo data is incomplete. Restore the data/demo_dataset folder.");
    }
    return result;
}

Analysis analyze(const Audio& audio) {
    if (audio.samples.size() < 2 || audio.sample_rate <= 0) {
        throw std::runtime_error("Audio is empty or invalid.");
    }
    const auto frame_count = std::max<std::size_t>(
        1, (audio.samples.size() <= kFrameLength)
            ? 1 : 1 + (audio.samples.size() - kFrameLength) / kHopLength);
    std::vector<double> rms;
    std::vector<double> energy;
    std::vector<double> zcr;
    std::vector<double> variance;
    std::vector<double> centroids;
    std::vector<double> bandwidths;
    std::vector<double> rolloffs;
    std::vector<double> flatness;
    std::vector<double> dominant;
    std::vector<double> normalized_spectra;
    std::vector<float> spectrogram;
    std::vector<double> previous_magnitude(kFftSize / 2 + 1, 0.0);
    double flux_total = 0.0;

    for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        const std::size_t start = frame_index * kHopLength;
        std::array<double, kFrameLength> frame{};
        double sum = 0.0;
        double square_sum = 0.0;
        double energy_sum = 0.0;
        std::size_t crossings = 0;
        bool previous_positive = false;
        for (std::size_t i = 0; i < kFrameLength; ++i) {
            const auto sample_index = start + i;
            const double sample = sample_index < audio.samples.size()
                ? audio.samples[sample_index] : 0.0;
            frame[i] = sample;
            sum += sample;
            square_sum += sample * sample;
            energy_sum += sample * sample;
            const bool positive = sample >= 0.0;
            if (i != 0 && positive != previous_positive) {
                ++crossings;
            }
            previous_positive = positive;
        }
        const double frame_mean = sum / kFrameLength;
        double frame_variance = 0.0;
        for (const auto sample : frame) {
            const double difference = sample - frame_mean;
            frame_variance += difference * difference;
        }
        rms.push_back(std::sqrt(square_sum / kFrameLength));
        energy.push_back(energy_sum);
        zcr.push_back(static_cast<double>(crossings) / kFrameLength);
        variance.push_back(frame_variance / kFrameLength);

        std::array<std::complex<double>, kFftSize> bins{};
        for (std::size_t i = 0; i < kFrameLength; ++i) {
            const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (kFrameLength - 1));
            bins[i] = std::complex<double>(frame[i] * window, 0.0);
        }
        fft(bins);
        std::vector<double> magnitude(kFftSize / 2 + 1);
        double magnitude_sum = 0.0;
        double power_sum = 0.0;
        double log_power_sum = 0.0;
        double weighted_frequency = 0.0;
        double peak = -1.0;
        std::size_t peak_index = 0;
        for (std::size_t bin = 0; bin < magnitude.size(); ++bin) {
            magnitude[bin] = std::abs(bins[bin]) / kFftSize;
            const double frequency = static_cast<double>(bin) * audio.sample_rate / kFftSize;
            magnitude_sum += magnitude[bin];
            weighted_frequency += frequency * magnitude[bin];
            power_sum += magnitude[bin] * magnitude[bin];
            log_power_sum += std::log(magnitude[bin] * magnitude[bin] + 1e-30);
            if (magnitude[bin] > peak) {
                peak = magnitude[bin];
                peak_index = bin;
            }
        }
        const double centroid = weighted_frequency / std::max(magnitude_sum, 1e-30);
        double weighted_spread = 0.0;
        double cumulative_power = 0.0;
        double rolloff_frequency = 0.0;
        bool rolloff_found = false;
        for (std::size_t bin = 0; bin < magnitude.size(); ++bin) {
            const double frequency = static_cast<double>(bin) * audio.sample_rate / kFftSize;
            weighted_spread += (frequency - centroid) * (frequency - centroid) * magnitude[bin];
            cumulative_power += magnitude[bin] * magnitude[bin];
            if (!rolloff_found && cumulative_power >= 0.85 * power_sum) {
                rolloff_frequency = frequency;
                rolloff_found = true;
            }
        }
        centroids.push_back(centroid);
        bandwidths.push_back(std::sqrt(weighted_spread / std::max(magnitude_sum, 1e-30)));
        rolloffs.push_back(rolloff_frequency);
        flatness.push_back(
            std::exp(log_power_sum / magnitude.size())
            / std::max(power_sum / magnitude.size(), 1e-30));
        dominant.push_back(static_cast<double>(peak_index) * audio.sample_rate / kFftSize);

        const double norm = std::sqrt(power_sum);
        double frame_flux = 0.0;
        for (std::size_t bin = 0; bin < magnitude.size(); ++bin) {
            const double current = magnitude[bin] / std::max(norm, 1e-30);
            const double difference = current - previous_magnitude[bin];
            frame_flux += difference * difference;
            previous_magnitude[bin] = current;
            spectrogram.push_back(static_cast<float>(
                20.0 * std::log10(std::max(magnitude[bin], 1e-12))));
        }
        flux_total += std::sqrt(frame_flux);
    }

    const double rms_mean = mean(rms);
    const double energy_mean = mean(energy);
    const double zcr_mean = mean(zcr);
    const double centroid_mean = mean(centroids);
    Analysis analysis;
    analysis.duration_seconds = static_cast<double>(audio.samples.size()) / audio.sample_rate;
    analysis.features = {
        rms_mean,
        standard_deviation(rms, rms_mean),
        energy_mean,
        standard_deviation(energy, energy_mean),
        zcr_mean,
        standard_deviation(zcr, zcr_mean),
        mean(variance),
        [&audio]() {
            double total = 0.0;
            for (std::size_t i = 1; i < audio.samples.size(); ++i) {
                total += std::abs(static_cast<double>(audio.samples[i]) - audio.samples[i - 1]);
            }
            return total / (audio.samples.size() - 1);
        }(),
        centroid_mean,
        standard_deviation(centroids, centroid_mean),
        mean(bandwidths),
        mean(rolloffs),
        mean(flatness),
        mean(dominant),
        flux_total / std::max<std::size_t>(1, frame_count),
    };

    const std::size_t stride = std::max<std::size_t>(1, audio.samples.size() / 12000);
    for (std::size_t i = 0; i < audio.samples.size(); i += stride) {
        analysis.waveform.push_back(audio.samples[i]);
    }
    analysis.spectrogram_db.resize(frame_count);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const auto begin = spectrogram.begin() + frame * (kFftSize / 2 + 1);
        analysis.spectrogram_db[frame] = std::vector<float>(
            begin, begin + (kFftSize / 2 + 1));
    }
    return analysis;
}

void RandomForest::train(
    const std::vector<Analysis>& analyses, const std::vector<Label>& labels) {
    if (analyses.size() != labels.size() || analyses.size() < 4) {
        throw std::runtime_error("Not enough labeled samples to train the demo classifier.");
    }
    std::mt19937 generator(2026);
    trees_.clear();
    trees_.reserve(kTreeCount);

    for (std::size_t tree_index = 0; tree_index < kTreeCount; ++tree_index) {
        Tree tree;
        std::vector<std::size_t> bootstrap;
        bootstrap.reserve(analyses.size());
        std::uniform_int_distribution<std::size_t> sample_pick(0, analyses.size() - 1);
        for (std::size_t i = 0; i < analyses.size(); ++i) {
            bootstrap.push_back(sample_pick(generator));
        }

        const auto build = [&](auto&& self, const std::vector<std::size_t>& indices,
                               int depth) -> int {
            std::size_t synthetic_count = 0;
            for (const auto index : indices) {
                synthetic_count += labels[index] == Label::Synthetic ? 1U : 0U;
            }
            Node node;
            node.synthetic_probability = indices.empty()
                ? 0.0 : static_cast<double>(synthetic_count) / indices.size();
            const int node_index = static_cast<int>(tree.nodes.size());
            tree.nodes.push_back(node);
            if (indices.size() < 4 || depth >= 5 || synthetic_count == 0
                || synthetic_count == indices.size()) {
                return node_index;
            }

            std::array<std::size_t, kFeatureCount> feature_indices{};
            std::iota(feature_indices.begin(), feature_indices.end(), 0);
            std::shuffle(feature_indices.begin(), feature_indices.end(), generator);
            const std::size_t candidate_count = 4;
            double best_impurity = std::numeric_limits<double>::infinity();
            int best_feature = -1;
            double best_threshold = 0.0;
            std::vector<std::size_t> best_left;
            std::vector<std::size_t> best_right;
            for (std::size_t candidate = 0; candidate < candidate_count; ++candidate) {
                const auto feature = feature_indices[candidate];
                std::vector<double> values;
                values.reserve(indices.size());
                for (const auto index : indices) {
                    values.push_back(analyses[index].features[feature]);
                }
                std::sort(values.begin(), values.end());
                const double threshold = values[values.size() / 2];
                std::vector<std::size_t> left;
                std::vector<std::size_t> right;
                for (const auto index : indices) {
                    (analyses[index].features[feature] <= threshold ? left : right)
                        .push_back(index);
                }
                if (left.empty() || right.empty()) {
                    continue;
                }
                const double impurity =
                    (static_cast<double>(left.size()) * gini(left, labels)
                     + static_cast<double>(right.size()) * gini(right, labels))
                    / indices.size();
                if (impurity < best_impurity) {
                    best_impurity = impurity;
                    best_feature = static_cast<int>(feature);
                    best_threshold = threshold;
                    best_left = std::move(left);
                    best_right = std::move(right);
                }
            }
            if (best_feature < 0 || best_impurity >= gini(indices, labels)) {
                return node_index;
            }
            const int left_child = self(self, best_left, depth + 1);
            const int right_child = self(self, best_right, depth + 1);
            tree.nodes[node_index].feature = best_feature;
            tree.nodes[node_index].threshold = best_threshold;
            tree.nodes[node_index].left = left_child;
            tree.nodes[node_index].right = right_child;
            return node_index;
        };
        build(build, bootstrap, 0);
        trees_.push_back(std::move(tree));
    }
}

Prediction RandomForest::predict(const std::array<double, kFeatureCount>& features) const {
    if (trees_.empty()) {
        throw std::runtime_error("Demo classifier has not been trained.");
    }
    double probability = 0.0;
    for (const auto& tree : trees_) {
        int node_index = 0;
        while (tree.nodes[node_index].feature >= 0) {
            const auto& node = tree.nodes[node_index];
            node_index = features[static_cast<std::size_t>(node.feature)] <= node.threshold
                ? node.left : node.right;
        }
        probability += tree.nodes[node_index].synthetic_probability;
    }
    probability /= trees_.size();
    return {probability >= 0.5 ? Label::Synthetic : Label::Real,
            probability >= 0.5 ? probability : 1.0 - probability};
}

bool RandomForest::ready() const noexcept {
    return !trees_.empty();
}

TrainedDemo train_demo(const std::vector<Audio>& dataset) {
    std::vector<std::size_t> real;
    std::vector<std::size_t> synthetic;
    for (std::size_t i = 0; i < dataset.size(); ++i) {
        (dataset[i].label == Label::Real ? real : synthetic).push_back(i);
    }
    if (real.size() < 4 || synthetic.size() < 4) {
        throw std::runtime_error("Demo data must contain at least four samples per class.");
    }
    const auto split_class = [](std::vector<std::size_t> indices) {
        std::mt19937 generator(2042);
        std::shuffle(indices.begin(), indices.end(), generator);
        const std::size_t test_count = std::max<std::size_t>(1, indices.size() / 4);
        std::pair<std::vector<std::size_t>, std::vector<std::size_t>> result;
        result.second.assign(indices.begin(), indices.begin() + test_count);
        result.first.assign(indices.begin() + test_count, indices.end());
        return result;
    };
    const auto real_split = split_class(real);
    const auto synthetic_split = split_class(synthetic);
    std::vector<std::size_t> train_indices = real_split.first;
    train_indices.insert(train_indices.end(), synthetic_split.first.begin(), synthetic_split.first.end());
    std::vector<std::size_t> test_indices = real_split.second;
    test_indices.insert(test_indices.end(), synthetic_split.second.begin(), synthetic_split.second.end());

    std::vector<Analysis> analyses;
    analyses.reserve(dataset.size());
    for (const auto& audio : dataset) {
        analyses.push_back(analyze(audio));
    }
    std::vector<Analysis> training_features;
    std::vector<Label> training_labels;
    for (const auto index : train_indices) {
        training_features.push_back(analyses[index]);
        training_labels.push_back(dataset[index].label);
    }

    TrainedDemo result;
    result.model.train(training_features, training_labels);
    result.evaluation.training_count = train_indices.size();
    result.evaluation.test_count = test_indices.size();
    int true_positive = 0;
    int false_positive = 0;
    int false_negative = 0;
    int correct = 0;
    for (const auto index : test_indices) {
        const auto prediction = result.model.predict(analyses[index].features).label;
        const auto actual = dataset[index].label;
        ++result.evaluation.confusion[static_cast<int>(actual)][static_cast<int>(prediction)];
        correct += prediction == actual ? 1 : 0;
        if (actual == Label::Synthetic && prediction == Label::Synthetic) {
            ++true_positive;
        } else if (actual == Label::Real && prediction == Label::Synthetic) {
            ++false_positive;
        } else if (actual == Label::Synthetic && prediction == Label::Real) {
            ++false_negative;
        }
    }
    result.evaluation.accuracy = static_cast<double>(correct) / test_indices.size();
    result.evaluation.precision = true_positive + false_positive == 0
        ? 0.0 : static_cast<double>(true_positive) / (true_positive + false_positive);
    result.evaluation.recall = true_positive + false_negative == 0
        ? 0.0 : static_cast<double>(true_positive) / (true_positive + false_negative);
    result.evaluation.f1 = result.evaluation.precision + result.evaluation.recall == 0.0
        ? 0.0 : 2.0 * result.evaluation.precision * result.evaluation.recall
            / (result.evaluation.precision + result.evaluation.recall);
    result.samples = dataset;
    return result;
}

std::string label_text(Label label) {
    return label == Label::Real ? "REAL" : "AI-GENERATED";
}

std::string format_features(const Analysis& analysis) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(5);
    for (std::size_t i = 0; i < kFeatureCount; ++i) {
        output << std::left << std::setw(29) << kFeatureNames[i]
               << std::right << analysis.features[i] << '\n';
    }
    return output.str();
}

std::string format_evaluation(const Evaluation& evaluation) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1)
           << "DEMO HOLD-OUT  ·  " << evaluation.test_count << " clips\n"
           << "Accuracy " << 100.0 * evaluation.accuracy
           << "%   Precision · AI " << 100.0 * evaluation.precision << "%\n"
           << "Recall · AI " << 100.0 * evaluation.recall
           << "%   F1 · AI " << 100.0 * evaluation.f1 << "%\n"
           << "Confusion (actual / predicted)\n"
           << "REAL [" << evaluation.confusion[0][0] << ", "
           << evaluation.confusion[0][1] << "]   AI ["
           << evaluation.confusion[1][0] << ", "
           << evaluation.confusion[1][1] << ']';
    return output.str();
}

}  // namespace forensic
