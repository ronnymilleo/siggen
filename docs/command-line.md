# Command line, logging and batch datasets

[Back to the README](../README.md)

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
`16-QAM`, `32-QAM`, `64-QAM`, `256-QAM`, `OOK`, `4-PAM`, `DBPSK`, `DQPSK`,
`pi/4-DQPSK`, `8-DPSK`, `OQPSK`, `4-ASK`, `2-FSK`, `4-FSK`, `MSK`, `WGN`) are accepted case-insensitively; pulse names are
`rrc` and `rectangular`. Signal options are `--preset`, `--modulation`,
`--symbols`, `--symbol-rate`, `--sps`, `--pulse`, `--roll-off`, `--span`,
`--tone-spacing-hz` (2-FSK and 4-FSK only), `--gain`, `--seed`,
`--data-source random|explicit`, and `--bits`. Noise options
are `--samples`, `--sample-rate`, `--noise-power`, `--noise-seed`, and
`--snr-db <dB>|off`; `--snr-db off` disables preset-provided AWGN. Channel
impairment options for linear waveforms are `--cfo-hz`, `--phase-noise-hz`,
`--iq-gain-db`, `--iq-phase-deg`, `--dc-i`, `--dc-q`, `--adc-bits`, and
`--impairment-seed` (see [Noise, SNR and channel impairments](noise-and-impairments.md)). Export
options are `--format csv|cf32`, `--output`/`-o`, and `--overwrite`. Without an
output path, CSV uses `signal.csv` and binary uses `signal.iq`.

Options incompatible with the selected waveform are rejected: noise-source
options require `--modulation WGN`, and linear options (including `--snr-db`)
are rejected for WGN. Pulse options (`--pulse`, `--roll-off`, `--span`) are
rejected for FSK and MSK, which have no pulse filter, and `--tone-spacing-hz`
is rejected for every other waveform (MSK fixes the spacing). Switching waveform families through `--modulation` starts
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
