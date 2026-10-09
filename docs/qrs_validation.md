# Streaming QRS detection and record 100 validation

The CLAVIS detector is a streaming Pan-Tompkins-style pipeline. Each call
consumes one MLII voltage sample and retains only filter, integration, and
recent localization state.

## Processing pipeline

1. A second-order Butterworth high-pass at 5 Hz and second-order Butterworth
   low-pass at 18 Hz suppress baseline drift and high-frequency noise.
   Coefficients are calculated at construction from the record's sampling
   rate using digital biquad equations. They are therefore designed for the
   actual 360 Hz record rate rather than copied from the original 200 Hz
   Pan-Tompkins implementation.
2. A five-point derivative emphasizes the QRS slope and is scaled using the
   actual sampling rate.
3. Squaring makes both positive and negative slopes positive.
4. A 150 ms moving-window integrator describes QRS energy.
5. Local integrated peaks are compared with an adaptive threshold derived
   from separately updated signal and noise levels.
6. A 200 ms refractory period prevents one QRS complex from being counted
   more than once.

The integrated peak occurs after the electrical R peak. To account for that
processing delay, the detector searches its recent band-passed history for
the corresponding R peak and reports that earlier sample index. It also
reports the later sample at which the decision became available and their
time difference.

The first two seconds (720 samples at 360 Hz) train the adaptive threshold.
No detections are emitted during this startup interval. The first accepted
beat after startup is reported as `insufficient_data`; an RR interval and
instantaneous heart rate (`60 / RR`) become available after the second
accepted beat.

## Reference scoring

`tests/validate_qrs.py` reads the official `100.atr` file with Python WFDB.
The symbols counted as beats are:

`N L R B A a J S V r F e j n E / f Q ?`

Rhythm changes, comments, and signal-quality annotations are not beats. In
record 100 this selects the `N`, `A`, and `V` annotations and excludes the
single `+` rhythm annotation. Reference beats before sample 720 are excluded
because the detector explicitly emits no decisions during threshold startup.

Detections and annotations are matched in time order, one-to-one, within
150 ms (54 samples at 360 Hz). Reference beats near the end are not excluded.
Consequently, the beat at sample 649991 is counted as a false negative: only
eight later input samples exist before index 649999, which is not enough for
the causal integration peak to be confirmed before end-of-file. Across the
2269 accepted detections, confirmation followed the reported R-peak by 2 to
41 samples (median 25); the final nearby beats required 12 to 13 samples. The
missed annotation is 254 samples after the last detected beat, consistent with
the surrounding RR timing. No synthetic samples are appended and this beat is
not removed from the reported results.

Measured record 100 results:

- Reference beats after startup: 2270
- Detections: 2269
- True positives: 2269
- False positives: 0
- False negatives: 1
- Sensitivity: 99.955947%
- Positive predictive value: 100.000000%
- Mean absolute matched timing error: 9.648 ms
- Maximum absolute matched timing error: 50.000 ms

These measurements validate this implementation on one database record only.
They are not evidence of clinical accuracy, safety, or regulatory compliance.
