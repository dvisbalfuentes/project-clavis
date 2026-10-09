#!/usr/bin/env python3
"""Compare CLAVIS QRS detections with WFDB beat annotations."""

import argparse
import re
import subprocess

import numpy as np
import wfdb


# WFDB beat annotations from the MIT annotation code table. Rhythm changes,
# comments, and signal-quality markers are intentionally not beats.
BEAT_SYMBOLS = {
    "N", "L", "R", "B", "A", "a", "J", "S", "V", "r", "F",
    "e", "j", "n", "E", "/", "f", "Q", "?",
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("record", help="WFDB record stem, for example data/mitdb/100")
    parser.add_argument("--executable", default="./build/ecg_playback")
    parser.add_argument("--tolerance-ms", type=float, default=150.0)
    args = parser.parse_args()

    completed = subprocess.run(
        [args.executable, args.record, "0", "--qrs"],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = completed.stdout.splitlines()
    rate_match = re.search(r"sampling_rate_hz=([0-9.]+)", lines[0])
    startup_match = next(
        (re.search(r"startup_samples=(\d+)", line) for line in lines
         if "startup_samples=" in line),
        None,
    )
    if not rate_match or not startup_match:
        raise RuntimeError("Detector output is missing rate or startup metadata")
    sampling_rate = float(rate_match.group(1))
    startup_samples = int(startup_match.group(1))
    tolerance_samples = round(args.tolerance_ms * sampling_rate / 1000.0)

    detections = np.array([
        int(match.group(1))
        for line in lines
        if (match := re.search(r"r_peak_sample=(\d+)", line))
    ], dtype=int)
    annotation = wfdb.rdann(args.record, "atr")
    references = np.array([
        int(sample)
        for sample, symbol in zip(annotation.sample, annotation.symbol)
        if symbol in BEAT_SYMBOLS and sample >= startup_samples
    ], dtype=int)

    reference_index = detection_index = true_positives = 0
    errors: list[int] = []
    false_positives: list[int] = []
    false_negatives: list[int] = []
    while reference_index < len(references) and detection_index < len(detections):
        difference = int(detections[detection_index] - references[reference_index])
        if abs(difference) <= tolerance_samples:
            errors.append(difference)
            true_positives += 1
            reference_index += 1
            detection_index += 1
        elif detections[detection_index] < references[reference_index] - tolerance_samples:
            false_positives.append(int(detections[detection_index]))
            detection_index += 1
        else:
            false_negatives.append(int(references[reference_index]))
            reference_index += 1
    false_positives.extend(map(int, detections[detection_index:]))
    false_negatives.extend(map(int, references[reference_index:]))

    sensitivity = true_positives / (true_positives + len(false_negatives))
    positive_predictive_value = true_positives / (
        true_positives + len(false_positives)
    )
    absolute_errors = np.abs(np.asarray(errors))
    print(f"sampling_rate_hz={sampling_rate:g}")
    print(f"startup_samples={startup_samples}")
    print(f"tolerance_samples={tolerance_samples}")
    print(f"reference_beats={len(references)}")
    print(f"detections={len(detections)}")
    print(f"true_positives={true_positives}")
    print(f"false_positives={len(false_positives)}")
    print(f"false_negatives={len(false_negatives)}")
    print(f"sensitivity={sensitivity:.8f}")
    print(f"positive_predictive_value={positive_predictive_value:.8f}")
    print(f"mean_absolute_error_ms={absolute_errors.mean() / sampling_rate * 1000:.3f}")
    print(f"maximum_absolute_error_ms={absolute_errors.max() / sampling_rate * 1000:.3f}")
    print(f"false_positive_samples={false_positives}")
    print(f"false_negative_samples={false_negatives}")


if __name__ == "__main__":
    main()
