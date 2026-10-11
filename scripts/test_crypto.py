import os
import hmac
import hashlib
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

def test_python_crypto():
    aes_key = os.urandom(32)  # 256-bit
    hmac_key = os.urandom(32)
    iv = os.urandom(12)       # 96-bit GCM IV
    
    plaintext = b"CLAVIS_TELEMETRY_PAYLOAD_TEST"
    
    aesgcm = AESGCM(aes_key)
    encrypted_data = aesgcm.encrypt(iv, plaintext, None)
    
    ciphertext = encrypted_data[:-16]
    tag = encrypted_data[-16:]
    
    mac = hmac.new(hmac_key, ciphertext, hashlib.sha256).hexdigest()
    
    print("--- Python Crypto Verification ---")
    print(f"Plaintext:  {plaintext.decode()}")
    print(f"Ciphertext: {ciphertext.hex()}")
    print(f"GCM Tag:    {tag.hex()}")
    print(f"HMAC:       {mac}")
    
    decrypted_data = aesgcm.decrypt(iv, ciphertext + tag, None)
    assert decrypted_data == plaintext, "Decryption failed!"
    print("\nSUCCESS: Python encryption and decryption cycle verified.")

if __name__ == "__main__":
    test_python_crypto()
