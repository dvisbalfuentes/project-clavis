#ifndef FRAME_HPP
#define FRAME_HPP
#include <cstdint>
#include <vector>
#include <string>

struct TelemetryFrame {
    uint64_t seq_id;          
    uint64_t timestamp_us;    
    std::vector<uint8_t> iv;  
    std::vector<uint8_t> ciphertext; 
    std::string mac;         
};

#endif