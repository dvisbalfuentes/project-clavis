import sys
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "analytics"))
from anomaly_detector import analyze


def build_stream():
    rng = np.random.default_rng(42)
    n = 600
    rr = rng.normal(800, 30, n)
    rr[200] = 450
    rr[201] = 1150
    ts = 1727283600000000 + np.cumsum(rr * 1000).astype("int64")

    df = pd.DataFrame(
        {
            "id": np.arange(1, n + 1),
            "seq_id": np.arange(1, n + 1),
            "ts_us": ts,
            "voltage_mv": rng.normal(0, 0.5, n),
            "bpm": 60000.0 / rr,
            "rr_interval_ms": rr,
            "label": "normal",
        }
    )

    df.loc[[200, 201], "label"] = "pvc"

    df.loc[400, ["bpm", "rr_interval_ms"]] = [999.0, 5.0]
    df.loc[400, "label"] = "corrupt"

    df.loc[503, "label"] = "gap"
    df = df.drop(index=[500, 501, 502])

    replay = df.loc[[250]].copy()
    replay["label"] = "replay"
    df = pd.concat([df.loc[:299], replay, df.loc[300:]]).reset_index(drop=True)
    df["id"] = np.arange(1, len(df) + 1)
    return df


def main():
    result = analyze(build_stream())
    print(result.groupby(["label", "verdict"]).size())

    for label in ("replay", "corrupt", "gap"):
        verdicts = result.loc[result["label"] == label, "verdict"]
        assert (verdicts == "SECURITY").all(), label + " was not flagged as SECURITY"

    pvc = result.loc[result["label"] == "pvc", "verdict"]
    assert (pvc == "PHYSIOLOGICAL").all(), "natural cardiac spike was misclassified"

    normal = result[result["label"] == "normal"]
    false_rate = (normal["verdict"] != "NORMAL").mean()
    print("false positive rate on normal beats: %.2f%%" % (false_rate * 100))
    assert false_rate < 0.03, "too many false positives"

    print("PASS")


if __name__ == "__main__":
    main()