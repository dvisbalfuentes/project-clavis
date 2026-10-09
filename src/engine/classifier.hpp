#pragma once

#include <cstddef>
#include <deque>
#include <optional>

namespace clavis::engine {

struct ClassifierConfig {
  double bradycardia_bpm{50.0};
  double tachycardia_bpm{100.0};
  std::size_t irregularity_window{8};
  std::size_t irregularity_min_intervals{4};
  double irregularity_cv_threshold{0.10};
};

enum class ClassificationStatus {
  insufficient_data,
  available,
};

struct ClassificationResult {
  ClassificationStatus status{ClassificationStatus::insufficient_data};
  bool bradycardia{};
  bool tachycardia{};
  std::optional<bool> irregular_rr;
  std::size_t rr_intervals_used{};
  std::optional<double> rr_coefficient_of_variation;
};

// Prototype physiological rule engine. It flags measurements; it does not
// diagnose arrhythmia or any other clinical condition.
class PhysiologicalClassifier {
 public:
  explicit PhysiologicalClassifier(ClassifierConfig config = {});

  [[nodiscard]] ClassificationResult process(
      std::optional<double> rr_interval_seconds,
      std::optional<double> heart_rate_bpm);
  void reset() noexcept;
  [[nodiscard]] const ClassifierConfig& config() const noexcept { return config_; }

 private:
  ClassifierConfig config_;
  std::deque<double> rr_history_;
};

}  // namespace clavis::engine
