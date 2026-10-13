#ifndef ENVELOPE_HPP 
#define ENVELOPE_HPP 

#include <string>
#include <vector>
#include <cstdint>

namespace clavis {
namespace crypto {

    constexpr size_t AES_KEY_SIZE = 32;
    constexpr size_t IV_SIZE = 12;
    constexpr size_t TAG_SIZE = 16;
    constexpr size_t HMAC_KEY_SIZE = 32;

    class Envelope { 
    public:
        bool encrypt_and_sign(
            const std::string& plaintext,
            std::string& base64_iv,
            std::string& base64_ciphertext,
            std::string& hex_mac
        );

private:
        bool encrypt_aes_gcm(
            const std::vector<uint8_t>& plaintext,
            const std::vector<uint8_t>& key,
            const std::vector<uint8_t>& iv,
            std::vector<uint8_t>& ciphertext,
            std::vector<uint8_t>& tag
        );

        bool decrypt_aes_gcm(
            const std::vector<uint8_t>& ciphertext,
            const std::vector<uint8_t>& key,
            const std::vector<uint8_t>& iv,
            const std::vector<uint8_t>& tag,
            std::vector<uint8_t>& plaintext
        );

        std::string compute_hmac_sha256(
            const std::vector<uint8_t>& data,
            const std::vector<uint8_t>& hmac_key
        );
    };

}
} 

#endif
