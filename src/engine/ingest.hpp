#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace clavis::engine {

// Metadata retained from a MIT-BIH WFDB header.  The original MIT-BIH
// Arrhythmia Database uses 212-packed samples and a 360 Hz sampling rate.
struct EcgMetadata {
  double sampling_rate_hz{};
  std::string voltage_unit{"mV"};
  std::vector<std::string> signal_names;
  std::size_t sample_count{};
};

struct EcgSample {
  std::size_t index{};
  double time_seconds{};
  std::vector<double> voltage;
};

// Reads an original MIT-BIH/WFDB record described by <record>.hea and its
// 212-format .dat file(s).  Pass either a record stem or the .hea path.
class MitBihReader {
 public:
  explicit MitBihReader(const std::string& record_path);

  [[nodiscard]] const EcgMetadata& metadata() const noexcept { return metadata_; }
  [[nodiscard]] EcgSample sample_at(std::size_t index) const;

 private:
  EcgMetadata metadata_;
  std::vector<std::vector<double>> channels_;
};

// Delivers a record in sample order.  A zero playback rate runs as fast as
// possible; a positive rate is a multiplier of the record's native timing.
void play_realtime(const MitBihReader& reader, double playback_rate,
                   const std::function<bool(const EcgSample&)>& on_sample);

}  // namespace clavis::engine
