#include "envelope.hpp" 
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include <iomanip>
#include <sstream>
#include <iostream>

bool encrypt_aes_gcm( 
    const std::vector<uint8_t>& plaintext, 
    const std::vector<uint8_t>& key, 
    const std::vector<uint8_t>& iv, 
    std::vector<uint8_t>& ciphertext, 
    std::vector<uint8_t>& tag 
) { 
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new(); 
    if (!ctx) return false; 
    
    int len = 0; 
    int ciphertext_len = 0; 
    ciphertext.resize(plaintext.size()); 
    tag.resize(TAG_SIZE); 
    
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), NULL)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    
    const uint8_t* key_ptr = key.empty() ? nullptr : key.data(); 
    const uint8_t* iv_ptr = iv.empty() ? nullptr : iv.data(); 
    
    if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key_ptr, iv_ptr)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    
    uint8_t* cipher_ptr = ciphertext.empty() ? nullptr : ciphertext.data(); 
    const uint8_t* plain_ptr = plaintext.empty() ? nullptr : plaintext.data(); 
    
    if (1 != EVP_EncryptUpdate(ctx, cipher_ptr, &len, plain_ptr, plaintext.size())) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    ciphertext_len = len; 
    
    if (1 != EVP_EncryptFinal_ex(ctx, cipher_ptr + len, &len)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    ciphertext_len += len; 
    ciphertext.resize(ciphertext_len); 
    
    uint8_t* tag_ptr = tag.empty() ? nullptr : tag.data(); 
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag_ptr)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    
    EVP_CIPHER_CTX_free(ctx); 
    return true; 
} 

bool decrypt_aes_gcm( 
    const std::vector<uint8_t>& ciphertext, 
    const std::vector<uint8_t>& key, 
    const std::vector<uint8_t>& iv, 
    const std::vector<uint8_t>& tag, 
    std::vector<uint8_t>& plaintext 
) { 
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new(); 
    if (!ctx) return false; 
    
    int len = 0; 
    int plaintext_len = 0; 
    plaintext.resize(ciphertext.size()); 
    
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), NULL)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    
    const uint8_t* key_ptr = key.empty() ? nullptr : key.data(); 
    const uint8_t* iv_ptr = iv.empty() ? nullptr : iv.data(); 
    
    if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, key_ptr, iv_ptr)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    
    uint8_t* plain_ptr = plaintext.empty() ? nullptr : plaintext.data(); 
    const uint8_t* cipher_ptr = ciphertext.empty() ? nullptr : ciphertext.data(); 
    
    if (1 != EVP_DecryptUpdate(ctx, plain_ptr, &len, cipher_ptr, ciphertext.size())) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    plaintext_len = len; 
    
    const uint8_t* tag_ptr = tag.empty() ? nullptr : tag.data(); 
    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, (void*)tag_ptr)) { 
        EVP_CIPHER_CTX_free(ctx); 
        return false; 
    } 
    
    int ret = EVP_DecryptFinal_ex(ctx, plain_ptr + len, &len); 
    EVP_CIPHER_CTX_free(ctx); 
    
    if (ret > 0) { 
        plaintext_len += len; 
        plaintext.resize(plaintext_len); 
        return true; 
    } else { 
        return false; 
    } 
} 

std::string compute_hmac_sha256( 
    const std::vector<uint8_t>& data, 
    const std::vector<uint8_t>& hmac_key 
) { 
    unsigned char hash[EVP_MAX_MD_SIZE]; 
    unsigned int hmac_len = 0; 
    
    const uint8_t* key_ptr = hmac_key.empty() ? nullptr : hmac_key.data(); 
    const uint8_t* data_ptr = data.empty() ? nullptr : data.data(); 
    
    HMAC(EVP_sha256(), key_ptr, hmac_key.size(), data_ptr, data.size(), hash, &hmac_len); 
    
    std::stringstream ss; 
    for (unsigned int i = 0; i < hmac_len; i++) { 
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i]; 
    } 
    return ss.str(); 
}
