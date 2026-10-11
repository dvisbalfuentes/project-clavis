import base64
import json
import os
import socket
import time


def make_frame(seq_id):
    payload = {
        "seq_id": seq_id,
        "timestamp_us": int(time.time() * 1_000_000),
        "iv": base64.b64encode(os.urandom(12)).decode(),
        "ciphertext": base64.b64encode(os.urandom(32)).decode(),
        "mac": os.urandom(32).hex(),
    }
    return json.dumps(payload).encode() + b"\n"


with socket.create_connection(("127.0.0.1", 9000)) as sock:
    first = make_frame(101)
    sock.sendall(first)
    sock.sendall(make_frame(102))
    sock.sendall(first)
    sock.sendall(b"this is not json\n")

print("sent 4 lines: 2 good frames, 1 replay, 1 malformed")