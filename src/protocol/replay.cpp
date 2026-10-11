#include "frame.hpp"
#include <chrono>

class ReplayGuard {
private:
    uint64_t last_seen_seq_id = 0;
    const uint64_t MAX_TIME_DELTA_US = 5000000;

    uint64_t get_current_timestamp_us() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }

public:
    bool validate_frame(const TelemetryFrame& frame) {
        if (frame.seq_id <= last_seen_seq_id) {
            return false;
        }

        uint64_t current_time = get_current_timestamp_us();
        if (current_time > frame.timestamp_us) {
            if (current_time - frame.timestamp_us > MAX_TIME_DELTA_US) {
                return false;
            }
        } else {
            if (frame.timestamp_us - current_time > MAX_TIME_DELTA_US) {
                return false;
            }
        }

        last_seen_seq_id = frame.seq_id;
        return true;
    }
};