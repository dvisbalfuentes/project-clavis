#include "engine/classifier.hpp"
#include "engine/ingest.hpp"
#include "engine/pan_tompkins.hpp"
#include "crypto/envelope.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: device_gateway <record-or-header> [playback-rate]\n";
        return 1;
    }
    
    double rate = (argc >= 3) ? std::stod(argv[2]) : 1.0;
    
    clavis::engine::MitBihReader reader(argv[1]);
    clavis::engine::PanTompkinsDetector detector(reader.metadata().sampling_rate_hz);
    clavis::engine::PhysiologicalClassifier classifier;
    
    clavis::crypto::Envelope crypto_engine; 
    uint64_t sequence_id = 1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Fatal: Failed to create socket\n";
        return 1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000); // Chesper's INGEST_PORT
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr); // Chesper's INGEST_HOST
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Fatal: Could not connect to Python gateway on port 9000\n";
        return 1;
    }
    std::cout << "[+] Connected to CLAVIS Ingestion Service.\n";

    clavis::engine::play_realtime(reader, rate, [&](const clavis::engine::EcgSample& sample) {
        const auto result = detector.process(sample.voltage.front());
        
        if (result.sufficient_data && result.peak_detected) {
            auto now = std::chrono::system_clock::now();
            auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();

            std::stringstream telemetry_ss;
            telemetry_ss << "{\"hr_bpm\":" << *result.heart_rate_bpm << "}";
            std::string plaintext_telemetry = telemetry_ss.str();

            std::string base64_iv;
            std::string base64_ciphertext;
            std::string hex_mac;
            
            crypto_engine.encrypt_and_sign(
                plaintext_telemetry, 
                base64_iv, 
                base64_ciphertext, 
                hex_mac
            );

            std::stringstream payload;
            payload << "{"
                    << "\"seq_id\":" << sequence_id << ","
                    << "\"timestamp_us\":" << timestamp_us << ","
                    << "\"iv\":\"" << base64_iv << "\","
                    << "\"ciphertext\":\"" << base64_ciphertext << "\","
                    << "\"mac\":\"" << hex_mac << "\""
                    << "}\n"; // Newline required by Chesper's reader.readline()

            std::string final_json = payload.str();
          
            if (send(sock, final_json.c_str(), final_json.length(), 0) < 0) {
                std::cerr << "[-] Connection lost. Dropping frame.\n";
                return false; 
            }
            
            std::cout << "[Tx] Sent encrypted frame: Seq " << sequence_id << "\n";
            sequence_id++;
        }
        return true; 
    });

    close(sock);
    return 0;
}
