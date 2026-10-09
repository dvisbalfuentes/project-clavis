#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace clavis::engine {

struct QrsResult {
  bool peak_detected{};
  std::size_t r_peak_index{};
  std::size_t detection_sample_index{};
  double processing_delay_seconds{};
  std::optional<double> rr_interval_seconds;
  std::optional<double> heart_rate_bpm;
  bool sufficient_data{};
};

// Streaming Pan-Tompkins-style QRS detector. One call consumes one physical
// ECG sample. Filter coefficients and all time windows are derived from the
// supplied sampling rate; the intended and validated rate is 360 Hz.
class PanTompkinsDetector {
 public:
  explicit PanTompkinsDetector(double sampling_rate_hz);

  [[nodiscard]] QrsResult process(double voltage);
  [[nodiscard]] double sampling_rate_hz() const noexcept { return sampling_rate_hz_; }
  [[nodiscard]] std::size_t startup_samples() const noexcept { return startup_samples_; }

 private:
  struct Biquad {
    double b0{};
    double b1{};
    double b2{};
    double a1{};
    double a2{};
    double z1{};
    double z2{};

    double process(double input);
  };

  static Biquad lowpass(double sampling_rate_hz, double cutoff_hz);
  static Biquad highpass(double sampling_rate_hz, double cutoff_hz);
  void initialize_thresholds();
  std::size_t locate_r_peak(std::size_t integrated_peak_index) const;
  void update_threshold();

  double sampling_rate_hz_{};
  std::size_t sample_index_{};
  std::size_t integration_window_{};
  std::size_t refractory_samples_{};
  std::size_t startup_samples_{};
  Biquad highpass_;
  Biquad lowpass_;
  std::array<double, 5> derivative_history_{};
  std::deque<double> squared_window_;
  double squared_sum_{};
  double integration_two_back_{};
  double integration_one_back_{};
  std::deque<std::pair<std::size_t, double>> filtered_history_;
  std::vector<double> startup_peak_values_;
  bool thresholds_initialized_{};
  double signal_level_{};
  double noise_level_{};
  double threshold_{};
  std::optional<std::size_t> last_r_peak_;
  std::size_t detected_peak_count_{};
};

}  // namespace clavis::engine
