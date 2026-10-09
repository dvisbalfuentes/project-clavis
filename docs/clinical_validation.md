# Clinical validation notes

This document records Aswina's measured prototype validation results for the
CLAVIS team specification. It does not claim FDA compliance, clinical safety,
or diagnostic performance.

## Data evaluated

Validation used MIT-BIH Arrhythmia Database record 100:

- 650000 samples per channel
- 360 Hz sampling rate
- MLII and V5 channels
- Millivolt (`mV`) physical units
- Original two-channel WFDB format 212

The Python reference was WFDB 4.3.1. QRS detection used MLII. V5 was validated
for ingestion but was not used by the detector.

## Ingestion comparison

The C++ reader and Python WFDB produced the same sample count, rate, units, and
physical voltage values. Across 650000 samples in each of two channels
(1300000 values total):

- MLII maximum absolute voltage difference: 0.0 mV
- V5 maximum absolute voltage difference: 0.0 mV
- Nonmatching voltage values: 0

The C++ reader converts each digital sample with `(digital - baseline) / gain`.
For record 100 the gain is 200 ADC units per mV and the baseline is 1024.

Reproduce this measurement with:

```bash
conda run -n clavis python tests/validate_ingest.py data/mitdb/100
```

## QRS validation

The reference `100.atr` annotations were read with Python WFDB. Beat symbols
counted were `N L R B A a J S V r F e j n E / f Q ?`; rhythm, comment, and
signal-quality annotations were not counted. Record 100 contributes `N`, `A`,
and `V` beats. Its `+` rhythm annotation is not a beat.

The detector has a two-second threshold-learning interval. Reference beats
before sample 720 were excluded because the detector intentionally emits no
decisions during that interval. Remaining detections and annotations were
matched chronologically, one-to-one, within 150 ms, or 54 samples at 360 Hz.

Measured result:

- Reference beats after startup: 2270
- Detections: 2269
- True positives: 2269
- False positives: 0
- False negatives: 1
- Sensitivity: 99.955947%
- Positive predictive value: 100.000000%
- Mean absolute matched timing error: 9.648 ms
- Maximum absolute matched timing error: 50.000 ms

The false negative is the final reference beat at sample 649991. The record
ends at sample 649999, leaving only eight later samples. Accepted decisions
required 2 to 41 later samples overall (median 25), and the nearby final beats
required 12 to 13. The missed annotation is 254 samples after the preceding
detection, consistent with surrounding RR timing, but the causal integrator
cannot confirm its peak before EOF. It remains a false negative; validation
does not append fabricated samples or silently exclude it.

## Classifier verification

Unit tests cover strict heart-rate boundaries, regular and irregular RR input,
startup, invalid values, coexisting flags, and reset behavior. The implemented
rules are:

- Bradycardia flag below 50 BPM; exactly 50 is not flagged.
- Tachycardia flag above 100 BPM; exactly 100 is not flagged.
- Default irregular-RR flag when sample RR coefficient of variation is at
  least 0.10, using up to 8 recent valid intervals and requiring at least 4.

On record 100, 2269 QRS detections produced 2268 valid RR/BPM classifications:

- Bradycardia flags: 0
- Tachycardia flags: 18
- Irregular-RR flags: 230
- Regular-RR results: 2035
- Tachycardia and irregular-RR flags together: 16
- Classification insufficient because RR/BPM was absent: 1
- Irregularity insufficient during initial RR history: 4 detections total

These are per-detection rule outputs, not diagnosed episodes. No labeled
classifier ground truth was evaluated, so no classifier sensitivity,
specificity, predictive value, or diagnostic accuracy is claimed. The QRS
detection metrics above measure heartbeat-location agreement only; they must
not be presented as classifier accuracy.

## Limitations

Results cover one record and one detection channel. Detector and classifier
behavior may differ with other subjects, rhythms, noise, leads, acquisition
systems, or sampling rates. Instantaneous BPM and RR variability depend on
correct QRS detection. The irregularity rule is a configurable prototype flag,
not an arrhythmia diagnosis. No clinical outcome, safety, demographic, or
regulatory validation has been performed.
