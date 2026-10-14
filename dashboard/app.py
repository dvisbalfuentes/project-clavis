import argparse
import itertools
import logging
import math
import os
import random
import time
from datetime import datetime, timezone
from pathlib import Path

import psycopg2
from dotenv import load_dotenv
from flask import Flask, jsonify, render_template
from flask_socketio import SocketIO, emit

load_dotenv(Path(__file__).resolve().parent.parent / "infra" / ".env")

DB_SETTINGS = {
    "host": os.getenv("DB_HOST", "127.0.0.1"),
    "port": int(os.getenv("DB_PORT", "5432")),
    "user": os.getenv("POSTGRES_USER", "clavis"),
    "password": os.getenv("POSTGRES_PASSWORD", ""),
    "dbname": os.getenv("POSTGRES_DB", "clavis"),
}

SAMPLE_RATE_HZ = 360
TICK_SECONDS = 0.25
FRAME_FETCH_LIMIT = 2000
HISTORY_LIMIT = 20
BRADYCARDIA_BPM = 50
TACHYCARDIA_BPM = 100

FRAME_SQL = (
    "SELECT id, seq_id, voltage_mv, bpm, rr_interval_ms, classification "
    "FROM ecg_telemetry WHERE id > %s ORDER BY id LIMIT " + str(FRAME_FETCH_LIMIT)
)
ALERT_FILTER = "(severity IN ('WARN','CRITICAL') OR event_type = 'ANOMALY_PHYSIOLOGICAL')"
ALERT_SQL = (
    "SELECT id, ts, severity, event_type, seq_id, details FROM audit_logs "
    "WHERE id > %s AND " + ALERT_FILTER + " ORDER BY id LIMIT 200"
)
HISTORY_SQL = (
    "SELECT id, ts, severity, event_type, seq_id, details FROM audit_logs "
    "WHERE " + ALERT_FILTER + " ORDER BY id DESC LIMIT %s"
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("dashboard")

app = Flask(__name__)
app.config["DEMO"] = True
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

demo_alert_ids = itertools.count(1)
demo_attack_cycle = itertools.cycle(
    [
        ("REPLAY ATTACK", "Replay Attack Blocked [Sequence ID Duplicate: 10423]", ["seq_regression", "ts_regression"]),
        ("MAC FAILURE", "OpenSSL Verification Failed [Corrupted MAC Dropped]", ["mac_mismatch"]),
        ("COMMAND SPOOFING", "Unauthenticated Therapy Command Rejected [Forged HMAC]", ["implausible_value"]),
    ]
)


def classify_bpm(bpm):
    if bpm is None:
        return None
    if bpm < BRADYCARDIA_BPM:
        return "BRADYCARDIA"
    if bpm > TACHYCARDIA_BPM:
        return "TACHYCARDIA"
    return "NORMAL"


def categorize(event_type, severity):
    if event_type == "ANOMALY_PHYSIOLOGICAL":
        return "physiological"
    if event_type in ("ANOMALY_SECURITY", "MAC_FAILURE", "REPLAY ATTACK", "MAC FAILURE", "COMMAND SPOOFING") or severity in ("WARN", "CRITICAL"):
        return "security"
    return "info"


def make_alert(alert_id, ts, severity, event_type, seq_id, details):
    if not isinstance(details, dict):
        details = {}
    reasons = details.get("reasons")
    if details.get("custom_desc"):
        text = details["custom_desc"]
    elif isinstance(reasons, list):
        text = ", ".join(str(r) for r in reasons)
    else:
        text = str(details.get("reason", ""))
    
    cat = categorize(event_type, severity)
    return {
        "id": alert_id,
        "ts": ts.isoformat() if hasattr(ts, "isoformat") else str(ts),
        "severity": severity,
        "event_type": event_type,
        "tag": event_type,
        "seq_id": seq_id,
        "category": cat,
        "type": cat,
        "detail": text[:200],
        "desc": text[:200],
        "bpm": details.get("bpm"),
        "rr_ms": details.get("rr_interval_ms"),
    }


def build_telemetry(rows):
    samples = [round(r[2], 4) for r in rows if r[2] is not None]
    bpm = next((r[3] for r in reversed(rows) if r[3] is not None), None)
    rr_ms = next((r[4] for r in reversed(rows) if r[4] is not None), None)
    label = next((r[5] for r in reversed(rows) if r[5]), None) or classify_bpm(bpm)
    return {
        "samples": samples,
        "bpm": bpm,
        "rr_ms": rr_ms,
        "classification": label,
        "last_seq": rows[-1][1],
        "count": len(rows),
    }


def db_connect():
    conn = psycopg2.connect(**DB_SETTINGS)
    conn.autocommit = True
    return conn


def fetch_history():
    conn = db_connect()
    try:
        with conn.cursor() as cur:
            cur.execute(HISTORY_SQL, (HISTORY_LIMIT,))
            rows = cur.fetchall()
    finally:
        conn.close()
    return [make_alert(*row) for row in reversed(rows)]


def live_loop():
    conn = None
    last_frame_id = 0
    last_alert_id = 0
    while True:
        try:
            if conn is None:
                conn = db_connect()
                with conn.cursor() as cur:
                    cur.execute("SELECT COALESCE(MAX(id), 0) FROM ecg_telemetry")
                    last_frame_id = cur.fetchone()[0]
                    cur.execute("SELECT COALESCE(MAX(id), 0) FROM audit_logs")
                    last_alert_id = cur.fetchone()[0]
                log.info("connected to database, streaming new rows only")
            with conn.cursor() as cur:
                cur.execute(FRAME_SQL, (last_frame_id,))
                rows = cur.fetchall()
                if rows:
                    last_frame_id = rows[-1][0]
                    socketio.emit("telemetry", build_telemetry(rows))
                cur.execute(ALERT_SQL, (last_alert_id,))
                for row in cur.fetchall():
                    last_alert_id = row[0]
                    socketio.emit("alert", make_alert(*row))
        except Exception:
            log.exception("database poll failed, retrying")
            if conn is not None:
                try:
                    conn.close()
                except Exception:
                    pass
            conn = None
            socketio.sleep(2)
            continue
        socketio.sleep(TICK_SECONDS)


def beat_shape(phase):
    p_wave = 0.12 * math.exp(-(((phase - 0.18) / 0.03) ** 2))
    q_wave = -0.15 * math.exp(-(((phase - 0.285) / 0.012) ** 2))
    r_wave = 1.2 * math.exp(-(((phase - 0.30) / 0.012) ** 2))
    s_wave = -0.25 * math.exp(-(((phase - 0.315) / 0.012) ** 2))
    t_wave = 0.30 * math.exp(-(((phase - 0.55) / 0.05) ** 2))
    return p_wave + q_wave + r_wave + s_wave + t_wave


class DemoGenerator:
    def __init__(self, base_rr=0.8, seed=None):
        self.base_rr = base_rr
        self.rng = random.Random(seed)
        self.rr = base_rr
        self.elapsed = 0.0
        self.beats = 0
        self.pending_rr = None
        self.seq = 10400

    def next_rr(self):
        self.beats += 1
        if self.pending_rr is not None:
            rr, self.pending_rr = self.pending_rr, None
            return rr, False
        if self.beats % 25 == 0:
            self.pending_rr = self.base_rr * 1.4
            return self.base_rr * 0.6, True
        return max(0.5, self.rng.gauss(self.base_rr, 0.02)), False

    def step(self, n_samples):
        samples = []
        premature = False
        for _ in range(n_samples):
            value = beat_shape(self.elapsed / self.rr) + self.rng.gauss(0, 0.008)
            samples.append(round(value, 4))
            self.seq += 1
            self.elapsed += 1.0 / SAMPLE_RATE_HZ
            if self.elapsed >= self.rr:
                self.elapsed -= self.rr
                self.rr, was_premature = self.next_rr()
                premature = premature or was_premature
        return samples, premature


def demo_loop():
    generator = DemoGenerator()
    started = time.monotonic()
    emitted = 0
    while True:
        due = int((time.monotonic() - started) * SAMPLE_RATE_HZ) - emitted
        if due > 0:
            samples, premature = generator.step(due)
            emitted += due
            bpm = round(60.0 / generator.rr, 1)
            socketio.emit(
                "telemetry",
                {
                    "samples": samples,
                    "bpm": bpm,
                    "rr_ms": round(generator.rr * 1000.0, 1),
                    "classification": classify_bpm(bpm),
                    "last_seq": generator.seq,
                    "count": due,
                },
            )
            if premature:
                details = {
                    "reasons": ["physio_outlier"],
                    "custom_desc": f"Premature Ventricular Contraction Detected (BPM: {bpm})",
                    "bpm": bpm,
                    "rr_interval_ms": round(generator.rr * 1000.0, 1),
                }
                socketio.emit(
                    "alert",
                    make_alert(
                        next(demo_alert_ids),
                        datetime.now(timezone.utc),
                        "INFO",
                        "ANOMALY_PHYSIOLOGICAL",
                        generator.seq,
                        details,
                    ),
                )
        socketio.sleep(TICK_SECONDS)


def dispatch_simulated_attack():
    event_type, custom_desc, reasons = next(demo_attack_cycle)
    alert = make_alert(
        next(demo_alert_ids),
        datetime.now(timezone.utc),
        "CRITICAL",
        event_type,
        10423,
        {"reasons": reasons, "custom_desc": custom_desc},
    )
    log.warning("SECURITY ALERT DISPATCHED: %s - %s", event_type, custom_desc)
    socketio.emit("alert", alert)
    return alert


@app.route("/")
def index():
    return render_template("index.html", demo=True)


@app.route("/health")
def health():
    return jsonify(status="ok", demo=app.config["DEMO"])


@app.route("/demo/attack", methods=["POST"])
def demo_attack():
    alert = dispatch_simulated_attack()
    return jsonify(sent=alert["event_type"], detail=alert["detail"])


@socketio.on("connect")
def on_connect():
    if app.config["DEMO"]:
        emit("history", [])
        return
    try:
        emit("history", fetch_history())
    except Exception:
        log.exception("could not load alert history")
        emit("history", [])


@socketio.on("simulate_attack")
def handle_simulate_attack():
    dispatch_simulated_attack()


def main():
    parser = argparse.ArgumentParser(description="CLAVIS clinical dashboard")
    parser.add_argument("--demo", action="store_true", help="stream a synthetic ECG, no database needed")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5001)
    args = parser.parse_args()

    env_demo = os.getenv("DEMO_MODE", "0") == "1"
    app.config["DEMO"] = args.demo or env_demo
    
    socketio.start_background_task(demo_loop if app.config["DEMO"] else live_loop)
    log.info("open http://%s:%d in your browser (Demo Stream: %s)", args.host, args.port, app.config["DEMO"])
    socketio.run(app, host=args.host, port=args.port, allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()