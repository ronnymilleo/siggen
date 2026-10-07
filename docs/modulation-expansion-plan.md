# Modulation expansion: AMCPy coverage and frequency modulation

Status: proposed implementation plan. No new modulation code has been implemented.
Baseline: modernization fast-forwarded locally into `main` at `d2eec00`.
Working branch: `feat/modulation-expansion`. Planning task: Jay #16.

## Reference and scope

Inspected AMCPy commit
[`a461d85e2c5a906af3993da34af2515560484fd6`](https://github.com/ronnymilleo/amcpy/tree/a461d85e2c5a906af3993da34af2515560484fd6),
particularly `src/amcpy/config.py`, `src/amcpy/feature_extraction.py` and README.
The live checkout is newer than the cached GitHub landing page.

| AMCPy class | Current app | Planned work |
|---|---|---|
| BPSK | Supported | Preserve existing mapping, seed behavior and samples |
| QPSK | Supported | Preserve existing mapping, seed behavior and samples |
| 8PSK | Missing | Add Gray-coded 8-PSK |
| 16QAM | Supported as 16-QAM | Preserve existing mapping and normalization |
| 64QAM | Missing | Add Gray-coded square 64-QAM |
| WGN | Missing | Add complex white Gaussian noise as a signal source |

WGN is a noise class, not a symbol modulation. AMCPy reads existing complex
MATLAB arrays; the inspected checkout does not contain the original waveform
synthesizer or datasets. This plan provides modulation-class coverage and a
specified new generator, without claiming byte-identical reproduction of the
master's-degree datasets. Exact historical compatibility would require original
generation parameters/code or reference samples, including pulse shape, mapping,
phase, sample rate, noise definition and frame boundaries.

Recommended additional coverage for this iteration: continuous-phase 2-FSK,
4-FSK, and MSK. Include optional AWGN on generated digital signals, because noisy
classification examples are useful alongside the WGN-only class. Keep clean
waveforms as the default.

Dataset export directly into AMCPy is a separate optional workstream, described
below. Default planning assumption: deliver interactive generation first.
GFSK/GMSK, OQPSK, differential PSK, PAM/OOK, larger constellations and analog
modulation remain subsequent candidates; avoid an arbitrary unbounded M control.

## Architecture

Keep C++23, ImGui/ImPlot, value types and owned asynchronous generation. Extend
by waveform family rather than forcing every signal through `MapSymbols()` and
RRC convolution.

- Add a small descriptor table with stable serialized identifiers, display names,
  family, optional bits per symbol, valid pulse controls and analysis capabilities.
- Separate common output/data settings from linear-modulation, FSK and noise
  settings. Prefer small tagged value types and functions over a class hierarchy.
- Linear PSK/QAM retains symbol count/rate, SPS and RRC/rectangular shaping.
- FSK/MSK uses symbols, sample rate derived from baud/SPS, and integrated frequency.
  RRC filtering of the complex FSK output is not a supported shaping option.
- WGN uses explicit sample count, sample rate and noise power. It has no mapped
  symbols, symbol rate, RRC delay or bit-input requirement.
- Results retain immutable configuration, complex samples and timing metadata.
  Keep mapped constellation points only where meaningful; represent symbol labels,
  frequency levels and observation availability explicitly for other families.
- Generalize result validation and export length checks; their current formulas
  assume that every source is a linear symbol train.
- Replace the hard-coded four-bits-per-symbol input ceiling and UI buffer size.
  64-QAM needs six bits per symbol: up to 393216 explicit bits at 65536 symbols.
  Apply bounded arithmetic to parser, generator, editor and export together.
- Preserve the current 4194304-sample limit, symbol/SPS limits and deterministic
  existing random-bit sequence. Separate data and noise PRNG streams.

## Numerical contract

### Linear modulation

Preserve current BPSK, QPSK and 16-QAM mappings exactly. Do not switch existing
QPSK to a generic phase convention as a side effect of adding 8-PSK.

8-PSK consumes three bits. At phases `k*pi/4`, k=0..7, assign labels
`000,001,011,010,110,111,101,100` counterclockwise. Use unit radius and a fixed
zero phase origin. The wrap-around pair also differs by one bit.

64-QAM consumes six bits: the first three select I, the next three Q. Axis levels
in increasing amplitude order are:

| Label | Unscaled level |
|---|---:|
| 000 | -7 |
| 001 | -5 |
| 011 | -3 |
| 010 | -1 |
| 110 | +1 |
| 111 | +3 |
| 101 | +5 |
| 100 | +7 |

Divide both axes by `sqrt(42)` for unit average constellation energy. Existing
RRC unit-energy taps, full-tail output lengths, gain and matched timing remain
unchanged. Test energy over the complete constellation, not an arbitrary random
buffer. Explicit input is exact-length binary data with no silent padding.

### Noise and SNR

Generate independent zero-mean Gaussian I and Q. For total complex noise power
P, each component has variance P/2. WGN specifies P in relative amplitude squared
before the common amplitude gain; reported output power includes gain squared.
It does not use a signal-relative SNR control.

For optional AWGN, define SNR as clean complex sample power divided by added
complex noise power. It is not Eb/N0 or Es/N0. Measure clean reference power over
a documented steady-state interval excluding RRC transients. Reject SNR mode
when no valid reference interval exists or its power is zero. Preserve the clean
signal gain; never normalize each noisy buffer to force an exact measured SNR.
Store requested SNR, reference interval/power, target noise power and noise seed
in metadata. Statistical measured SNR will vary across realizations.

Specify the integer-to-uniform and uniform-to-Gaussian conversion explicitly
(e.g. documented Box–Muller pairing/order with uniforms strictly inside (0,1)).
Do not rely on `std::normal_distribution` for cross-library reproducibility.
Noise-disabled output must remain identical to the existing clean generator;
noise settings must not change the transmitted bit sequence.

### FSK and MSK

Start with continuous phase throughout each generated buffer. For M=2 or 4,
frequencies are `f_m = (m - (M-1)/2) * tone_spacing_hz`. Use labels `0,1` for
ascending binary tones, and `00,01,11,10` for ascending four-level tones.

Use `x[n] = gain * exp(j*phase[n])`, with initial phase zero and
`phase[n+1] = phase[n] + 2*pi*f[n]/Fs`; retain phase at symbol transitions.
Generate exactly `symbol_count * SPS` samples, without an RRC tail. Declare the
modulation index as `h = tone_spacing_hz / symbol_rate_baud`; display the derived
value rather than allowing two contradictory independent controls. Default h=1
for 2-FSK/4-FSK. MSK is the binary specialization h=0.5, with tone centers at
`±symbol_rate_baud/4` and constant envelope.

Validate finite positive spacing/rates, representable phase increments and every
tone center strictly inside Nyquist. Explain that this center-frequency check
alone does not make rectangular frequency transitions bandlimited; spectral
skirts can alias at low SPS. Keep SPS=8 as the initial default and verify spectra.
Phase resets/discontinuous FSK are not included in the first implementation.

## Sequential implementation stages

Each stage has one Jay task, its predecessor dependency, a buildable local commit,
and a full result/validation/problems/ideas/decisions report. Follow
open → started → review → closed. Keep automatic Jay branch/PR integration
unconfigured, record its diagnostic, and do not push or publish automatically.

| Stage | Task | Commit | Acceptance and validation |
|---|---|---|---|
| 1 | #17 | `refactor: model waveform families and capabilities` | Family descriptors, family-specific configuration/result validation and bounded six-bit input. GUI/preset/export use capabilities. Version-2 serialization with legacy/v1 imports. Golden existing outputs unchanged; reject malformed/incompatible settings transactionally. |
| 2 | #18 | `feat: add normalized Gray-coded 8-PSK` | All eight expected points, cyclic Gray adjacency, energy and bit ordering. UI, preset, metadata, RRC/rectangular output, matched timing and recovery covered. |
| 3 | #19 | `feat: add normalized Gray-coded 64-QAM` | All 64 expected points, axis/neighbor adjacency and average energy. Exact six-bit input including maximum count. UI/export round trips and truncation-aware matched recovery verified. |
| 4 | #20 | `feat: generate complex Gaussian noise and AWGN` | WGN-only source and optional seeded AWGN. Independent PRNG streams, explicit power/SNR semantics and metadata. Statistical mean/variance/cross-correlation and SNR confidence bounds; zero/no-reference cases; disabled-noise regression; noisy export checks. |
| 5 | #21 | `feat: add continuous-phase 2-FSK and 4-FSK` | Defined tone mapping, spacing/index, phase continuity, constant envelope and lengths. All tone centers, instantaneous frequency sign, deterministic output and aliasing validation tested. UI hides incompatible RRC/matched controls. |
| 6 | #22 | `feat: add minimum-shift keying` | MSK uses the shared binary continuous-phase path with locked h=0.5. Independent phase-increment fixture, tone deviation, envelope, continuity, length, presets and export tests. |
| 7 | #23 | `feat: adapt analysis views for modulation families` | Linear mapped/matched constellations, labeled FSK/MSK I/Q trajectory and instantaneous-frequency view, and WGN waveform/PSD statistics. No misleading symbol slicer for non-linear/noise sources. Full headless/GUI/sanitizer regression, UI lifecycle/resizing/snapshot export checks and README tables. |

Every feature stage exposes a usable UI and working export; stage 7 consolidates
analysis and documentation rather than postponing all integration until the end.
Noise-only results leave constellation analysis unavailable. With AWGN, label
mapped ideal points and noisy observations distinctly. Frequency estimates use
phase differences with explicit endpoint handling and tests near phase wrapping;
never differentiate an unwrapped-looking plot after waveform reduction.

## Optional AMCPy dataset integration

The inspected configuration uses variables `signal_bpsk`, `signal_qpsk`,
`signal_8psk`, `signal_qam16`, `signal_qam64`, `signal_noise`, with complex arrays
shaped `(n_snr, n_frames, frame_size)`. Defaults are -10 through 20 dB in 2 dB
steps, 1000 frames per SNR and 2048 samples per frame.

If selected, add a separately estimated dataset stage after noise support:

- Export a bounded batch manifest plus existing binary I/Q; use an optional
  Python/SciPy converter to write MATLAB files accepted by `scipy.io.loadmat`.
  Keep Python/MATLAB dependencies out of the C++ desktop build.
- Define per-frame seeds from stable modulation/SNR/frame identifiers; record
  ordering and prevent accidental reuse between training and evaluation splits.
- Generate guard symbols and crop exactly 2048 steady-state samples for linear
  pulses. Record cropping and timing separately from normal full-tail export.
- Define WGN power across the SNR axis explicitly: pure noise has no intrinsic
  signal-relative SNR. Do not assign SNR labels without a reference convention.
- Stream/chunk frame creation with progress and cancellation; AMCPy's default
  six-class batch has 196608000 complex samples (about 1.57 GB at complex float32)
  before any conversion copies. Do not allocate the entire batch in the GUI.
- Verify shapes, names, complex dtype, SNR axis and a small end-to-end feature
  extraction run. FSK/MSK require an intentional AMCPy classifier/config change;
  they cannot be added to an existing six-class model merely by exporting data.

## Follow-up candidates

GFSK and GMSK are the next frequency-modulation extensions: Gaussian shaping acts
on the frequency-driving symbols before phase integration, with explicit BT,
span, normalization and filter delay/tail conventions. They need independent
waveform fixtures and their own stage. OQPSK and differential schemes similarly
need timing/state contracts. PAM/OOK can reuse much of the linear pulse path but
need explicit energy/DC conventions. Carrier offsets, fading, hardware streaming
and full classifier training integration remain separate projects.
