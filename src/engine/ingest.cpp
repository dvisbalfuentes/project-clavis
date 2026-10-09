#include "engine/ingest.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace clavis::engine {
namespace {

std::vector<std::string> words(const std::string& text) {
  std::istringstream input(text);
  std::vector<std::string> result;
  for (std::string word; input >> word;) result.push_back(word);
  return result;
}

double number_before(const std::string& text, char delimiter) {
  const auto at = text.find(delimiter);
  return std::stod(text.substr(0, at));
}

std::string unit_after(const std::string& text, char delimiter) {
  const auto at = text.find(delimiter);
  if (at == std::string::npos) return "mV";
  std::size_t end = at + 1;
  while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) ++end;
  return text.substr(at + 1, end - at - 1);
}

std::int16_t sign_extend_12(std::uint16_t value) {
  return static_cast<std::int16_t>((value & 0x800U) ? value | 0xF000U : value);
}

}  // namespace

MitBihReader::MitBihReader(const std::string& record_path) {
  namespace fs = std::filesystem;
  fs::path header_path(record_path);
  if (header_path.extension() != ".hea") header_path += ".hea";

  std::ifstream header(header_path);
  if (!header) throw std::runtime_error("Cannot open WFDB header: " + header_path.string());

  std::string line;
  if (!std::getline(header, line)) throw std::runtime_error("Empty WFDB header");
  const auto first = words(line);
  if (first.size() < 3) throw std::runtime_error("Malformed WFDB header line");
  const auto signal_count = static_cast<std::size_t>(std::stoul(first[1]));
  metadata_.sampling_rate_hz = number_before(first[2], '/');
  if (!std::isfinite(metadata_.sampling_rate_hz) ||
      metadata_.sampling_rate_hz <= 0.0)
    throw std::runtime_error("WFDB sampling rate must be positive and finite");
  if (first.size() >= 4) metadata_.sample_count = std::stoull(first[3]);

  std::vector<double> gains;
  std::vector<int> baselines;
  std::vector<fs::path> data_files;
  for (std::size_t channel = 0; channel < signal_count; ++channel) {
    if (!std::getline(header, line)) throw std::runtime_error("Missing WFDB signal specification");
    const auto spec = words(line);
    if (spec.size() < 3) throw std::runtime_error("Malformed WFDB signal specification");
    if (spec[1].rfind("212", 0) != 0)
      throw std::runtime_error("Only original MIT-BIH 212-format records are supported");
    data_files.push_back(header_path.parent_path() / spec[0]);
    gains.push_back(number_before(spec[2], '/'));
    if (gains.back() == 0.0) throw std::runtime_error("WFDB gain cannot be zero");
    // WFDB uses the parenthesized baseline in the gain field when present.
    // Otherwise, the ADC zero field (the fifth signal-spec field) is the
    // baseline.  MIT-BIH record 100 uses the latter form: 200 11 1024 ...
    baselines.push_back(spec.size() >= 5 ? std::stoi(spec[4]) : 0);
    const auto paren = spec[2].find('(');
    if (paren != std::string::npos) baselines.back() = std::stoi(spec[2].substr(paren + 1));
    const auto unit = unit_after(spec[2], '/');
    if (!unit.empty()) metadata_.voltage_unit = unit;
    metadata_.signal_names.push_back(spec.size() >= 8 ? spec.back() : "signal_" + std::to_string(channel));
  }
  if (signal_count != 2 || !std::all_of(data_files.begin(), data_files.end(),
      [&](const fs::path& path) { return path == data_files.front(); }))
    throw std::runtime_error("MIT-BIH 212 reader requires two signals in one .dat file");

  std::ifstream data(data_files.front(), std::ios::binary);
  if (!data) throw std::runtime_error("Cannot open WFDB data: " + data_files.front().string());
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(data)), {});
  if (bytes.size() % 3 != 0) throw std::runtime_error("Malformed 212 data: byte count is not divisible by 3");
  const std::size_t available = bytes.size() / 3;
  if (metadata_.sample_count == 0) metadata_.sample_count = available;
  if (metadata_.sample_count > available) throw std::runtime_error("WFDB header sample count exceeds data");
  channels_.assign(2, std::vector<double>(metadata_.sample_count));
  for (std::size_t i = 0; i < metadata_.sample_count; ++i) {
    const auto b0 = static_cast<std::uint16_t>(bytes[3 * i]);
    const auto b1 = static_cast<std::uint16_t>(bytes[3 * i + 1]);
    const auto b2 = static_cast<std::uint16_t>(bytes[3 * i + 2]);
    const auto raw0 = sign_extend_12(b0 | ((b1 & 0x0FU) << 8));
    const auto raw1 = sign_extend_12(b2 | ((b1 & 0xF0U) << 4));
    channels_[0][i] = (raw0 - baselines[0]) / gains[0];
    channels_[1][i] = (raw1 - baselines[1]) / gains[1];
  }
}

EcgSample MitBihReader::sample_at(std::size_t index) const {
  if (index >= metadata_.sample_count) throw std::out_of_range("ECG sample index out of range");
  EcgSample result{index, static_cast<double>(index) / metadata_.sampling_rate_hz, {}};
  result.voltage.reserve(channels_.size());
  for (const auto& channel : channels_) result.voltage.push_back(channel[index]);
  return result;
}

void play_realtime(const MitBihReader& reader, double playback_rate,
                   const std::function<bool(const EcgSample&)>& on_sample) {
  if (playback_rate < 0.0) throw std::invalid_argument("Playback rate cannot be negative");
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < reader.metadata().sample_count; ++i) {
    if (playback_rate > 0.0 && i > 0) {
      const auto elapsed = std::chrono::duration<double>(
          static_cast<double>(i) /
          (reader.metadata().sampling_rate_hz * playback_rate));
      std::this_thread::sleep_until(start +
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(elapsed));
    }
    if (!on_sample(reader.sample_at(i))) break;
  }
}

}  // namespace clavis::engine
