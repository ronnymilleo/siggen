# Command line, logging and batch datasets

[Back to the README](../README.md)

## Command line

The desktop application and the command-line tool are separate executables.
`siggen` opens the GUI and takes no arguments; `siggen-cli` is the tool covered
here. With no arguments it generates the default BPSK signal and exports
`signal.csv` plus `signal.csv.json` in the current directory, then exits:

```sh
./out/debug/bin/siggen                      # opens the desktop application
./out/debug/bin/siggen-cli
./out/debug/bin/siggen-cli --output captures/example.csv
./out/debug/bin/siggen-cli --output signal.csv --overwrite
./out/debug/bin/siggen-cli --modulation 64-QAM --symbols 512 --sps 4 --format sigmf
./out/debug/bin/siggen-cli --modulation qpsk --snr-db 10 --noise-seed 7 -o noisy.csv
./out/debug/bin/siggen-cli --modulation wgn --samples 8192 --sample-rate 16000 --noise-power 2
./out/debug/bin/siggen-cli --preset custom.preset --gain 2 --output preset.csv
./out/debug/bin/siggen-cli --help
./out/debug/bin/siggen-cli --version
```

Configuration resolves as **defaults → loaded preset → explicitly supplied CLI
options**, then validates through the shared library; options that were not
supplied never replace preset values.

### Waveforms

`--modulation` accepts these names, case-insensitively. Switching families
starts from that family's defaults while retaining gain and seeds; incompatible
preset fields never silently become active.

| Family | Waveforms |
|---|---|
| Linear | `BPSK`, `QPSK`, `8-PSK`, `16-QAM`, `32-QAM`, `64-QAM`, `256-QAM`, `OOK`, `4-PAM`, `4-ASK`, `DBPSK`, `DQPSK`, `pi/4-DQPSK`, `8-DPSK`, `OQPSK` |
| FSK | `2-FSK`, `4-FSK`, `MSK` |
| Noise | `WGN` |

### Signal options

| Option | Values | Notes |
|---|---|---|
| `--preset` | file path | Preset loaded before the explicit options |
| `--modulation` | waveform name | See the table above |
| `--symbols` | count | Symbol count (linear and FSK) |
| `--symbol-rate` | baud | Symbol rate (linear and FSK) |
| `--sps` | count | Samples per symbol (linear and FSK) |
| `--pulse` | `rrc`, `rectangular` | Pulse shape (linear only) |
| `--roll-off` | 0–1 | RRC roll-off |
| `--span` | symbols | RRC span |
| `--tone-spacing-hz` | Hz | FSK tone spacing, default 1000 (2-FSK/4-FSK only; MSK fixes it to symbol rate / 2) |
| `--gain` | 0–1000000 | Amplitude gain, applied once |
| `--seed` | uint32 | Data PRNG seed |
| `--data-source` | `random`, `explicit` | Input bits source |
| `--bits` | binary digits | Explicit input; selects `explicit` data |

### Noise options

| Option | Values | Notes |
|---|---|---|
| `--snr-db` | dB, or `off` | AWGN on linear waveforms; `off` disables preset-provided AWGN |
| `--samples` | count | WGN sample count |
| `--sample-rate` | Hz | WGN sample rate; sets the spectrum frequency axis |
| `--noise-power` | power | WGN total complex noise power before gain |
| `--noise-seed` | uint32 | Noise PRNG seed (WGN source and AWGN) |

### Channel impairments

Linear waveforms only; see
[Noise, SNR and channel impairments](noise-and-impairments.md).

| Option | Values | Notes |
|---|---|---|
| `--cfo-hz` | Hz | Carrier frequency offset |
| `--phase-noise-hz` | Hz | Oscillator phase-noise 3 dB linewidth |
| `--iq-gain-db` | dB | IQ gain imbalance: Q gain relative to I |
| `--iq-phase-deg` | degrees | IQ quadrature skew |
| `--dc-i` | fraction | DC offset on I, as a fraction of the RMS amplitude |
| `--dc-q` | fraction | DC offset on Q, as a fraction of the RMS amplitude |
| `--adc-bits` | 2–24 | Quantizer bits per component; 0 disables |
| `--impairment-seed` | uint32 | Phase-noise PRNG seed |

### Export options

| Option | Values | Notes |
|---|---|---|
| `--format` | `csv`, `sigmf` | Single-generation output format |
| `--output`, `-o` | file path | Default: `signal.csv`, or `signal.sigmf-data` (plus `signal.sigmf-meta`) |
| `--overwrite` | flag | Replace existing sample and metadata files |

### General options

| Option | Values | Notes |
|---|---|---|
| `--log-level` | `trace`–`off` | Console log level; overrides `SPDLOG_LEVEL`, default `info` (see [Logging](#logging)) |
| `--help` | flag | Print usage and exit |
| `--version` | flag | Print the version and exit |

### Compatibility rules

- Noise-source options (`--samples`, `--sample-rate`, `--noise-power`) require
  `--modulation WGN`; linear options (including `--snr-db`) are rejected for WGN.
- Pulse options (`--pulse`, `--roll-off`, `--span`) are rejected for FSK and MSK,
  which have no pulse filter, and `--tone-spacing-hz` is rejected for every other
  waveform (MSK fixes the spacing).
- `--bits` selects explicit input unless a conflicting `--data-source random`
  was explicitly supplied.
- The destination's parent directory must exist, and existing sample or metadata
  files are protected unless `--overwrite` is specified.
- Generation or export errors return a nonzero exit status.

## Analyzing a recording

`siggen-cli analyze <file>` measures a SigMF recording (`.sigmf-meta` or `.sigmf-data`, including batch frames), using the same
estimators: sample count and rate, mean power, PAPR, the strongest spectral bin, the
99 % occupied bandwidth and, for a recording made by siggen, the waveform, EVM,
SNR after the matched filter, the measured Eb/N0 and the bit errors of the reference receiver. See
[Analysis, SigMF and Python](analysis-and-python.md).

| Option | Values | Default | Notes |
|---|---|---|---|
| `file` | path | — | Positional, required: SigMF `.sigmf-meta` or `.sigmf-data` |
| `--window` | `hann`, `hamming`, `blackman`, `rectangular` | `hann` | Welch window |
| `--segment` | count | 1024 | Welch segment length in samples (minimum 4) |
| `--json` | flag | off | Print the report as JSON |

No signal or export option applies to `analyze`; passing one is an error.

## Measuring BER

`siggen-cli ber` sweeps Eb/N0 and measures the bit error rate of the ideal reference receiver, next
to the textbook value. It takes the same signal, noise and impairment options as single generation
(`--modulation`, `--sps`, `--pulse`, `--cfo-hz`, ...), but `--symbols`, `--bits`, `--data-source` and
`--snr-db` are rejected because the sweep controls them. Only linear waveforms are accepted.

```sh
siggen-cli ber --modulation QPSK --eb-n0-db=0,2,4,6,8
siggen-cli ber --modulation DQPSK --eb-n0-db=4,8 --cfo-hz 20 --json
```

```text
waveform: QPSK, 8 samples/symbol, RRC pulse, ideal reference receiver
 Eb/N0 dB   SNR dB       bits     errors          BER       theory
     0.00     -6.02       8152        637    7.814e-02    7.865e-02
```

| Option | Values | Default | Notes |
|---|---|---|---|
| `--eb-n0-db` | comma list | 0 to 10 in steps of 2 | Sweep points in dB |
| `--min-errors` | count | 100 | Stop a point after this many bit errors |
| `--max-bits` | count | 2000000 | Stop a point after this many bits |
| `--block-symbols` | count | 4096 | Symbols generated per block |
| `--json` | flag | off | Print the curve as JSON |

See [Bit error rate and the reference receiver](ber-and-receiver.md).

## Batch dataset generation

`siggen-cli batch` sweeps waveform, seed, and SNR lists and writes fixed-length
frames into a new output directory, sharing preset loading and applicable
signal overrides with single generation:

```sh
./out/debug/bin/siggen-cli batch --modulations BPSK QPSK 8-PSK 16-QAM 64-QAM WGN \
  --seeds 42 43 --snrs-db=-10,0,10 \
  --frame-size 2048 --frames-per-point 100 \
  --output-dir dataset --format sigmf
```

| Option | Values | Default | Notes |
|---|---|---|---|
| `--modulations` | waveform list | resolved waveform | Sweep axis, space-separated |
| `--seeds` | seed list | resolved seed | Sweep axis, space-separated |
| `--snrs-db` | comma list | resolved SNR | Sweep axis; use the `--snrs-db=-10,0,10` form for negative values |
| `--frame-size` | samples | 2048 | Samples per frame |
| `--frames-per-point` | count | 1 | Frames per sweep point |
| `--output-dir` | path | `dataset` | New output directory; must not exist |
| `--format` | `csv`, `sigmf` | `sigmf` | Frame format |
| `--overwrite` | flag | — | Rejected; batches never replace an existing directory |
| `--preset` | file path | — | Preset loaded before the explicit batch overrides |

Plus the shared [signal](#signal-options), [noise](#noise-options) and
[impairment](#channel-impairments) options, placed after `batch`.

Batch rules:

- Lists preserve user order and reject duplicates. Without an SNR list, the
  resolved single-signal noise setting is retained.
- WGN ignores the SNR axis: it is generated once per seed and frame at the
  configured power with a null SNR label.
- Linear input is always seeded random data: `--bits`, `--data-source`,
  `--symbols`, and `--samples` are rejected because the frame size determines
  generation length.
- Preset, signal, and format options go after `batch`; root options before the
  subcommand are rejected except for the global `--log-level`.
- Mixed waveform lists accept options for either included family. A preset
  supplies its active family's settings; other families start from defaults
  before CLI overrides.

### Frame generation

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

### Output files and manifest

The output directory must not exist; existing directories are never touched.
Files are named `frame_<point>_<frame>` with zero-padded indices preserving
user list order, each with its metadata file: `<name>.sigmf-meta` for SigMF (any SigMF tool opens the frames; the frame description sits under `siggen:metadata`, with the generator preset and crop position beside it so `siggen-cli analyze` can report EVM on the frame interior) or `<file>.json` for CSV. A versioned
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

## Logging

spdlog writes timestamped, leveled console logs to stderr (color when supported).
The CLI `--log-level` takes precedence over `SPDLOG_LEVEL`; the environment
variable remains a fallback, followed by the default `info`:

| Level | Effect |
|---|---|
| `trace` | Everything |
| `debug` | Initialization and generation-start details |
| `info` | Normal progress (default) |
| `warn` | Warnings and errors only |
| `error` | Errors only |
| `critical` | Critical errors only |
| `off` | Silence |

```sh
./out/debug/bin/siggen-cli --output signal.csv --log-level debug
```

Redirect stderr to keep a log file if needed. Logs include paths for preset/export
operations but do not dump I/Q samples or explicit input bits. Logging is
synchronous with a thread-safe sink; warnings and errors flush immediately.
GUI errors remain visible, and per-frame validation does not generate logs.

## Build and library notes

The default build produces the GUI and the CLI. A headless build
(`-DSIGGEN_BUILD_GUI=OFF`) builds only `siggen-cli` and the tests, without
graphics dependencies. The former `imsignalgenerator` executable is now named
`siggen`; old build artifacts are not removed automatically.

[CLI11 v2.6.2](https://github.com/CLIUtils/CLI11/releases/tag/v2.6.2) is pinned as a
Git submodule. It parses options before graphics initialization, so help, version,
and argument errors work without a display and do not generate output files.
Invalid levels, missing values, and unknown options fail with a usage error.
CLI11 is used by the CLI and spdlog by both executables, but library-only builds need
neither. Vendor examples, tests, documentation, benchmarks, and installation
default off.
