#include "engine/classifier.hpp"

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace clavis::engine {

PhysiologicalClassifier::PhysiologicalClassifier(ClassifierConfig config)
    : config_(config) {
  if (!std::isfinite(config_.bradycardia_bpm) ||
      !std::isfinite(config_.tachycardia_bpm) ||
      config_.bradycardia_bpm <= 0.0 ||
      config_.tachycardia_bpm <= config_.bradycardia_bpm)
    throw std::invalid_argument("Classifier BPM thresholds are invalid");
  if (config_.irregularity_window < 2 ||
      config_.irregularity_min_intervals < 2 ||
      config_.irregularity_min_intervals > config_.irregularity_window)
    throw std::invalid_argument("Classifier RR window configuration is invalid");
  if (!std::isfinite(config_.irregularity_cv_threshold) ||
      config_.irregularity_cv_threshold < 0.0)
    throw std::invalid_argument("Classifier RR irregularity threshold is invalid");
}

ClassificationResult PhysiologicalClassifier::process(
    std::optional<double> rr_interval_seconds,
    std::optional<double> heart_rate_bpm) {
  ClassificationResult result;
  if (!rr_interval_seconds || !heart_rate_bpm ||
      !std::isfinite(*rr_interval_seconds) ||
      !std::isfinite(*heart_rate_bpm) ||
      *rr_interval_seconds <= 0.0 || *heart_rate_bpm <= 0.0)
    return result;

  result.status = ClassificationStatus::available;
  result.bradycardia = *heart_rate_bpm < config_.bradycardia_bpm;
  result.tachycardia = *heart_rate_bpm > config_.tachycardia_bpm;

  rr_history_.push_back(*rr_interval_seconds);
  while (rr_history_.size() > config_.irregularity_window)
    rr_history_.pop_front();
  result.rr_intervals_used = rr_history_.size();
  if (rr_history_.size() < config_.irregularity_min_intervals)
    return result;

  const double mean = std::accumulate(rr_history_.begin(), rr_history_.end(),
                                      0.0) /
                      static_cast<double>(rr_history_.size());
  double squared_deviation_sum = 0.0;
  for (double rr : rr_history_) {
    const double deviation = rr - mean;
    squared_deviation_sum += deviation * deviation;
  }
  const double standard_deviation = std::sqrt(
      squared_deviation_sum / static_cast<double>(rr_history_.size() - 1));
  const double coefficient_of_variation = standard_deviation / mean;
  result.rr_coefficient_of_variation = coefficient_of_variation;
  result.irregular_rr =
      coefficient_of_variation >= config_.irregularity_cv_threshold;
  return result;
}

void PhysiologicalClassifier::reset() noexcept {
  rr_history_.clear();
}

}  // namespace clavis::engine
