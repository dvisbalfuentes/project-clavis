import os
import time
import hmac
import hashlib
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# Ephemeral keys for local testing
AES_KEY = os.urandom(32)
HMAC_KEY = os.urandom(32)

def generate_valid_frame(seq_id, payload=b"CLINICAL_TELEMETRY"):
    iv = os.urandom(12)
    aesgcm = AESGCM(AES_KEY)
    encrypted = aesgcm.encrypt(iv, payload, None)
    ciphertext = encrypted[:-16]
    
    timestamp = int(time.time() * 1000000) 
    mac = hmac.new(HMAC_KEY, ciphertext, hashlib.sha256).hexdigest()
    
    return {
        "seq_id": seq_id,
        "timestamp_us": timestamp,
        "iv": iv.hex(),
        "ciphertext": ciphertext.hex(),
        "mac": mac
    }

def simulate_bit_flip_attack(valid_frame):
    print("\n[!] ATTACK VECTOR 1: Bit-Flipping / MAC Corruption")
    corrupted_frame = valid_frame.copy()
    # Corrupt the first 4 characters of the HMAC
    corrupted_frame["mac"] = "dead" + valid_frame["mac"][4:]
    print(f"Transmitting Corrupted MAC: {corrupted_frame['mac']}")
    print("-> Expected Defense: OpenSSL verification fails, packet dropped.")

def simulate_replay_attack(valid_frame):
    print("\n[!] ATTACK VECTOR 2: Replay Attack Injection")
    print(f"Re-transmitting identical frame with Seq ID: {valid_frame['seq_id']}")
    print("-> Expected Defense: ReplayGuard triggers [Sequence ID Duplicate] warning.")

def simulate_command_spoofing():
    print("\n[!] ATTACK VECTOR 3: Command Spoofing")
    spoofed_payload = b"ADJUST_PACEMAKER_DOSAGE"
    # Encrypt with correct AES key but sign with a forged HMAC key
    forged_hmac_key = os.urandom(32) 
    iv = os.urandom(12)
    aesgcm = AESGCM(AES_KEY)
    
    encrypted = aesgcm.encrypt(iv, spoofed_payload, None)
    ciphertext = encrypted[:-16]
    forged_mac = hmac.new(forged_hmac_key, ciphertext, hashlib.sha256).hexdigest()
    
    print(f"Injecting spoofed therapy command with Forged HMAC: {forged_mac}")
    print("-> Expected Defense: Command handler rejects unauthenticated operator.")

if __name__ == "__main__":
    print("Initializing Project CLAVIS Security Harness...")
    captured_frame = generate_valid_frame(seq_id=10423)
    
    simulate_bit_flip_attack(captured_frame)
    simulate_replay_attack(captured_frame)
    simulate_command_spoofing()