#include "engine/pan_tompkins.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace clavis::engine {
namespace {

constexpr double kHighpassHz = 5.0;
constexpr double kLowpassHz = 18.0;
constexpr double kButterworthQ = 0.7071067811865476;
constexpr double kIntegrationSeconds = 0.150;
constexpr double kRefractorySeconds = 0.200;
constexpr double kStartupSeconds = 2.0;

double validated_sampling_rate(double sampling_rate_hz) {
  if (!std::isfinite(sampling_rate_hz) || sampling_rate_hz <= 2.0 * kLowpassHz)
    throw std::invalid_argument("QRS detector sampling rate must exceed 36 Hz");
  return sampling_rate_hz;
}

std::size_t samples_for(double seconds, double sampling_rate_hz) {
  return std::max<std::size_t>(1, static_cast<std::size_t>(
      std::llround(seconds * sampling_rate_hz)));
}

}  // namespace

double PanTompkinsDetector::Biquad::process(double input) {
  const double output = b0 * input + z1;
  z1 = b1 * input - a1 * output + z2;
  z2 = b2 * input - a2 * output;
  return output;
}

PanTompkinsDetector::Biquad PanTompkinsDetector::lowpass(
    double sampling_rate_hz, double cutoff_hz) {
  const double omega = 2.0 * std::numbers::pi * cutoff_hz / sampling_rate_hz;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * kButterworthQ);
  const double a0 = 1.0 + alpha;
  return {(1.0 - cosine) / (2.0 * a0), (1.0 - cosine) / a0,
          (1.0 - cosine) / (2.0 * a0), -2.0 * cosine / a0,
          (1.0 - alpha) / a0};
}

PanTompkinsDetector::Biquad PanTompkinsDetector::highpass(
    double sampling_rate_hz, double cutoff_hz) {
  const double omega = 2.0 * std::numbers::pi * cutoff_hz / sampling_rate_hz;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * kButterworthQ);
  const double a0 = 1.0 + alpha;
  return {(1.0 + cosine) / (2.0 * a0), -(1.0 + cosine) / a0,
          (1.0 + cosine) / (2.0 * a0), -2.0 * cosine / a0,
          (1.0 - alpha) / a0};
}

PanTompkinsDetector::PanTompkinsDetector(double sampling_rate_hz)
    : sampling_rate_hz_(validated_sampling_rate(sampling_rate_hz)),
      integration_window_(samples_for(kIntegrationSeconds, sampling_rate_hz_)),
      refractory_samples_(samples_for(kRefractorySeconds, sampling_rate_hz_)),
      startup_samples_(samples_for(kStartupSeconds, sampling_rate_hz_)),
      highpass_(highpass(sampling_rate_hz_, kHighpassHz)),
      lowpass_(lowpass(sampling_rate_hz_, kLowpassHz)) {}

void PanTompkinsDetector::initialize_thresholds() {
  if (startup_peak_values_.empty()) {
    signal_level_ = 1.0;
    noise_level_ = 0.0;
  } else {
    std::sort(startup_peak_values_.begin(), startup_peak_values_.end());
    const auto percentile = [&](double fraction) {
      const auto position = static_cast<std::size_t>(
          fraction * static_cast<double>(startup_peak_values_.size() - 1));
      return startup_peak_values_[position];
    };
    noise_level_ = percentile(0.50);
    signal_level_ = percentile(0.90);
    if (signal_level_ <= noise_level_)
      signal_level_ = startup_peak_values_.back();
  }
  update_threshold();
  thresholds_initialized_ = true;
}

void PanTompkinsDetector::update_threshold() {
  threshold_ = noise_level_ + 0.25 * (signal_level_ - noise_level_);
}

std::size_t PanTompkinsDetector::locate_r_peak(
    std::size_t integrated_peak_index) const {
  const std::size_t first = integrated_peak_index > integration_window_
      ? integrated_peak_index - integration_window_
      : 0;
  std::size_t best_index = integrated_peak_index;
  double best_magnitude = -1.0;
  for (const auto& [index, value] : filtered_history_) {
    if (index < first || index > integrated_peak_index) continue;
    const double magnitude = std::abs(value);
    if (magnitude > best_magnitude) {
      best_magnitude = magnitude;
      best_index = index;
    }
  }
  return best_index;
}

QrsResult PanTompkinsDetector::process(double voltage) {
  if (!std::isfinite(voltage))
    throw std::invalid_argument("ECG sample must be finite");

  const std::size_t current_index = sample_index_++;
  const double filtered = lowpass_.process(highpass_.process(voltage));
  filtered_history_.emplace_back(current_index, filtered);
  const std::size_t history_length = integration_window_ + refractory_samples_;
  while (!filtered_history_.empty() &&
         filtered_history_.front().first + history_length < current_index)
    filtered_history_.pop_front();

  for (std::size_t i = derivative_history_.size() - 1; i > 0; --i)
    derivative_history_[i] = derivative_history_[i - 1];
  derivative_history_[0] = filtered;
  const double derivative = sampling_rate_hz_ / 8.0 *
      (2.0 * derivative_history_[0] + derivative_history_[1] -
       derivative_history_[3] - 2.0 * derivative_history_[4]);
  const double squared = derivative * derivative;
  squared_window_.push_back(squared);
  squared_sum_ += squared;
  if (squared_window_.size() > integration_window_) {
    squared_sum_ -= squared_window_.front();
    squared_window_.pop_front();
  }
  const double integrated = squared_sum_ /
      static_cast<double>(integration_window_);

  QrsResult result;
  result.sufficient_data = detected_peak_count_ >= 2;
  if (current_index < 2) {
    integration_two_back_ = integration_one_back_;
    integration_one_back_ = integrated;
    return result;
  }

  const bool local_peak = integration_one_back_ > integration_two_back_ &&
                          integration_one_back_ >= integrated;
  const std::size_t candidate_index = current_index - 1;
  const double candidate_value = integration_one_back_;
  integration_two_back_ = integration_one_back_;
  integration_one_back_ = integrated;

  if (!thresholds_initialized_) {
    if (local_peak) startup_peak_values_.push_back(candidate_value);
    if (current_index >= startup_samples_) initialize_thresholds();
    return result;
  }
  if (!local_peak) return result;

  const std::size_t r_peak = locate_r_peak(candidate_index);
  const bool outside_refractory = !last_r_peak_ ||
      r_peak >= *last_r_peak_ + refractory_samples_;
  if (candidate_value >= threshold_ && outside_refractory) {
    signal_level_ = 0.125 * candidate_value + 0.875 * signal_level_;
    result.peak_detected = true;
    result.r_peak_index = r_peak;
    result.detection_sample_index = current_index;
    result.processing_delay_seconds =
        static_cast<double>(current_index - r_peak) / sampling_rate_hz_;
    if (last_r_peak_) {
      const double rr = static_cast<double>(r_peak - *last_r_peak_) /
                        sampling_rate_hz_;
      result.rr_interval_seconds = rr;
      result.heart_rate_bpm = 60.0 / rr;
      result.sufficient_data = true;
    }
    last_r_peak_ = r_peak;
    ++detected_peak_count_;
    result.sufficient_data = detected_peak_count_ >= 2;
  } else {
    noise_level_ = 0.125 * candidate_value + 0.875 * noise_level_;
  }
  update_threshold();
  return result;
}

}  // namespace clavis::engine
