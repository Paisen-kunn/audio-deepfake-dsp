#include "audio_core.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected the bundled demo dataset path.");
        }
        bool mixed_case_mp3_dispatched = false;
        try {
            forensic::load_audio("missing.Mp3");
        } catch (const std::runtime_error& error) {
            if (std::string(error.what()).find("Unsupported audio format") != std::string::npos) {
                throw std::runtime_error("Mixed-case MP3 extension was not dispatched.");
            }
            mixed_case_mp3_dispatched = true;
        }
        if (!mixed_case_mp3_dispatched) {
            throw std::runtime_error("Missing MP3 fixture unexpectedly decoded.");
        }
        const auto samples = forensic::load_dataset(argv[1]);
        if (samples.size() != 24) {
            throw std::runtime_error("Expected exactly 24 bundled sample recordings.");
        }
        std::size_t real_count = 0;
        std::size_t synthetic_count = 0;
        std::size_t resampled_count = 0;
        for (const auto& audio : samples) {
            real_count += audio.label == forensic::Label::Real ? 1U : 0U;
            synthetic_count += audio.label == forensic::Label::Synthetic ? 1U : 0U;
            resampled_count += audio.sample_rate == forensic::kSampleRate ? 1U : 0U;
            const auto result = forensic::analyze(audio);
            if (result.waveform.empty() || result.spectrogram_db.empty()) {
                throw std::runtime_error("Missing waveform or spectrogram data.");
            }
            for (const auto value : result.features) {
                if (!std::isfinite(value)) {
                    throw std::runtime_error("DSP features contain a non-finite value.");
                }
            }
        }
        if (real_count != 12 || synthetic_count != 12
            || resampled_count != samples.size()) {
            throw std::runtime_error("Bundled demo data must have balanced classes.");
        }

        const auto demo = forensic::train_demo(samples);
        if (!demo.model.ready() || demo.evaluation.test_count != 6
            || demo.evaluation.training_count != 18) {
            throw std::runtime_error("Demo classifier did not train/evaluate as expected.");
        }
        const auto analysis = forensic::analyze(samples.front());
        const auto prediction = demo.model.predict(analysis.features);
        if (prediction.score < 0.5 || prediction.score > 1.0) {
            throw std::runtime_error("Classifier output is outside its valid range.");
        }
        const int matrix_total =
            demo.evaluation.confusion[0][0] + demo.evaluation.confusion[0][1]
            + demo.evaluation.confusion[1][0] + demo.evaluation.confusion[1][1];
        if (matrix_total != 6) {
            throw std::runtime_error("Hold-out confusion matrix count is incorrect.");
        }
        const auto evaluation_text = forensic::format_evaluation(demo.evaluation);
        if (evaluation_text.find("6 clips") == std::string::npos
            || evaluation_text.find("0 clips") != std::string::npos) {
            throw std::runtime_error("Demo evaluation summary is empty or incorrect.");
        }
        std::cout << "PASS: WAV decoding, DSP features, bundled labels, classifier, "
                     "and held-out evaluation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
