#include "engine/pan_tompkins.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

std::vector<double> synthetic_ecg(double sampling_rate_hz, double seconds,
                                  const std::vector<double>& beat_times) {
  const auto count = static_cast<std::size_t>(sampling_rate_hz * seconds);
  std::vector<double> samples(count, 0.0);
  for (std::size_t index = 0; index < count; ++index) {
    const double time = static_cast<double>(index) / sampling_rate_hz;
    samples[index] = 0.02 * std::sin(2.0 * 3.141592653589793 * 0.5 * time);
    for (double beat : beat_times) {
      const double offset = time - beat;
      samples[index] += 1.2 * std::exp(-0.5 * offset * offset /
                                      (0.012 * 0.012));
    }
  }
  return samples;
}

}  // namespace

int main() {
  constexpr double sampling_rate_hz = 360.0;

  bool invalid_rate_rejected = false;
  try {
    clavis::engine::PanTompkinsDetector invalid(36.0);
  } catch (const std::invalid_argument&) {
    invalid_rate_rejected = true;
  }
  assert(invalid_rate_rejected);
  invalid_rate_rejected = false;
  try {
    clavis::engine::PanTompkinsDetector invalid(
        std::numeric_limits<double>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    invalid_rate_rejected = true;
  }
  assert(invalid_rate_rejected);

  const std::vector<double> beat_times{0.8, 1.8, 2.8, 3.8, 4.8, 5.8};
  const auto samples = synthetic_ecg(sampling_rate_hz, 6.5, beat_times);
  clavis::engine::PanTompkinsDetector detector(sampling_rate_hz);

  std::vector<clavis::engine::QrsResult> detections;
  for (double sample : samples) {
    const auto result = detector.process(sample);
    if (result.peak_detected) detections.push_back(result);
  }

  // The first two seconds train the adaptive threshold. Remaining beats
  // should be localized close to their known synthetic R-peak positions.
  assert(detections.size() == 4);
  for (std::size_t i = 0; i < detections.size(); ++i) {
    const auto expected = static_cast<std::size_t>(
        std::llround(beat_times[i + 2] * sampling_rate_hz));
    const auto error = detections[i].r_peak_index > expected
        ? detections[i].r_peak_index - expected
        : expected - detections[i].r_peak_index;
    assert(error <= static_cast<std::size_t>(0.050 * sampling_rate_hz));
    assert(detections[i].detection_sample_index >= detections[i].r_peak_index);
    assert(detections[i].processing_delay_seconds > 0.0);
  }

  // One detected beat is insufficient for RR and heart rate; the second is
  // sufficient and should describe the one-second synthetic RR interval.
  assert(!detections.front().sufficient_data);
  assert(!detections.front().rr_interval_seconds.has_value());
  assert(detections[1].sufficient_data);
  assert(std::abs(*detections[1].rr_interval_seconds - 1.0) < 0.05);
  assert(std::abs(*detections[1].heart_rate_bpm - 60.0) < 3.0);

  // The 200 ms refractory period must prevent duplicate detections for each
  // broadened synthetic QRS complex.
  for (std::size_t i = 1; i < detections.size(); ++i)
    assert(detections[i].r_peak_index - detections[i - 1].r_peak_index >= 72);

  std::cout << "pan_tompkins_test passed\n";
}
