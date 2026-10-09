#include "engine/classifier.hpp"
#include "engine/ingest.hpp"
#include "engine/pan_tompkins.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

int main(int argc, char* argv[]) {
  if (argc < 2 || argc > 5) {
    std::cerr << "Usage: ecg_playback <record-or-header> "
                 "[playback-rate; 0=fast] [--qrs] [--classify]\n";
    return 2;
  }
  try {
    double rate = 0.0;
    bool rate_seen = false;
    bool qrs_enabled = false;
    bool classifier_enabled = false;
    for (int argument = 2; argument < argc; ++argument) {
      const std::string value(argv[argument]);
      if (value == "--qrs") {
        qrs_enabled = true;
      } else if (value == "--classify") {
        classifier_enabled = true;
        qrs_enabled = true;
      } else if (!rate_seen) {
        rate = std::stod(value);
        rate_seen = true;
      } else {
        throw std::invalid_argument("Unexpected argument: " + value);
      }
    }
    clavis::engine::MitBihReader reader(argv[1]);
    const auto& metadata = reader.metadata();
    std::cout << "sampling_rate_hz=" << metadata.sampling_rate_hz
              << " voltage_unit=" << metadata.voltage_unit << '\n';
    std::optional<clavis::engine::PanTompkinsDetector> detector;
    std::optional<clavis::engine::PhysiologicalClassifier> classifier;
    if (qrs_enabled) {
      detector.emplace(metadata.sampling_rate_hz);
      std::cout << "qrs_channel=" << metadata.signal_names.front()
                << " qrs_status=insufficient_data startup_samples="
                << detector->startup_samples() << '\n';
    }
    if (classifier_enabled) classifier.emplace();
    clavis::engine::play_realtime(reader, rate, [&](const auto& sample) {
      if (!qrs_enabled) {
        std::cout << sample.index << ' ' << std::fixed << std::setprecision(6)
                  << sample.time_seconds;
        for (double voltage : sample.voltage) std::cout << ' ' << voltage;
        std::cout << '\n';
      } else {
        const auto result = detector->process(sample.voltage.front());
        if (result.peak_detected) {
          std::cout << "r_peak_sample=" << result.r_peak_index
                    << " time_seconds=" << std::fixed << std::setprecision(6)
                    << static_cast<double>(result.r_peak_index) /
                           metadata.sampling_rate_hz
                    << " detected_at_sample=" << result.detection_sample_index
                    << " processing_delay_seconds="
                    << result.processing_delay_seconds;
          if (result.sufficient_data) {
            std::cout << " rr_seconds=" << *result.rr_interval_seconds
                      << " heart_rate_bpm=" << *result.heart_rate_bpm
                      << " status=ok";
          } else {
            std::cout << " rr_seconds=NA heart_rate_bpm=NA"
                         " status=insufficient_data";
          }
          if (classifier_enabled) {
            const auto classification = classifier->process(
                result.rr_interval_seconds, result.heart_rate_bpm);
            if (classification.status ==
                clavis::engine::ClassificationStatus::insufficient_data) {
              std::cout << " classification=insufficient_data"
                           " bradycardia=0 tachycardia=0"
                           " irregular_rr=insufficient_data";
            } else {
              std::cout << " classification=available"
                        << " bradycardia=" << classification.bradycardia
                        << " tachycardia=" << classification.tachycardia;
              if (classification.irregular_rr) {
                std::cout << " irregular_rr=" << *classification.irregular_rr
                          << " rr_cv="
                          << *classification.rr_coefficient_of_variation
                          << " rr_window="
                          << classification.rr_intervals_used;
              } else {
                std::cout << " irregular_rr=insufficient_data"
                          << " rr_window="
                          << classification.rr_intervals_used;
              }
            }
          }
          std::cout << '\n';
        }
      }
      return true;
    });
  } catch (const std::exception& error) {
    std::cerr << "ecg_playback: " << error.what() << '\n';
    return 1;
  }
}
