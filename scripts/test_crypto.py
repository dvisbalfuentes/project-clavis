import os
import hmac
import hashlib
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

def test_python_crypto():
    # 1. Generate keys and IV matching C++ specs
    aes_key = os.urandom(32)  # 256-bit
    hmac_key = os.urandom(32)
    iv = os.urandom(12)       # 96-bit GCM IV
    
    plaintext = b"CLAVIS_TELEMETRY_PAYLOAD_TEST"
    
    # 2. AES-256-GCM Encryption
    aesgcm = AESGCM(aes_key)
    # AESGCM.encrypt appends the 16-byte tag to the end of the ciphertext automatically
    encrypted_data = aesgcm.encrypt(iv, plaintext, None)
    
    ciphertext = encrypted_data[:-16]
    tag = encrypted_data[-16:]
    
    # 3. HMAC-SHA256 Generation
    # In practice, this will cover the sequence ID, timestamp, and ciphertext
    mac = hmac.new(hmac_key, ciphertext, hashlib.sha256).hexdigest()
    
    print("--- Python Crypto Verification ---")
    print(f"Plaintext:  {plaintext.decode()}")
    print(f"Ciphertext: {ciphertext.hex()}")
    print(f"GCM Tag:    {tag.hex()}")
    print(f"HMAC:       {mac}")
    
    # 4. Decryption Check
    decrypted_data = aesgcm.decrypt(iv, ciphertext + tag, None)
    assert decrypted_data == plaintext, "Decryption failed!"
    print("\nSUCCESS: Python encryption and decryption cycle verified.")

if __name__ == "__main__":
    test_python_crypto()