# Signal-engine teammate handoff

This document separates the working local C++ engine from integration details
that Aswina, Daphne, and Chesper still need to agree on.

## Working local APIs

All types are in `clavis::engine`.

### ECG reader and playback

```cpp
MitBihReader reader("data/mitdb/100");
const EcgMetadata& metadata = reader.metadata();
EcgSample sample = reader.sample_at(index);

play_realtime(reader, playback_rate, [&](const EcgSample& sample) {
  // Consume sample in chronological order.
  return true;  // false stops playback
});
```

`EcgMetadata` provides `sampling_rate_hz`, a shared `voltage_unit`, channel
names, and sample count. `EcgSample` provides a zero-based sample index, signal
time in seconds, and a voltage vector in header channel order. Record 100 uses
360 Hz, mV, MLII at `voltage[0]`, and V5 at `voltage[1]`.

Playback rate `0` is unpaced; a positive rate is a native-time multiplier.
Signal timestamps remain `index / sampling_rate_hz` at every playback speed.

### Streaming QRS detector

```cpp
PanTompkinsDetector detector(metadata.sampling_rate_hz);
QrsResult qrs = detector.process(sample.voltage[0]);
```

Call `process` exactly once per sequential MLII sample. `QrsResult` includes:

- `peak_detected`: whether this input completed a detection decision
- `r_peak_index`: estimated R-peak location in original sample coordinates
- `detection_sample_index`: when the decision became available
- `processing_delay_seconds`: time between those two indices
- `rr_interval_seconds`: unavailable until a previous accepted beat exists
- `heart_rate_bpm`: unavailable until RR exists
- `sufficient_data`: true after at least two accepted beats

The detector deliberately emits no peaks during its first two seconds of
threshold learning.

### Physiological classifier

```cpp
PhysiologicalClassifier classifier;
ClassificationResult flags = classifier.process(
    qrs.rr_interval_seconds, qrs.heart_rate_bpm);
```

Only call the classifier for detected peaks. Its status is
`insufficient_data` when RR or BPM is unavailable or invalid. With valid
metrics, bradycardia and tachycardia booleans are immediately available.
`irregular_rr` is an optional boolean and remains unavailable until the minimum
RR history is present. Units are seconds for RR and beats per minute for BPM.
`reset()` clears RR history between records or patients.

## How Daphne can consume engine output

The current callback is the integration point. Daphne's security component can
receive either every `EcgSample`, each completed QRS/metric event, or both once
the team chooses a plaintext message definition. The signal engine should
remain responsible for signal order, physical units, R-peak localization, RR,
BPM, and prototype flags. Daphne's layer should remain responsible for
serialization decisions agreed by the team, encryption/authentication,
framing, sequence protection, and transport.

Unavailable values must remain distinguishable from numeric zero. In C++, they
are `std::optional`; the eventual plaintext representation still needs team
agreement. The encrypted example in `docs/shared_contract.json` describes only
the outer envelope and has not been changed.

## Pending plaintext and transport agreement

The team still needs to decide:

- Whether messages contain raw samples, QRS events, metric events, or batches
- Plaintext field names, types, version, and serialization format
- Whether voltage is encoded as floating-point mV or scaled integer units
- Channel identifiers and whether both MLII and V5 are transported
- Timestamp semantics: signal-relative time, wall-clock time, or both
- How sample index and the envelope `seq_id` relate
- Representation of unavailable RR, BPM, and irregularity values
- Whether processing/detection delay is transmitted
- Batch size, cadence, ordering, retry, and backpressure behavior
- Record/patient/session identifiers and ownership of those identifiers
- Error, end-of-record, reconnect, and dropped-message behavior
- Which fields Daphne authenticates and which Chesper stores or displays

These details should be agreed before changing the shared contract or building
an adapter that could accidentally become an undocumented de facto format.

## Current versus pending behavior

Working locally:

- Format-212 record 100 decoding and timed playback
- MLII streaming QRS detection
- R-peak, RR, BPM, processing-delay, and classifier output
- Unit tests plus independent WFDB ingestion and annotation validation
- Command-line raw, QRS, and classifier modes

Pending integration:

- A team-approved plaintext payload
- Calls into Daphne's security/framing implementation
- Delivery to Chesper's ingestion service, database, and dashboard
- End-to-end retry, replay, failure, and performance testing

No encryption, database, dashboard, or network transport is implemented by the
signal engine.
