import argparse
import json
import logging
import os
import time
from pathlib import Path

import numpy as np
import pandas as pd
import psycopg2
from dotenv import load_dotenv

load_dotenv(Path(__file__).resolve().parent.parent / "infra" / ".env")

DB_SETTINGS = {
    "host": os.getenv("DB_HOST", "127.0.0.1"),
    "port": int(os.getenv("DB_PORT", "5432")),
    "user": os.getenv("POSTGRES_USER", "clavis"),
    "password": os.getenv("POSTGRES_PASSWORD", ""),
    "dbname": os.getenv("POSTGRES_DB", "clavis"),
}

WINDOW = 30
MIN_PERIODS = 10
Z_THRESHOLD = 3.5
MAD_FLOOR_MS = 10.0
MAX_TS_GAP_US = 5_000_000
BPM_RANGE = (20.0, 300.0)
RR_RANGE_MS = (200.0, 3000.0)
VOLTAGE_RANGE_MV = (-10.0, 10.0)
FLATLINE_WINDOW = 360
FLATLINE_STD = 1e-6
FETCH_LIMIT = 20000
CONTEXT_ROWS = 400

SECURITY_FLAGS = (
    "seq_regression",
    "seq_gap",
    "ts_regression",
    "ts_gap",
    "implausible_value",
    "flatline",
)
CRITICAL_FLAGS = ("seq_regression", "ts_regression", "implausible_value")
ALL_FLAGS = list(SECURITY_FLAGS) + ["physio_outlier"]

COLUMNS = ["id", "seq_id", "ts_us", "voltage_mv", "bpm", "rr_interval_ms"]
NUMERIC_COLUMNS = ["voltage_mv", "bpm", "rr_interval_ms"]

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("anomaly")


def robust_z(series):
    baseline = series.shift(1)
    rolling = baseline.rolling(WINDOW, min_periods=MIN_PERIODS)
    median = rolling.median()
    mad = rolling.apply(lambda w: np.median(np.abs(w - np.median(w))), raw=True)
    mad = mad.clip(lower=MAD_FLOOR_MS)
    return 0.6745 * (series - median) / mad


def out_of_range(series, bounds):
    return series.notna() & ~series.between(bounds[0], bounds[1])


def analyze(df):
    out = df.copy().reset_index(drop=True)

    prev_max_seq = out["seq_id"].cummax().shift(1)
    prev_max_ts = out["ts_us"].cummax().shift(1)

    out["seq_regression"] = out["seq_id"] <= prev_max_seq
    out["seq_gap"] = (out["seq_id"] - prev_max_seq) > 1
    out["ts_regression"] = out["ts_us"] <= prev_max_ts
    out["ts_gap"] = (out["ts_us"] - prev_max_ts) > MAX_TS_GAP_US

    out["implausible_value"] = (
        out_of_range(out["bpm"], BPM_RANGE)
        | out_of_range(out["rr_interval_ms"], RR_RANGE_MS)
        | out_of_range(out["voltage_mv"], VOLTAGE_RANGE_MV)
    )

    voltage_std = out["voltage_mv"].rolling(FLATLINE_WINDOW, min_periods=FLATLINE_WINDOW).std()
    out["flatline"] = voltage_std < FLATLINE_STD

    out["rr_z"] = robust_z(out["rr_interval_ms"])
    out["physio_outlier"] = out["rr_z"].abs() > Z_THRESHOLD

    security = out[list(SECURITY_FLAGS)].any(axis=1)
    out["verdict"] = np.where(
        security,
        "SECURITY",
        np.where(out["physio_outlier"], "PHYSIOLOGICAL", "NORMAL"),
    )
    flag_matrix = out[ALL_FLAGS].to_numpy()
    out["reasons"] = [
        [name for name, hit in zip(ALL_FLAGS, row) if hit] for row in flag_matrix
    ]
    return out


def fetch_frames(conn, last_id):
    select = "SELECT " + ", ".join(COLUMNS) + " FROM ecg_telemetry "
    with conn.cursor() as cur:
        cur.execute(select + "WHERE id <= %s ORDER BY id DESC LIMIT %s", (last_id, CONTEXT_ROWS))
        context = cur.fetchall()[::-1]
        cur.execute(select + "WHERE id > %s ORDER BY id LIMIT %s", (last_id, FETCH_LIMIT))
        new_rows = cur.fetchall()
    df = pd.DataFrame(context + new_rows, columns=COLUMNS)
    for col in NUMERIC_COLUMNS:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def none_if_nan(value):
    return None if pd.isna(value) else float(value)


def severity_for(reasons):
    if any(r in CRITICAL_FLAGS for r in reasons):
        return "CRITICAL"
    if any(r in SECURITY_FLAGS for r in reasons):
        return "WARN"
    return "INFO"


def build_alerts(df, last_id):
    flagged = df[(df["verdict"] != "NORMAL") & (df["id"] > last_id)]
    alerts = []
    for row in flagged.itertuples():
        event = "ANOMALY_SECURITY" if row.verdict == "SECURITY" else "ANOMALY_PHYSIOLOGICAL"
        details = {
            "telemetry_id": int(row.id),
            "reasons": row.reasons,
            "bpm": none_if_nan(row.bpm),
            "rr_interval_ms": none_if_nan(row.rr_interval_ms),
            "voltage_mv": none_if_nan(row.voltage_mv),
            "rr_z": none_if_nan(row.rr_z),
        }
        alerts.append((severity_for(row.reasons), event, int(row.seq_id), json.dumps(details)))
    return alerts


def emit_alerts(conn, alerts):
    if not alerts:
        return
    with conn.cursor() as cur:
        cur.executemany(
            "INSERT INTO audit_logs (severity, event_type, seq_id, details) "
            "VALUES (%s, %s, %s, %s::jsonb)",
            alerts,
        )


def run_once(conn, last_id):
    df = fetch_frames(conn, last_id)
    if df.empty or int(df["id"].max()) <= last_id:
        return last_id
    result = analyze(df)
    alerts = build_alerts(result, last_id)
    emit_alerts(conn, alerts)
    new_rows = result[result["id"] > last_id]
    counts = new_rows["verdict"].value_counts().to_dict()
    log.info("analyzed %d new frames: %s", len(new_rows), counts)
    return int(df["id"].max())


def main():
    parser = argparse.ArgumentParser(description="CLAVIS anomaly detector")
    parser.add_argument("--watch", action="store_true", help="keep running and check for new frames")
    parser.add_argument("--interval", type=float, default=2.0, help="seconds between checks in watch mode")
    args = parser.parse_args()

    conn = psycopg2.connect(**DB_SETTINGS)
    conn.autocommit = True
    last_id = 0
    try:
        while True:
            last_id = run_once(conn, last_id)
            if not args.watch:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        conn.close()


if __name__ == "__main__":
    main()