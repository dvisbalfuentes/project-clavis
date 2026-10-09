# Prototype physiological classifier

The classifier consumes the RR interval and instantaneous BPM emitted by the
QRS detector. Missing, non-finite, zero, or negative values return
`insufficient_data` and do not update the RR history.

Default rules:

- Bradycardia flag: BPM is strictly less than 50.
- Tachycardia flag: BPM is strictly greater than 100.
- Exactly 50 BPM and exactly 100 BPM do not trigger those respective flags.
- RR irregularity: sample coefficient of variation (standard deviation divided
  by mean) is at least 0.10 over the most recent 8 valid RR intervals.
- At least 4 valid RR intervals are required before the irregularity flag is
  available. Before that, heart-rate flags remain available but irregularity
  reports `insufficient_data`.

`ClassifierConfig` makes the RR window, minimum interval count, coefficient of
variation threshold, and BPM thresholds explicit. The window retains up to 8
intervals by default and begins evaluating as soon as 4 are present. Calling
`reset()` clears this history.

Heart-rate and irregular-RR flags are independent. For example, a tachycardia
flag and an irregular-RR flag can both be true for the same detector event.

The irregular-RR result is a prototype variability flag, not a diagnosis of
atrial fibrillation or any other arrhythmia. Instantaneous BPM can change
sharply after a missed or extra QRS detection, and the coefficient of variation
is sensitive to the same detector errors. The rules have only been exercised
on the available prototype data and tests; they are not clinically validated
or suitable for treatment decisions.

## MIT-BIH record 100 result

Using the 2269 QRS detections produced after detector startup:

- 1 classification was insufficient because the first detected beat had no RR
  interval or BPM.
- 2268 events had valid RR and BPM metrics.
- Irregularity was insufficient for the first 4 detections (the no-RR event and
  the first 3 valid intervals).
- Bradycardia flags: 0.
- Tachycardia flags: 18.
- Irregular-RR flags: 230.
- Regular-RR results: 2035.
- Tachycardia and irregular-RR coexisted on 16 events.
- Observed instantaneous BPM range: 54.961832 to 114.893617.
- Observed available RR coefficient-of-variation range: 0.013499 to 0.195607.

These are counts of detector events, not counts of diagnosed episodes or
patients. Consecutive flagged events are not consolidated into episodes.
