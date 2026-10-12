#include "engine/ingest.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void append_pair(std::ofstream& output, int first, int second) {
  const auto a = static_cast<unsigned int>(first) & 0xFFFU;
  const auto b = static_cast<unsigned int>(second) & 0xFFFU;
  output.put(static_cast<char>(a & 0xFFU));
  output.put(static_cast<char>(((b >> 8) << 4) | (a >> 8)));
  output.put(static_cast<char>(b & 0xFFU));
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto directory = fs::temp_directory_path() / "clavis_ingest_test";
  fs::create_directories(directory);
  const auto stem = directory / "synthetic";
  {
    std::ofstream header(stem.string() + ".hea");
    header << "synthetic 2 360 2\n"
           << "synthetic.dat 212 200/mV 12 100 0 0 0 I\n"
           << "synthetic.dat 212 200/mV 12 -100 0 0 0 II\n";
    std::ofstream data(stem.string() + ".dat", std::ios::binary);
    append_pair(data, 200, -100);
    append_pair(data, 400, -200);
  }
  clavis::engine::MitBihReader reader(stem.string());
  assert(reader.metadata().sampling_rate_hz == 360.0);
  assert(reader.metadata().voltage_unit == "mV");
  assert(reader.metadata().sample_count == 2);
  const auto second = reader.sample_at(1);
  assert(second.index == 1 && second.time_seconds == 1.0 / 360.0);
  assert(second.voltage[0] == 1.5 && second.voltage[1] == -0.5);
  std::size_t delivered = 0;
  clavis::engine::play_realtime(reader, 0.0, [&](const auto&) { return ++delivered < 2; });
  assert(delivered == 2);

  // At 0.01x, two 360 Hz samples span one interval of about 0.278 s.
  // This checks pacing independently from the sample decoding assertions.
  delivered = 0;
  const auto playback_start = std::chrono::steady_clock::now();
  clavis::engine::play_realtime(reader, 0.01,
      [&](const auto&) { ++delivered; return true; });
  const std::chrono::duration<double> playback_elapsed =
      std::chrono::steady_clock::now() - playback_start;
  assert(delivered == 2);
  assert(playback_elapsed.count() >= 0.20 && playback_elapsed.count() < 0.45);

  fs::remove_all(directory);
  std::cout << "ingest_test passed\n";
}
