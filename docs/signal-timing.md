# Pulse gain, length and timing

[Back to the README](../README.md)

## Pulse gain, length and timing

`sample_rate_hz = symbol_rate_baud * SPS`. Time starts at the first stored sample;
sample k is at `k / sample_rate_hz` seconds. Buffer duration is
`sample_count / sample_rate_hz`, while the last sample's timestamp is one sample
interval earlier. Full filter transients are included.

RRC taps are real, symmetric and normalized so `sum(tap*tap) = 1`. Beta zero uses
the sinc limit; the center and quarter-roll-off singularities are evaluated
without unstable subtraction. A filter has `span * SPS + 1` taps. Symbols are
zero-inserted at multiples of SPS and convolved with these taps. This is pulse
shaping of symbol impulses, not filtering repeated rectangular samples.

For N symbols:

- RRC sample count: `(N - 1) * SPS + tap_count`.
- RRC transmit delay: `(tap_count - 1) / 2` samples.
- Rectangular sample count: `N * SPS`; every symbol repeats for SPS samples and
  the reported FIR delay is zero.

Unit-energy RRC and unit-height rectangular pulses intentionally have different
sample powers. With unit-average-energy symbols, steady-state RRC sample power
is approximately `gain^2 / SPS`; rectangular sample power is approximately
`gain^2`. Amplitude is relative, with no voltage or impedance calibration.

The matched RRC view filters with the same unit-energy taps and samples at
`2 * transmit_delay + symbol_index * SPS`. It excludes one complete RRC span at
each end for steady-state comparisons; fewer than `2*span+1` symbols yields no
steady-state points. Finite truncation leaves residual intersymbol interference,
particularly at low roll-off or short spans. Tests at beta 0.35/span 12 allow
absolute complex errors below 0.02 for PSK and 0.03 for 16-QAM at gain 1.
Rectangular observations average each SPS-sample symbol interval. Both displayed
constellation views include gain; `GeneratedSignal::symbols` retains the original
normalized mapping.
