#!/usr/bin/env python3
"""Compare CLAVIS format-212 decoding with the official Python WFDB reader."""

import argparse
import re
import subprocess
import tempfile

import numpy as np
import wfdb


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("record", help="WFDB record stem, for example data/mitdb/100")
    parser.add_argument("--executable", default="./build/ecg_playback")
    args = parser.parse_args()

    with tempfile.TemporaryFile(mode="w+") as output:
        subprocess.run(
            [args.executable, args.record, "0"],
            check=True,
            stdout=output,
            text=True,
        )
        output.seek(0)
        metadata_line = output.readline().strip()
        decoded = np.loadtxt(output)

    reference = wfdb.rdrecord(args.record)
    rate_match = re.search(r"sampling_rate_hz=([0-9.]+)", metadata_line)
    unit_match = re.search(r"voltage_unit=([^ ]+)", metadata_line)
    if not rate_match or not unit_match:
        raise RuntimeError("Playback output is missing sampling rate or units")
    if decoded.ndim != 2 or decoded.shape[1] != reference.n_sig + 2:
        raise RuntimeError("Playback output has an unexpected number of columns")

    voltage_difference = decoded[:, 2:] - reference.p_signal
    maximum_difference = np.max(np.abs(voltage_difference), axis=0)
    print(f"cpp_samples={decoded.shape[0]}")
    print(f"wfdb_samples={reference.sig_len}")
    print(f"cpp_sampling_rate_hz={float(rate_match.group(1)):g}")
    print(f"wfdb_sampling_rate_hz={reference.fs:g}")
    print(f"cpp_voltage_unit={unit_match.group(1)}")
    print(f"wfdb_signal_names={reference.sig_name}")
    print(f"wfdb_units={reference.units}")
    print(f"maximum_absolute_voltage_difference={maximum_difference.tolist()}")

    if decoded.shape[0] != reference.sig_len:
        raise SystemExit("FAILED: sample counts differ")
    if float(rate_match.group(1)) != reference.fs:
        raise SystemExit("FAILED: sampling rates differ")
    if any(unit != unit_match.group(1) for unit in reference.units):
        raise SystemExit("FAILED: voltage units differ")
    if np.any(maximum_difference != 0.0):
        raise SystemExit("FAILED: decoded voltage values differ")
    print("validation=passed")


if __name__ == "__main__":
    main()
