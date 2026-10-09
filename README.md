# Project CLAVIS signal engine

CLAVIS is a student biomedical-cybersecurity prototype. This repository section
replays original MIT-BIH ECG recordings, detects QRS complexes, calculates RR
intervals and instantaneous heart rate, and emits prototype physiological
flags. It is not a medical device and does not provide diagnoses.

## Environment setup

The tested local environment uses macOS, Apple Clang, Conda, Python 3.12, and
CMake. From the repository root:

```bash
conda create -n clavis python=3.12 cmake pip
conda activate clavis
python -m pip install -r requirements.txt
```

`requirements.txt` installs Python WFDB 4.3.1 for independent validation. On a
new Mac, install Apple's command-line compiler once if `clang++` is missing:

```bash
xcode-select --install
```

## Download MIT-BIH record 100

Downloaded recordings are stored under `data/`, which is ignored by Git.

```bash
mkdir -p data/mitdb
curl --fail --location --output data/mitdb/100.hea https://physionet.org/files/mitdb/1.0.0/100.hea
curl --fail --location --output data/mitdb/100.dat https://physionet.org/files/mitdb/1.0.0/100.dat
curl --fail --location --output data/mitdb/100.atr https://physionet.org/files/mitdb/1.0.0/100.atr
```

The `.hea` and `.dat` files contain the waveform and metadata. The `.atr` file
contains the expert reference annotations used only for QRS validation.

## Build and test

```bash
conda activate clavis
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The current suite contains ingestion, Pan-Tompkins, and classifier tests.

## Reproduce validation

Compare all decoded samples in both channels with Python WFDB:

```bash
conda run -n clavis python tests/validate_ingest.py data/mitdb/100
```

Compare MLII QRS detections with the reference beat annotations using a
one-to-one 150 ms window:

```bash
conda run -n clavis python tests/validate_qrs.py data/mitdb/100
```

The measured results and scoring decisions are documented in
`docs/clinical_validation.md` and `docs/qrs_validation.md`.

## Demonstrations

Fast raw decoding writes the complete 650,000-sample output to a file:

```bash
./build/ecg_playback data/mitdb/100 0 > /tmp/clavis_record_100.txt
```

Fast heartbeat detection and classification prints only detected beats:

```bash
./build/ecg_playback data/mitdb/100 0 --qrs --classify
```

Real-time playback at the recording's native timing:

```bash
./build/ecg_playback data/mitdb/100 1 --qrs --classify
```

Record 100 lasts about 30 minutes. Use `Ctrl+C` to stop real-time playback.
Omitting `--qrs` preserves raw sample output; `--qrs` enables heartbeat metrics;
`--classify` implies QRS detection and adds physiological flags.

## Documentation

- `docs/clinical_validation.md`: measured validation results and limitations.
- `docs/signal_integrity.md`: signal representation, timing, error handling, and
  supported formats.
- `docs/engine_handoff.md`: C++ API and teammate integration boundary.
- `docs/qrs_validation.md`: QRS algorithm and annotation-scoring details.
- `docs/classifier.md`: configurable physiological rules.
