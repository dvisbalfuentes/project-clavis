#ifndef ENVELOPE_HPP 
#define ENVELOPE_HPP 

#include <string>
#include <vector>
#include <cstdint>

constexpr size_t AES_KEY_SIZE = 32;
constexpr size_t IV_SIZE = 12;
constexpr size_t TAG_SIZE = 16;
constexpr size_t HMAC_KEY_SIZE = 32;

struct CryptoEnvelope { 
    std::vector<uint8_t> iv;
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;
    std::string mac_hex;
};

bool encrypt_aes_gcm(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    std::vector<uint8_t>& ciphertext,
    std::vector<uint8_t>& tag
);

std::string compute_hmac_sha256(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& hmac_key
);

#endif
