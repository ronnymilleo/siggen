# Using the GUI

[Back to the README](../README.md)

## Using the generator

The generator opens on startup, optionally initialized from `--preset` and
signal options supplied with `--gui`; it does not generate automatically. Enter
modulation, symbol count, symbol rate in baud, samples per symbol (SPS), and
amplitude gain in **Signal Setup**. Choose RRC or rectangular pulses in **Pulse
Shaping**. The **AWGN** section enables additive noise with a requested SNR in
dB; the visible data seed defaults to 5489 and the noise seed to 5490.
**Advanced** selects seeded random bits or exact explicit binary input (up to
eight bits per symbol). Selecting **WGN** replaces the symbol controls with the
noise source: sample count, sample rate, total complex noise power, and noise
seed.

The **Channel impairments** section degrades the signal after AWGN with a
carrier frequency offset, oscillator phase noise, IQ gain and phase imbalance,
DC offsets and an ADC quantizer; every field defaults to off.

**Generate Signal** starts one owned asynchronous job. Editing settings while it
runs affects the next request. The previous result remains visible until a new
result completes; a message identifies settings that differ from that result.
Failed generation preserves the previous result. Closing the application waits
for its pending job. **Signal Summary**, plots and exports describe completed
samples, using their original configuration.

Defaults are BPSK, 256 symbols, 1000 Bd, SPS 8, RRC roll-off 0.2, RRC span 10
symbols, gain 1, random seed 5489, and noise seed 5490; WGN defaults are 2048
samples, 8000 Hz, and unit noise power. Bounds are 1–65536 symbols, SPS 1–32,
span 1–100 symbols, roll-off [0,1], and gain [0,1000000]. RRC requires SPS >= 2
and an even `span * SPS`. Rate must be positive, with finite derived sample rate
and buffer duration. Generation uses checked sizes and a 4,194,304-sample
ceiling, which also bounds WGN sample counts. There is no implicit padding or
truncation of explicit bits: supply exactly `symbol_count * bits_per_symbol`
characters, each `0` or `1` (no whitespace), up to 524,288 bits (65,536 symbols x 8 bits) for 256-QAM.

## Measurements, eye diagram, spectral windows and guided presets

After **Generate Signal** the **Measurements** block reports mean and peak power
and the PAPR over the complete buffer (filter transients included). For linear
signals it also reports the EVM of the matched-filter observations relative to
the gain-scaled ideal symbols, and the corresponding SNR after the matched filter
(`-EVM` in dB). Matched filtering averages noise over about SPS samples, so this
value is the requested sample-level SNR plus `10*log10(SPS)` in expectation; the
GUI shows both numbers and the measured one still varies with the noise
realization. A clean RRC signal shows a small EVM floor (about -43 dB for the
default span 10, roll-off 0.2) from the truncated filter. Noise sources and FSK
signals have power statistics only (no symbol-rate observations to compare).

For linear signals the Measurements block also shows the bit error rate of the ideal reference receiver
(BER, bit errors over bits compared, SER), the Eb/N0 implied by the measured EVM and the textbook BER at that Eb/N0. The
**BER** tab sweeps Eb/N0 in the background and plots the measured curve against theory, with an upper bound where no error was
seen and an **Export image...** button like the other tabs; see [Bit error rate and the reference receiver](ber-and-receiver.md).

The **Eye** tab overlays up to 200 matched-filter traces, each two symbol periods
wide and centred on a decision instant, for the I or Q component. Traces cover the
same steady-state symbols as the constellation.

The **Spectrum** tab selects the Welch window: periodic Hann (default),
Hamming, Blackman or Rectangular. Hann output is unchanged from earlier releases.

`presets/` contains numbered guided lessons. Lines beginning with `#` after the
preset header are notes: parsers ignore them and the GUI shows them after
**Load Preset**, so a lesson can say what to look at (for example
`presets/02-qpsk-snr-8db.preset`). Lessons 11–16 cover 2-FSK, MSK, DQPSK under a
carrier offset, 4-PAM, 256-QAM and OOK; lessons 17–21 cover OQPSK, pi/4-DQPSK, 32-QAM, 8-DPSK and 4-ASK. Controls also have hover tooltips, and the symbol rate, SPS, pulse, roll-off, span and SNR controls have a **?** button that opens a longer "what is this?" panel (the SNR panel also shows your current SNR as Es/N0 and Eb/N0).

## Views and spectrum calibration

**Waveform** displays I in blue and Q in orange against seconds. A cached reduction
keeps both I and Q extrema in chronological order, with at most 4096 points. This
is a bounded overview; deep zoom cannot restore omitted samples. Export contains
all original samples. **Constellation** separates mapped ideal symbols from
matched-filter observations and constrains equal units per pixel on the axes;
with AWGN enabled the observation view is explicitly labeled as noisy. WGN
results show waveform, spectrum and power statistics without any symbol
constellation. FSK and MSK results replace the constellation and eye with a
**Frequency** tab (see [Modulations](modulations.md#frequency-modulation-2-fsk-4-fsk-and-msk)).

Each tab can be saved as a PNG or SVG image (see [Exporting plot images](#exporting-plot-images)).

**Eye** overlays two-symbol-period traces of the matched-filter I and Q (OQPSK
shows the half-symbol Q offset). **Pipeline** (linear signals only) stacks five
rows on one time axis, in symbol periods: the data bits; the mapped symbols
(amplitude gain applied); the symbols with SPS - 1 zeros inserted after each
(for OQPSK the Q impulses lag by half a symbol); the clean pulse-filter output;
and the final signal with AWGN and impairments. The filter delays its output by
`span / 2` symbols, so by default rows 4 and 5 are shifted back by that delay
to line each pulse peak up with its symbol (a checkbox turns this off, to see the
delay itself). Sliders choose the first symbol and how many (4 to 64) are shown.
Row 4 is the same signal as the exported samples without noise, regenerated from
the same configuration with AWGN and impairments off; when none are enabled rows 4
and 5 coincide. FSK, MSK and WGN have no symbol/filter chain and no Pipeline tab.
**Spectrum** uses full complex samples, independent of waveform reduction. The
Welch estimator uses a periodic window (Hann by default, see above), 50% overlap, no mean subtraction,
and an unnormalized forward FFT with negative exponent. The default segment size
is 1024; shorter buffers use the largest power of two that fits, with a minimum
of four samples. Only complete segments contribute; an incomplete trailing
segment is omitted. DC is retained. Frequency bins cover `[-Fs/2, Fs/2)`.

Each segment contributes `abs(FFT(x*w))^2 / (Fs * sum(w*w))`, averaged across
segments. No one-sided factor of two is applied. Linear output is relative
amplitude squared per Hz. Summing density times bin width gives window-weighted
mean complex-sample power. A complex exponential `exp(+j*2*pi*f*t)` peaks at
positive f; conjugating it reverses the sign. The display is
`10*log10(density)` relative to 1 amplitude²/Hz, floored at -200 dB for plotting.
It is not dBm/Hz or a hardware-calibrated measurement.

## Exporting plot images

Every plot tab (Waveform, Frequency, Constellation, Eye, Pipeline, Spectrum) has an
**Export image...** button that saves what the tab shows as a **PNG** or an **SVG**,
for slides and reports. Choose the destination, the format, the size in pixels
(200 to 8192 per side, default 1600x900) and the dark background of the app (default) or a light one for print. An existing file is only replaced after you confirm.

![Export image dialog on the Eye tab](images/export-image-dialog.png)

The image is drawn from the plotted data, not captured from the window, so it does
not depend on the window size, the zoom or the screen's DPI, and the axes, tick
labels, legend and titles are added for you. SVG is vector: it stays sharp at any
size in a document and its text can be edited. PNG is antialiased and uses the same system font as the GUI (the first of Inter, Noto Sans, DejaVu Sans, Liberation Sans or Helvetica that is installed), falling back to a small built-in bitmap font. The dark theme uses the app's own palette. Colours follow the screen (I blue, Q orange). What is exported:

- Waveform: the same min/max-reduced points as the screen, whole signal.
- Constellation: the view selected (mapped symbols or matched-filter observations), equal scale on both axes.
- Eye: the selected component, all overlaid traces with transparency.
- Pipeline: the five rows for the symbols currently shown (first symbol, count and the delay-compensation checkbox apply).
- Spectrum: the selected window. Frequency (FSK): estimate and nominal tone.
