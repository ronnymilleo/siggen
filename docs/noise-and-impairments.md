# Noise, SNR and channel impairments

[Back to the README](../README.md)

## Noise and SNR

Complex WGN generates independent zero-mean Gaussian I and Q components. For a
configured total complex noise power `P` (relative amplitude squared, before the
common amplitude gain), each component has variance `P/2`; reported output power
includes gain squared. WGN defaults are 2048 samples, 8000 Hz, unit noise power,
and noise seed 5490. WGN has no mapped symbols, symbol rate, RRC delay, explicit
bits, or SNR control, and its analysis omits constellation views.

Gaussian conversion is a documented Box–Muller transform driven by
`std::mt19937(noise_seed)`, not `std::normal_distribution`. Each uniform is
`u = (raw + 0.5) / 2^32`, strictly inside (0,1). Each pair `(u1, u2)` yields
`z0 = sqrt(-2 ln u1) * cos(2*pi*u2)` first and `z1 = sqrt(-2 ln u1) * sin(2*pi*u2)`
second; consecutive draws alternate cos/sin from the same pair, with I before Q.

### SNR, Es/N0 and Eb/N0

The SNR setting is a per-sample ratio over the whole sample rate, so it changes
with oversampling. With `Ps` the clean signal power, `Pn` the added complex noise
power, `Fs = Rs * SPS` the sample rate and `Rs` the symbol rate, the symbol energy
is `Es = Ps / Rs` and the noise density `N0 = Pn / Fs`, so
`Es/N0 = SNR * SPS` (dB: `SNR + 10*log10(SPS)`) and
`Eb/N0 = Es/N0 / bits_per_symbol` (dB: `Es/N0 - 10*log10(bits_per_symbol)`).
Compare modulations at equal Eb/N0, not equal SNR. The GUI shows the equivalent
Es/N0 and Eb/N0 under the SNR field; they follow the requested SNR, not the
measured one.

## Channel impairments

Linear signals can pass through a simple front-end/channel stage after AWGN.
Every effect is disabled at its default value, and a configuration with no
active effect produces bit-identical output to earlier versions. Effects are
applied in a fixed order, with `n` the sample index and `fs` the sample rate:

1. **CFO** (`--cfo-hz`): multiplies by `exp(j*2*pi*f*n/fs)`.
2. **Phase noise** (`--phase-noise-hz`): multiplies by `exp(j*phi[n])`, where `phi` is a
   Wiener process whose per-sample increments are Gaussian with variance
   `2*pi*linewidth/fs` (a Lorentzian oscillator with the given 3 dB linewidth).
   Increments come from the same documented Box–Muller source, seeded by
   `--impairment-seed` (default 5491), independent of the data and noise seeds.
3. **IQ imbalance** (`--iq-gain-db`, `--iq-phase-deg`): `I' = I` and
   `Q' = g*(Q*cos(phi) + I*sin(phi))` with `g = 10^(gain_db/20)`. This squeezes
   and shears the constellation and creates an image of the signal in the spectrum.
4. **DC offset** (`--dc-i`, `--dc-q`): adds a constant to I and Q, expressed as a
   fraction of the buffer's RMS amplitude at that point.
5. **Quantization** (`--adc-bits`, 2–24, 0 disables): mid-rise quantizer per
   component whose full scale auto-ranges to the largest I or Q magnitude.

Impairments reject noise sources (WGN). Presets with any active impairment are
saved as version 3, which appends `CfoHz`, `PhaseNoiseLinewidthHz`, `IqGainDb`,
`IqPhaseDeg`, `DcOffsetI`, `DcOffsetQ`, `AdcBits` and `ImpairmentSeed`; presets
without impairments remain version 2. Export and batch sidecars gain an
`impairments` block when anything was applied. In `siggen batch`, impairments run
on each cropped frame after AWGN, with a per-frame seed derived from the base seed,
waveform, frame index and the configured impairment seed (stream tag 3); the
manifest records the settings and derived seed for impaired frames. Guided
presets 07–10 demonstrate each effect.

Optional AWGN applies only to linear signals and is disabled by default. SNR is
defined as clean complex sample power divided by added complex noise power (not
Eb/N0 or Es/N0). The clean reference power is measured over the fully supported
steady-state interval: `[span*SPS, symbol_count*SPS)` for RRC and the entire
signal for rectangular pulses. Empty or zero-power reference intervals are
rejected. The clean signal gain is preserved and realized noise is never
rescaled to force an exact measured SNR, so measured values vary across
realizations. Requested SNR, reference interval and power, added noise power,
and the noise seed are recorded in metadata. Data and noise PRNG streams are
independent: changing the noise seed never changes the transmitted symbols, and
disabling noise reproduces the existing clean samples exactly.
