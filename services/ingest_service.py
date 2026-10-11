import asyncio
import base64
import binascii
import json
import logging
import os
from pathlib import Path

import asyncpg
from dotenv import load_dotenv

load_dotenv(Path(__file__).resolve().parent.parent / "infra" / ".env")

HOST = os.getenv("INGEST_HOST", "127.0.0.1")
PORT = int(os.getenv("INGEST_PORT", "9000"))
DEVICE_ID = os.getenv("DEVICE_ID", "sim-001")
BATCH_SIZE = int(os.getenv("BATCH_SIZE", "100"))
FLUSH_INTERVAL = float(os.getenv("FLUSH_INTERVAL", "0.5"))
QUEUE_MAX = 10000
MAX_LINE = 65536

DB_HOST = os.getenv("DB_HOST", "127.0.0.1")
DB_PORT = int(os.getenv("DB_PORT", "5432"))
DB_USER = os.getenv("POSTGRES_USER", "clavis")
DB_PASSWORD = os.getenv("POSTGRES_PASSWORD", "")
DB_NAME = os.getenv("POSTGRES_DB", "clavis")

REQUIRED_FIELDS = ("seq_id", "timestamp_us", "iv", "ciphertext", "mac")

INSERT_FRAMES_SQL = """
INSERT INTO ecg_telemetry (device_id, seq_id, ts, ts_us, iv, ciphertext, mac)
SELECT $1, t.seq_id, to_timestamp((t.ts_us / 1000000.0)::double precision),
       t.ts_us, t.iv, t.ct, t.mac
FROM unnest($2::bigint[], $3::bigint[], $4::bytea[], $5::bytea[], $6::bytea[])
     AS t(seq_id, ts_us, iv, ct, mac)
ON CONFLICT (device_id, seq_id) DO NOTHING
RETURNING seq_id
"""

INSERT_AUDIT_SQL = """
INSERT INTO audit_logs (severity, event_type, seq_id, details)
VALUES ($1, $2, $3, $4::jsonb)
"""

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("ingest")


def parse_frame(line):
    try:
        obj = json.loads(line)
    except (json.JSONDecodeError, UnicodeDecodeError):
        raise ValueError("invalid JSON")

    if not isinstance(obj, dict):
        raise ValueError("frame must be a JSON object")

    missing = [f for f in REQUIRED_FIELDS if f not in obj]
    if missing:
        raise ValueError("missing fields: " + ", ".join(missing))

    seq_id = obj["seq_id"]
    if isinstance(seq_id, bool) or not isinstance(seq_id, int) or seq_id < 0:
        raise ValueError("seq_id must be a non-negative integer")

    ts_us = obj["timestamp_us"]
    if isinstance(ts_us, bool) or not isinstance(ts_us, int) or ts_us <= 0:
        raise ValueError("timestamp_us must be a positive integer")

    try:
        iv = base64.b64decode(obj["iv"], validate=True)
        ciphertext = base64.b64decode(obj["ciphertext"], validate=True)
    except (binascii.Error, TypeError, ValueError):
        raise ValueError("iv and ciphertext must be valid base64")
    if len(iv) != 12:
        raise ValueError("iv must decode to 12 bytes")
    if not ciphertext:
        raise ValueError("ciphertext is empty")

    try:
        mac = bytes.fromhex(obj["mac"])
    except (TypeError, ValueError):
        raise ValueError("mac must be a hex string")
    if len(mac) != 32:
        raise ValueError("mac must decode to 32 bytes")

    return seq_id, ts_us, iv, ciphertext, mac


async def flush(pool, batch):
    frames = [item[1] for item in batch if item[0] == "frame"]
    audits = [item[1:] for item in batch if item[0] == "audit"]

    async with pool.acquire() as conn:
        if frames:
            rows = await conn.fetch(
                INSERT_FRAMES_SQL,
                DEVICE_ID,
                [f[0] for f in frames],
                [f[1] for f in frames],
                [f[2] for f in frames],
                [f[3] for f in frames],
                [f[4] for f in frames],
            )
            remaining = [f[0] for f in frames]
            for row in rows:
                remaining.remove(row["seq_id"])
            for seq_id in remaining:
                audits.append(
                    ("CRITICAL", "REPLAY_DETECTED", seq_id,
                     {"reason": "duplicate seq_id for device"})
                )
            log.info("stored %d frames, rejected %d duplicates", len(rows), len(remaining))

        if audits:
            await conn.executemany(
                INSERT_AUDIT_SQL,
                [(sev, evt, seq, json.dumps(det)) for sev, evt, seq, det in audits],
            )


async def writer(pool, queue):
    loop = asyncio.get_running_loop()
    while True:
        batch = [await queue.get()]
        deadline = loop.time() + FLUSH_INTERVAL
        while len(batch) < BATCH_SIZE:
            timeout = deadline - loop.time()
            if timeout <= 0:
                break
            try:
                batch.append(await asyncio.wait_for(queue.get(), timeout))
            except asyncio.TimeoutError:
                break
        try:
            await flush(pool, batch)
        except Exception:
            log.exception("batch insert failed, %d items dropped", len(batch))
        finally:
            for _ in batch:
                queue.task_done()


async def handle_client(reader, writer_stream, queue):
    peer = str(writer_stream.get_extra_info("peername"))
    log.info("client connected: %s", peer)
    try:
        while True:
            try:
                line = await reader.readline()
            except ValueError:
                log.warning("oversized line from %s, closing", peer)
                await queue.put(
                    ("audit", "WARN", "MALFORMED_FRAME", None,
                     {"reason": "line too long", "peer": peer})
                )
                break
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            try:
                frame = parse_frame(line)
            except ValueError as exc:
                await queue.put(
                    ("audit", "WARN", "MALFORMED_FRAME", None,
                     {"reason": str(exc), "peer": peer})
                )
                continue
            await queue.put(("frame", frame))
    except ConnectionError:
        pass
    finally:
        writer_stream.close()
        log.info("client disconnected: %s", peer)


async def main():
    pool = await asyncpg.create_pool(
        host=DB_HOST, port=DB_PORT, user=DB_USER,
        password=DB_PASSWORD, database=DB_NAME,
        min_size=1, max_size=4,
    )
    queue = asyncio.Queue(maxsize=QUEUE_MAX)
    writer_task = asyncio.create_task(writer(pool, queue))

    async def on_connect(reader, writer_stream):
        await handle_client(reader, writer_stream, queue)

    server = await asyncio.start_server(on_connect, HOST, PORT, limit=MAX_LINE)
    log.info("listening on %s:%d", HOST, PORT)

    try:
        async with server:
            await server.serve_forever()
    finally:
        await queue.join()
        writer_task.cancel()
        await pool.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass