# Bass Generator And Motion Control

This note records the current bassline generator so future changes can build on
the same mental model.

## Manual GEN

`GEN` creates a queued 32-step mono bass pattern. It keeps the current bass
style and root, then changes the rhythm, notes, accent, and slide values using a
new random seed. The queued pattern is applied on the next 16-step boundary.

The generator uses:

- The selected bass style, which defines density, accent rate, slide rate,
  octave bias, and rhythm bias.
- The selected root plus a minor scale: `0, 2, 3, 5, 7, 8, 10`.
- The current drum pattern. Kick steps raise bass gate probability; snare/clap
  steps lower it; some styles use hats or offbeats as extra rhythm hints.
- A seed mixed with sample time, so repeated `GEN` presses produce variants
  instead of a fixed preset.

## Motion GEN

The Tab5 BMI270 is sampled from the main app loop every 35 ms. The audio task
does not read the sensor.

Each motion peak estimates:

- Energy: derived from gyro magnitude plus acceleration delta.
- Direction: dominant signed gyro axis, encoded as `X+`, `X-`, `Y+`, `Y-`,
  `Z+`, or `Z-`.
- Period: smoothed time between recent motion peaks.

When a strong enough peak is detected and the 650 ms cooldown has elapsed, the
app queues a generated bassline for the next 16-step boundary.

Motion mapping:

- `X+`: pushes the melody upward across the phrase.
- `X-`: pulls the melody downward across the phrase.
- `Y+`: favors early offbeats.
- `Y-`: favors later offbeats.
- `Z+`: increases slide behavior.
- `Z-`: increases octave jumps and accent pressure.
- Faster wave periods create denser/stutter-like gates.
- Slower wave periods create sparser quarter-step gates.

If BMI270 initialization fails, the app still runs; manual `GEN` remains
available and the UI shows motion as off.
