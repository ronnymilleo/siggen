# Siggen

[![CI](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml/badge.svg)](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml)

A C++23 signal generator with a command-line interface and optional ImGui/ImPlot
GUI. Generate reproducible complex I/Q samples for BPSK, QPSK, Gray-coded 8-PSK,
Gray-coded square 16-QAM, Gray-coded square 64-QAM, and complex white Gaussian
noise (WGN), with optional AWGN on linear signals; inspect waveforms,
constellations and a two-sided spectrum; save presets; export single signals; and
generate swept fixed-length frame datasets (`siggen batch`) for classifier
training. This application generates complex baseband, not an RF carrier.

<div align="center">
  <img width="952" height="1040" alt="image" src="https://github.com/user-attachments/assets/e3b1460e-60e9-4812-90b6-55bfb21fdd97" />
  <p align="center"><em>Waveform - Captured in Arch Linux with Hyprland</em></p>

  <img width="954" height="372" alt="image" src="https://github.com/user-attachments/assets/edb93423-c40b-4376-a4ec-829189cd89ae" />
  <p align="center"><em>Constellation - Captured in Arch Linux with Hyprland</em></p>

  <img width="955" height="365" alt="image" src="https://github.com/user-attachments/assets/21a40add-c82b-4d7e-8faa-69fd4e968864" />
  <p align="center"><em>Spectrum - Captured in Arch Linux with Hyprland</em></p>
</div>

## Build and test

Requirements: CMake 3.28+, a C++23 compiler and standard library (including
`std::ios::noreplace`), and Ninja for the commands below. Vendor dependencies are
pinned Git submodules; no dependency download occurs during CMake configuration.

```sh
git submodule update --init --recursive
cmake --workflow --preset dev
./build/dev/bin/siggen --gui
```

Use CMake presets for documented builds and agent validation. Each workflow runs
configure → build → test; individual steps are also available:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

| Preset | Build | GUI | Compiler | Sanitizer |
|---|---|---|---|---|
| `dev` | Debug | Yes | System default | None |
| `release` | Release | Yes | System default | None |
| `headless` | Debug | No | System default | None |
| `headless-release` | Release | No | System default | None |
| `clang-headless` | Debug | No | Clang (Linux) | None |
| `asan` | Debug | No | GCC (Linux) | Address + undefined behavior |
| `tsan` | Debug | No | GCC (Linux) | Thread |

All profiles enable tests and project warnings. Configure, build, test, and
workflow names match and share `build/<preset-name>`. Tests print failures and
fail if no tests are discovered. Discovery happens at test time, so linking a
test executable does not execute it.

The headless executable, library, and tests need no graphics libraries. GoogleTest is included
only when `SIGGEN_BUILD_TESTS=ON`; Threads is discovered only for the executable, GUI, or tests.
The GUI requires OpenGL and the platform development libraries used by the pinned
GLFW. On Linux its default backend is X11, which also runs through XWayland.
GLFW examples, tests, documentation, and installation default off. GoogleMock and ImGui/ImPlot demo sources are excluded from default builds.

Executables are in the build directory's `bin/`, libraries in `lib/`, and
`compile_commands.json` stays in the build directory. In-source builds are
rejected. `SIGGEN_ENABLE_WARNINGS` controls compiler-appropriate warnings on project
targets. If clang-format is available, `cmake --build --preset dev --target format`
explicitly formats project sources; ordinary builds never rewrite sources.

### Local overrides and library-only builds

Keep machine-specific compiler paths and parallelism in ignored
`CMakeUserPresets.json`. Local names must differ from the checked-in presets;
inheritance gives each new name its own build directory. For example:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "local-clang",
      "inherits": "clang-headless",
      "cacheVariables": { "CMAKE_CXX_COMPILER": "/usr/bin/clang++" }
    },
    {
      "name": "library-only",
      "inherits": "headless",
      "cacheVariables": { "SIGGEN_BUILD_APP": "OFF", "SIGGEN_BUILD_TESTS": "OFF" }
    }
  ],
  "buildPresets": [
    { "name": "local-clang", "configurePreset": "local-clang", "jobs": 4 },
    { "name": "library-only", "configurePreset": "library-only", "jobs": 4 }
  ],
  "testPresets": [
    { "name": "local-clang", "inherits": "clang-headless", "configurePreset": "local-clang" }
  ],
  "workflowPresets": [
    {
      "name": "local-clang",
      "steps": [
        { "type": "configure", "name": "local-clang" },
        { "type": "build", "name": "local-clang" },
        { "type": "test", "name": "local-clang" }
      ]
    }
  ]
}
```

Run `cmake --workflow --preset local-clang`. To build only the signal library,
run `cmake --preset library-only` followed by `cmake --build --preset library-only`;
this needs no vendor submodules and has no test step. `SIGGEN_BUILD_APP` controls
the executable; `SIGGEN_BUILD_GUI` controls its optional graphical interface. For a local
GUI or Release Clang profile, inherit `dev` or `release` and set the compiler path.
CMake 3.28 supports these built-in [workflow presets](https://cmake.org/cmake/help/v3.28/manual/cmake-presets.7.html).

## Command line and logging

There is one application executable, `siggen`. With no arguments it generates
the default BPSK signal and exports `signal.csv` plus `signal.csv.json` in the
current directory, then exits. Use `--gui` to open the desktop interface instead:

```sh
./build/dev/bin/siggen
./build/dev/bin/siggen --output captures/example.csv
./build/dev/bin/siggen --output signal.csv --overwrite
./build/dev/bin/siggen --modulation 64-QAM --symbols 512 --sps 4 --format cf32
./build/dev/bin/siggen --modulation qpsk --snr-db 10 --noise-seed 7 -o noisy.csv
./build/dev/bin/siggen --modulation wgn --samples 8192 --sample-rate 16000 --noise-power 2
./build/dev/bin/siggen --preset custom.preset --gain 2 --output preset.csv
./build/dev/bin/siggen --gui --preset custom.preset
./build/dev/bin/siggen --gui --log-level debug
./build/dev/bin/siggen --help
./build/dev/bin/siggen --version
```

Configuration resolves as **defaults → loaded preset → explicitly supplied CLI
options**, then validates through the shared library; options that were not
supplied never replace preset values. Modulation names (`BPSK`, `QPSK`, `8-PSK`,
`16-QAM`, `64-QAM`, `WGN`) are accepted case-insensitively; pulse names are
`rrc` and `rectangular`. Signal options are `--preset`, `--modulation`,
`--symbols`, `--symbol-rate`, `--sps`, `--pulse`, `--roll-off`, `--span`,
`--gain`, `--seed`, `--data-source random|explicit`, and `--bits`. Noise options
are `--samples`, `--sample-rate`, `--noise-power`, `--noise-seed`, and
`--snr-db <dB>|off`; `--snr-db off` disables preset-provided AWGN. Export
options are `--format csv|cf32`, `--output`/`-o`, and `--overwrite`. Without an
output path, CSV uses `signal.csv` and binary uses `signal.iq`.

Options incompatible with the selected waveform are rejected: noise-source
options require `--modulation WGN`, and linear options (including `--snr-db`)
are rejected for WGN. Switching waveform families through `--modulation` starts
from that family's defaults while retaining gain and seeds; incompatible preset
fields never silently become active. `--bits` selects explicit input unless a
conflicting `--data-source random` was explicitly supplied. Preset and signal
options may initialize the GUI (`--gui`) without generating; export options and
the `batch` subcommand cannot be combined with `--gui`. The destination's parent
directory must exist, and existing sample or metadata files are protected unless
`--overwrite` is specified. Generation or export errors return a nonzero exit
status.

The GUI keeps the generator controls and plots inside the main application
window, including on Wayland. The content follows window resizing and scrolls
when needed; detached platform windows and saved floating-panel positions are
disabled.

### Batch dataset generation

`siggen batch` sweeps waveform, seed, and SNR lists and writes fixed-length
frames into a new output directory, sharing preset loading and applicable
signal overrides with single generation:

```sh
./build/dev/bin/siggen batch --modulations BPSK QPSK 8-PSK 16-QAM 64-QAM WGN \
  --seeds 42 43 --snrs-db=-10,0,10 \
  --frame-size 2048 --frames-per-point 100 \
  --output-dir dataset --format cf32
```

Batch defaults are the resolved waveform and seed (or the explicit lists), one
frame per point, 2048 samples, and binary `cf32` frames. Lists preserve user
order and reject duplicates; `--snrs-db` accepts a comma-separated list (use the
`--snrs-db=-10,0,10` form for negative values). Without an SNR list, the
resolved single-signal noise setting is retained. WGN ignores the SNR axis: it
is generated once per seed and frame at the configured power with a null SNR
label. Batch linear input is always seeded random data; `--bits`,
`--data-source`, `--symbols`, and `--samples` are rejected because the frame
size determines generation length, and batch `--overwrite` is rejected.
Put preset, signal, and format options after `batch`; root options before the
subcommand are rejected except for the global `--log-level`. Mixed waveform
lists accept options for either included family. A preset supplies its active
family's settings; other families start from defaults before CLI overrides.

Each linear frame of length `F` is generated from `N = ceil(F / SPS)` payload
symbols plus `G = span_symbols` RRC guard symbols on each side (rectangular
frames need no guards), then cropped to `F` samples beginning at
`G * SPS + filter_delay_samples`. AWGN is applied after cropping with the
reference power measured over the retained clean frame. Frame timestamps start
at zero; the sidecar records the crop offset and original filter delay
separately. WGN frames generate `F` samples directly. Frames are produced
sequentially, holding only one frame in memory.

Per-frame seeds derive deterministically through `std::seed_seq` from
`{base seed, waveform ID, frame index, stream tag}` with fixed tags `data = 1`
and `noise = 2`; noise derivation additionally includes the configured noise
seed. Waveform IDs are fixed explicitly in the descriptor table: BPSK 0,
QPSK 1, 8-PSK 2, 16-QAM 3, 64-QAM 4, WGN 5. Reordering the table or enum does
not change these IDs. SNR is deliberately excluded from seed derivation, so all SNR
points of one (waveform, seed) sweep share the same underlying data and noise
realizations.

The output directory must not exist; existing directories are never touched.
Files are named `frame_<point>_<frame>` with zero-padded indices preserving
user list order, each with a JSON sidecar (`<file>.json`). A versioned
`manifest.jsonl` starts with a `batch_header` record, gains one `frame`
completion record (relative path, waveform, axis values, derived seeds, frame
size, format, and crop/noise provenance) appended and flushed only after that
frame's files close successfully, and ends with a `summary` record marked
`completed`. A request is capped at 100,000 frames and retains the per-
generation symbol/sample limits, including guard symbols; the complete sweep and
all size arithmetic are validated before any output is created. Generation or
write errors fail fast with a nonzero exit status and preserve completed
frames; an interrupted run simply lacks its completion summary, and files
without completion records are not advertised as valid. Resume and automatic
cleanup are not implemented.

AWGN metadata represents the half-open reference interval as a JSON object:
`{"begin": 0, "end": 2048}` includes sample 0 and excludes sample 2048.
Preflight rejects zero-gain AWGN and SNR ratios that overflow or underflow.
Sample-dependent numerical failures remain generation errors and follow the
same incomplete-run rules as write failures.

Both `dev` and `release` produce the same dual-mode executable. The `headless`
presets build `siggen` without graphics dependencies; `--gui` then reports that
GUI support was not compiled in. The former `imsignalgenerator` executable is now
named `siggen`; old build artifacts are not removed automatically.

[CLI11 v2.6.2](https://github.com/CLIUtils/CLI11/releases/tag/v2.6.2) is pinned as a
Git submodule. It parses options before graphics initialization, so help, version,
and argument errors work without a display and do not generate output files.
Invalid levels, missing values, and unknown options fail with a usage error.

spdlog writes timestamped, leveled console logs to stderr (color when supported).
`--log-level` accepts `trace`, `debug`, `info`, `warn`, `error`, `critical`, or `off`.
The CLI level takes precedence over `SPDLOG_LEVEL`; the environment variable
remains a fallback, followed by the default `info`. Debug adds initialization and
generation-start details. For example:

```sh
./build/headless/bin/siggen --output signal.csv --log-level debug
```

Use `--log-level warn` for warnings and errors or `--log-level off` for silence.
Redirect stderr to keep a log file if needed. Logs include paths for preset/export
operations but do not dump I/Q samples or explicit input bits. Logging is
synchronous with a thread-safe sink; warnings and errors flush immediately.
GUI errors remain visible, and per-frame validation does not generate logs.
CLI11 and spdlog are used by both application modes, but library-only builds need
neither. Vendor examples, tests, documentation, benchmarks, and installation
default off.

## Using the generator

The generator opens on startup, optionally initialized from `--preset` and
signal options supplied with `--gui`; it does not generate automatically. Enter
modulation, symbol count, symbol rate in baud, samples per symbol (SPS), and
amplitude gain in **Signal Setup**. Choose RRC or rectangular pulses in **Pulse
Shaping**. The **AWGN** section enables additive noise with a requested SNR in
dB; the visible data seed defaults to 5489 and the noise seed to 5490.
**Advanced** selects seeded random bits or exact explicit binary input (up to
six bits per symbol). Selecting **WGN** replaces the symbol controls with the
noise source: sample count, sample rate, total complex noise power, and noise
seed.

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
characters, each `0` or `1` (no whitespace), up to 393,216 bits for 64-QAM.

## Mapping and reproducibility

Mapped symbols have **unit average constellation energy**, before amplitude gain.
A particular random buffer need not have exactly unit empirical symbol energy.
There is no per-buffer renormalization. QPSK has two bits per symbol; 8-PSK
three; 16-QAM four; 64-QAM six. Bits are consumed left to right in symbol order.

| Modulation | Bits | Complex symbol before gain |
|---|---|---|
| BPSK | 0 | +1 + j0 |
| BPSK | 1 | -1 + j0 |
| QPSK | 00 | (+1 + j)/sqrt(2) |
| QPSK | 01 | (+1 - j)/sqrt(2) |
| QPSK | 11 | (-1 - j)/sqrt(2) |
| QPSK | 10 | (-1 + j)/sqrt(2) |

8-PSK uses unit radius and a fixed zero phase origin. At phases `k*pi/4`,
k = 0..7 counterclockwise, the Gray labels are:

| Phase index k | Bits |
|---|---|
| 0 | 000 |
| 1 | 001 |
| 2 | 011 |
| 3 | 010 |
| 4 | 110 |
| 5 | 111 |
| 6 | 101 |
| 7 | 100 |

Every adjacent pair, including the 7→0 wrap-around, differs in exactly one bit.

For 16-QAM, bits `b0 b1` select I and `b2 b3` select Q independently:

| Axis bit pair | Axis level before gain |
|---|---|
| 00 | -3/sqrt(10) |
| 01 | -1/sqrt(10) |
| 11 | +1/sqrt(10) |
| 10 | +3/sqrt(10) |

For 64-QAM, the first three bits select I and the next three select Q. Axis
levels in increasing amplitude order, divided by `sqrt(42)` for unit average
constellation energy:

| Axis bits | Unscaled level |
|---|---:|
| 000 | -7 |
| 001 | -5 |
| 011 | -3 |
| 010 | -1 |
| 110 | +1 |
| 111 | +3 |
| 101 | +5 |
| 100 | +7 |

These mappings make horizontal/vertical nearest neighbors differ by one bit.
QPSK, 8-PSK, 16-QAM and 64-QAM have defined I and Q components; BPSK Q is
zero. Gain scales sample amplitude once; power scales by gain squared.

Random input uses `std::mt19937(seed)`, consuming one engine output per bit and
mapping its least significant bit to `0` or `1`. It does not use an
implementation-dependent distribution. The first five bits for seed 5489 are
`00010`. Fixed settings reproduce bits and mapping across conforming engines;
small floating-point differences in filtering can occur across compilers or
math libraries, so cross-platform byte-identical samples are not guaranteed.

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

## Views and spectrum calibration

**Waveform** displays I in blue and Q in orange against seconds. A cached reduction
keeps both I and Q extrema in chronological order, with at most 4096 points. This
is a bounded overview; deep zoom cannot restore omitted samples. Export contains
all original samples. **Constellation** separates mapped ideal symbols from
matched-filter observations and constrains equal units per pixel on the axes;
with AWGN enabled the observation view is explicitly labeled as noisy. WGN
results show waveform, spectrum and power statistics without any symbol
constellation.

**Spectrum** uses full complex samples, independent of waveform reduction. The
Welch estimator uses a periodic Hann window, 50% overlap, no mean subtraction,
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

## Presets

The **Presets** section accepts a complete path (default `default.preset`). Save
writes the versioned text format below. Load parses into a temporary configuration
and applies it only after every field passes validation. Errors leave current
settings and the previous result intact. Saving replaces the selected preset.

```ini
[IQ Generator Preset]
Version=2
Modulation=BPSK
NumberOfSymbols=256
SymbolRateBaud=1000
SamplesPerSymbol=8
Pulse=RRC
RRCBeta=0.20000000000000001
SpanSymbols=10
AmplitudeGain=1
DataSource=Random
Seed=5489
Bits=
NoiseSampleCount=2048
NoiseSampleRateHz=8000
NoisePower=1
AwgnEnabled=false
AwgnSnrDb=10
NoiseSeed=5490
```

All version-2 fields are required. Modulation accepts `BPSK`, `QPSK`, `8-PSK`,
`16-QAM`, `64-QAM`, `WGN` case-insensitively; pulse accepts `RRC`,
`Rectangular`; source accepts `Random`, `Explicit`; booleans accept `true`,
`false`. Numbers use a locale-independent decimal point.
Unknown/duplicate fields, unsupported versions, non-finite numbers, and trailing
numeric junk are rejected. `Bits` must be empty or binary, even when dormant in
random mode. Fields belonging to an inactive waveform family persist but are
still validated, so presets fail transactionally. Files are limited to 1 MiB.
LF and CRLF are supported.

Version-1 presets and legacy `[SignalGenerator Preset]` files continue to
import, receiving noise-disabled defaults (`AwgnEnabled=false`, `NoiseSeed=5490`
and the WGN defaults above); version-1 files must not contain version-2 fields
or the new waveform names. Legacy files import `NumberOfSymbols`,
`SamplesPerSymbol`, and `RRCBeta`; the obsolete `ConstellationStart` field is
ignored. Legacy settings still must satisfy current numerical validation. New
saves always use version 2.

## I/Q export

**Export I/Q...** captures the last completed result when the dialog opens.
Choose a destination path and format. The dialog checks both sample and metadata
paths and requires explicit confirmation to overwrite. Extension selection is
manual. Exporting while generation is pending exports the captured completed
result, even if a newer result finishes while the dialog is open.

CSV is UTF-8/ASCII text with header `time_s,i,q`, followed by one complex sample
per row. Decimal precision preserves the stored float32 components and double
timestamps on round trip. Binary is headerless little-endian IEEE-754 float32,
interleaved as `I0,Q0,I1,Q1,...`, eight bytes per complex sample.

Both formats write `<destination>.json`. Its version-2 object records the
`format` (`csv` or `cf32_le`), `waveform`, `family` (`linear` or `noise`),
`sample_count`, `sample_rate_hz`, `scale` (gain already applied),
`sample_units`, and `timing`. Linear results add `symbol_energy`, the complete
`configuration` (including seed, explicit bits and PRNG rule), and an `awgn`
provenance block (requested SNR, reference power and interval, added noise
power, noise seed) when noise was applied. Noise results add a `noise_source`
block instead and never claim symbol energy or matched-symbol observations. Do
not multiply exported samples by `scale` again. Write, flush and close failures
are reported. The two-file operation is not atomic; a storage failure can leave
partial files, including after confirmed replacement. Retry after correcting the
destination.

Example binary import with NumPy:

```python
import json
import numpy as np
with open("signal.iq.json", encoding="utf-8") as f:
    metadata = json.load(f)
values = np.fromfile("signal.iq", dtype="<f4").reshape(-1, 2)
iq = values[:, 0] + 1j * values[:, 1]
assert len(iq) == metadata["sample_count"]
```

## Architecture and validation

- `library/waveform.*`: waveform descriptor table (identifiers, families,
  capabilities) shared by CLI, GUI, validation, serialization and analysis.
- `library/generator.*`: configuration, validation, mapping and shaping.
- `library/noise.*`: documented Box–Muller Gaussian source and complex WGN.
- `library/batch.*`: fixed-length frame generation, seed derivation and the
  manifest-writing batch writer.
- `library/signal_processing.*`: stable RRC taps and convolution.
- `library/signal_analysis.*`: matched observations and Welch PSD.
- `library/preset.*`, `library/iq_export.*`: validated file I/O.
- `application/command_line.h`, `application/cli_config.*`: CLI parsing and
  defaults → preset → explicit-option resolution.
- `application/generation_job.h`: owned worker and immutable completed snapshot.
- `application/plot_data.h`, `application/signal_generator.*`: cached presentation
  and UI-thread state. Analysis/cache preparation runs once on result completion;
  generation is asynchronous, while export and cache preparation are synchronous.

CLI tests exercise argument validation, configuration precedence, family
switching, incompatible-option rejection, batch resolution, and the real
executable without a display, including default CSV export, metadata, overwrite
protection, batch manifests, and log-level precedence.

Tests combine analytical limits and independent fixtures with mapping/Gray
adjacency for every constellation, deterministic bit vectors, shaping
superposition, matched timing, PSD power/sign, preset transactionality and
version migration, export precision, noise determinism, independent streams,
statistical mean/variance/correlation, SNR tolerances, zero-power rejection,
exact frame lengths, guard/crop timing, derived-seed pairing, manifest
ordering, and worker lifetime.
GUI tests exercise actual ImGui frames, window sizes, all views and failure states
without requiring a display server by default. Optional real OpenGL captures:

```sh
SIGGEN_CAPTURE_DIR=build/ui-captures \
  build/dev/bin/siggen_ui_tests --gtest_filter=UI.GeneratedViewsAndPendingClosure
```

That command needs a working display and saves screenshots of each modulation's
waveform, mapped constellation, matched constellation and spectrum.

Run the separate sanitizer workflows:

```sh
cmake --workflow --preset asan
cmake --workflow --preset tsan
```

`SIGGEN_SANITIZER` accepts exactly `none`, `address-undefined`, or `thread`.
Instrumentation is applied to project targets through compile and link options,
including static-library consumers; vendor compilation and global CMake flags are
untouched. Sanitizers require Linux with GCC or Clang and a successful compiler
and linker probe. Address/undefined and thread instrumentation cannot be combined.
The shipped sanitizer presets use GCC. Compiler/linker support does not guarantee
that the runtime can operate under a particular kernel or sandbox.

Leak detection remains enabled. LeakSanitizer cannot run under ptrace, and
sanitizer tests may need to run outside a ptrace-based sandbox. Do not disable
leak detection to make that environment pass. Windows/macOS validation remains
deferred; Windows resources and MSVC runtime handling are preserved.

This iteration excludes SDR streaming, RF carrier synthesis, analog modulation,
channel impairments and eye diagrams. FSK/MSK waveforms, AMCPy-specific dataset
conversion, GUI batch controls, batch resume and automatic cleanup remain
deferred. Large RRC spans take longer to generate;
there is no cancellation, and application closure waits for generation. The
waveform is an overview, the spectrum is a finite-buffer estimate, and exports
have no hardware timing or calibration guarantees.

## License

Siggen is distributed under the GNU General Public License, version 3. See
[`LICENSE`](LICENSE) for the complete license text.
