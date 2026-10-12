# Signal integrity specification

This document describes Aswina's current CLAVIS signal-engine behavior for the
team specification. It is not a claim of FDA compliance or medical-device
validation.

## Ordering, rate, conversion, and units

`MitBihReader` reads sample frames in file order. Each `EcgSample` contains a
zero-based `index`, `time_seconds = index / sampling_rate_hz`, and one physical
voltage per channel in header order. Record 100 is MLII followed by V5.

The sampling rate and declared sample count come from the first WFDB header
line. Digital format-212 values are sign-extended from 12 bits and converted
with:

```text
physical voltage = (digital sample - ADC baseline) / ADC gain
```

The reader uses a baseline embedded in the gain field when supplied; otherwise
it uses the WFDB ADC-zero field. It retains the header unit, which is `mV` for
record 100. The current metadata API exposes one shared unit string, so files
whose channels use different units are not represented completely.

## Signal time and playback time

Signal time is deterministic and comes only from sample index and header rate.
It does not change with playback speed. `play_realtime` uses a monotonic clock:

- Rate `0` delivers samples as fast as the program and output consumer allow.
- Rate `1` schedules sample `i` at approximately `start + i / sampling_rate`.
- Other positive values are playback multipliers, such as `2` for twice speed.
- Negative playback rates are rejected.

Operating-system scheduling and console I/O mean playback is best effort, not
hard real time. Delayed callbacks are not dropped or reordered; the loop moves
on immediately if a scheduled time has already passed.

## Detector delay and end-of-record behavior

QRS filtering, differentiation, and 150 ms integration require future samples
before a candidate can be confirmed. The detector searches retained filtered
history and reports the earlier estimated R-peak index, the later decision
index, and their processing delay. On record 100, accepted decision delays were
2 to 41 samples, with a median of 25 samples.

No samples are fabricated after EOF and there is no detector flush operation.
An R peak too close to EOF may therefore remain unconfirmed. Record 100's final
reference beat at sample 649991 has only eight later samples and is reported as
a false negative.

## Missing, malformed, and invalid input

The reader rejects an unavailable or empty header, malformed header fields,
missing signal specifications, unsupported formats, zero gain, missing data,
format-212 byte counts not divisible by three, and header sample counts larger
than the available data. Out-of-range random sample access throws an exception.

The current reader expects a complete contiguous recording. It does not mark,
interpolate, or conceal missing frames inside a file. WFDB checksum fields are
not currently verified.

The QRS detector rejects non-finite samples and invalid sampling rates. It
reports no QRS decisions during its two-second startup interval. The classifier
returns `insufficient_data` for absent, non-finite, zero, or negative RR/BPM
values and does not add invalid RR values to its history.

The command-line executable catches exceptions, prints an error, and exits
nonzero. The library APIs use C++ exceptions and optionals rather than a shared
transport error object.

## Supported formats and limitations

Supported input is the original MIT-BIH-style configuration of exactly two
signals stored together in one WFDB format-212 `.dat` file with a `.hea`
header. The entire selected record is decoded into memory.

Not currently supported:

- WFDB formats other than 212
- One-channel or more-than-two-channel records
- Channels stored in separate data files
- Multi-segment records
- Byte offsets, skew, or discontinuous samples
- Per-channel unit metadata
- WFDB checksum verification
- Live hardware acquisition or packet-loss recovery
- Automatic lead selection; the demo uses the first channel, MLII for record 100

Only record 100 has undergone the documented full-sample WFDB comparison and
reference QRS scoring.
