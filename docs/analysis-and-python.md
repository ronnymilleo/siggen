# Analysis, SigMF and Python

[Back to the README](../README.md)

Generated signals can leave siggen as [SigMF](https://sigmf.org) recordings, be measured again, and be
scripted from Python with NumPy. Everything uses the same C++ library as the GUI, so the numbers agree
everywhere.

## SigMF recordings

```python
import siggen

siggen.generate(modulation="16-QAM", snr_db=20).export("tx.sigmf-data", "sigmf")   # also writes tx.sigmf-meta
```

The format is described in [Presets and I/Q export](presets-and-export.md#sigmf). siggen
reads two datatypes back: `cf32_le` and `ci16_le` (scaled by 1/32768), so recordings from
SDR tools can be analyzed too; the file is checked for a whole number of samples and for
non-finite values.

## Analyzing a recording

`siggen.load` reads a SigMF recording (`.sigmf-meta` or `.sigmf-data`, including the batch frames below)
back into a `Signal`; the measurements are the same estimators the GUI shows:

```python
rx = siggen.load("tx.sigmf-meta")
len(rx.samples), rx.sample_rate                  # sample count and rate
rx.power_statistics()                            # {'mean_power', 'peak_power', 'papr_db'}
f, d = rx.psd(window="blackman", segment=2048)   # two-sided Welch PSD
rx.symbol_accuracy()   # SymbolAccuracy(evm_percent, evm_db, snr_after_matched_db, ...), or None
rx.bit_errors()        # BitErrors(bit_errors, bit_count, ber, ...) from the reference receiver, or None
```

`bit_errors` come from the [reference receiver](ber-and-receiver.md) run on the file's samples; the
measured Eb/N0 is derived from the EVM, and `siggen.theoretical_ber(waveform, eb_n0_db)` gives the textbook
value at that Eb/N0 (NaN where no closed form exists).

Power, PAPR and the spectrum need only the samples. **EVM and SNR need the symbols**, so `symbol_accuracy`
and `bit_errors` return a value only when the file carries siggen's configuration (a SigMF file written by
`Signal.export`) and its length matches it: siggen regenerates the ideal symbols from `siggen:preset` and
measures the *file's* samples against them. If the samples were modified after export, the measurement
reflects the modification. A recording made by another tool, or a cropped batch frame, has samples,
`sample_rate` and `psd` but no whole-signal configuration, so `preset` is `None` and `symbol_accuracy` and
`bit_errors` return `None`; the desktop analyzer scores a batch frame's interior from its `siggen:frame`
metadata instead.

The occupied bandwidth is the span between the points where 0.5 % of the Welch power lies
below and above, rounded outward by one bin. The EVM is a coherent measurement against the
transmitted constellation: with a carrier offset it measures the rotation, not the
errors a differential receiver would make.

## Python package

The package is `python/siggen`: pure Python with `ctypes` over a small C interface, so it
needs only NumPy and no compiler for the Python side. Install a prebuilt wheel from a
[release](https://github.com/ronnymilleo/siggen/releases) on Python 3.10 or newer:

```sh
pip install siggen-*.whl
python3 -c "import siggen; print(siggen.version())"
```

To build the shared library from source instead, configure with `-DSIGGEN_BUILD_PYTHON=ON`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_PYTHON=ON
cmake --build build
cd python && python3 -c "import siggen; print(siggen.version())"
```

An installed wheel carries `libsiggen_c` inside the package; a source build finds it under `out/*/lib`.
Set `SIGGEN_LIBRARY` to point at the library explicitly if neither is discovered.

```python
import siggen

tx = siggen.generate(modulation="16-QAM", sps=8, snr_db=20, cfo_hz=2)
tx.samples            # complex64 array, identical to a `sigmf` export
tx.symbols            # ideal mapped symbols (unit mean energy)
tx.symbol_accuracy()  # SymbolAccuracy(evm_percent, evm_db, snr_after_matched_db, ...)
tx.bit_errors()       # BitErrors(bit_errors, bit_count, ber, ...) from the ideal reference receiver
obs, idx = tx.matched_symbols()   # decision observations and their symbol indices
t, i, q = tx.eye()                # eye-diagram traces
f, d = tx.psd(window="blackman")  # Welch PSD, as in the Spectrum tab

noisy = tx.with_samples(tx.samples + noise)   # same configuration, edited samples
tx.export("tx.sigmf-data", "sigmf")           # csv or sigmf
rx = siggen.load("tx.sigmf-meta")             # any SigMF recording; siggen ones keep their configuration
```

`siggen.theoretical_ber("16-QAM", eb_n0_db)` returns the textbook BER (scalar or array, NaN where there is none) and
`siggen.ber_curve(eb_n0_db, modulation="16-QAM", ...)` runs the reference-receiver BER sweep, returning a `BerCurve` with
`eb_n0_db`, `snr_db`, `bits`, `bit_errors`, `ber` and `theory` arrays.

`generate` also accepts `preset=` (a preset path or text); keyword options override it.
`siggen.default_config()` lists the options and their defaults. `snr_db=None`
disables AWGN and `bits="0110..."` selects explicit data. Errors raise `ValueError`.

`symbol_accuracy`, `eye` and `matched_symbols` need the generated length and timing; after
`with_samples` they work only if the new array has the same length. A recording made by another
tool has samples, `sample_rate` and `psd` but no configuration (`preset` is `None`).

Tests live in `python/tests` and run under `ctest` as `Python.Package` when NumPy is installed
for the Python found by CMake.

## Batch datasets

`siggen.batch` sweeps waveform, seed and SNR lists and writes fixed-length frames into a new output
directory, for classifier training and dataset work. It shares the preset and keyword overrides of
`generate`, and returns a `BatchResult` with the point and frame counts:

```python
import siggen

result = siggen.batch(
    "dataset",
    modulations=["BPSK", "QPSK", "8-PSK", "16-QAM", "64-QAM", "WGN"],
    seeds=[42, 43],
    snrs_db=[-10, 0, 10],
    frame_size=2048,
    frames_per_point=100,
    format="sigmf",
)
result.point_count, result.frame_count
```

| Argument | Values | Default | Notes |
|---|---|---|---|
| `output_dir` | path | — | Positional, required: new output directory; must not exist |
| `modulations` | waveform list | resolved waveform | Sweep axis |
| `seeds` | seed list | resolved seed | Sweep axis |
| `snrs_db` | SNR list | resolved SNR | Sweep axis; WGN ignores it |
| `frame_size` | samples | 2048 | Samples per frame |
| `frames_per_point` | count | 1 | Frames per sweep point |
| `format` | `csv`, `sigmf` | `sigmf` | Frame format |
| `preset` | path or text | — | Preset loaded before the keyword overrides |

Plus the shared keyword options of [`generate`](#python-package) (`sps`, `pulse`, `roll_off`, `span`,
`gain`, `cfo_hz`, `noise_power`, ...).

Batch rules:

- The output directory must not exist; existing directories are never replaced, and an invalid sweep is
  rejected before any file is written.
- Lists preserve user order and reject duplicates. Without an SNR list, the resolved single-signal noise
  setting is retained.
- WGN ignores the SNR axis: it is generated once per seed and frame at the configured power with a null
  SNR label.
- Linear input is always seeded random data, so `bits` is rejected; the frame size determines the length.
- A preset supplies only its active family's settings: in a sweep that mixes linear waveforms and WGN, the
  other family starts from its defaults before the keyword overrides (`Core::PrepareBatchBase`).

### Frame generation

Each linear frame of length `F` is generated from `N = ceil(F / SPS)` payload symbols plus `G = span_symbols`
RRC guard symbols on each side (rectangular frames need no guards), then cropped to `F` samples beginning at
`G * SPS + filter_delay_samples`. AWGN is applied after cropping with the reference power measured over the
retained clean frame. Frame timestamps start at zero; the sidecar records the crop offset and original filter
delay separately. WGN frames generate `F` samples directly. Frames are produced sequentially, holding only one
frame in memory.

Per-frame seeds derive deterministically through `std::seed_seq` from `{base seed, waveform ID, frame index,
stream tag}` with fixed tags `data = 1` and `noise = 2`; noise derivation additionally includes the configured
noise seed. Waveform IDs are fixed in the descriptor table and never renumbered. SNR is deliberately excluded
from seed derivation, so all SNR points of one (waveform, seed) sweep share the same underlying data and noise
realizations.

### Output files and manifest

Files are named `frame_<point>_<frame>` with zero-padded indices preserving user list order, each with its
metadata file: `<name>.sigmf-meta` for SigMF (any SigMF tool opens the frames; the frame description sits under
`siggen:metadata`, with the generator preset and crop position beside it so the desktop analyzer can report EVM
on the frame interior) or `<file>.json` for CSV. A versioned `manifest.jsonl` starts with a `batch_header`
record, gains one `frame` completion record (relative path, waveform, axis values, derived seeds, frame size,
format, and crop/noise provenance) appended and flushed only after that frame's files close successfully, and
ends with a `summary` record marked `completed`. A request is capped at 100,000 frames and retains the
per-generation symbol/sample limits, including guard symbols; the complete sweep and all size arithmetic are
validated before any output is created. Generation or write errors raise `ValueError` and preserve completed
frames; an interrupted run simply lacks its completion summary. Resume and automatic cleanup are not
implemented.

A frame is a SigMF recording `siggen.load` reads back for its samples and spectrum:

```python
rx = siggen.load("dataset/frame_000000_000000.sigmf-meta")
len(rx.samples)      # frame_size
f, d = rx.psd()
```

## Notebooks

`python/notebooks` holds three notebooks (executed, so they render on GitHub; they need
`numpy`, `matplotlib` and Jupyter):

1. **Generate and measure**: waveform, constellation, eye and spectrum of a 16-QAM signal,
   EVM against SNR compared with theory (including the RRC truncation floor), and the Welch windows.
2. **SigMF files and carrier offset**: a SigMF round trip, and QPSK versus DQPSK symbol
   errors under a carrier offset, showing why EVM alone does not say how well a receiver
   would do.
3. **Bit error rate and the reference receiver**: BER against Eb/N0 for several modulations compared with theory,
   the price of differential detection, and how a carrier offset breaks coherent schemes.

Run them from `python/notebooks` after building the library.

## Where this leads

`matched_symbols()`, the ideal symbols and the reference receiver are the starting point for what a real receiver
adds: carrier recovery, timing recovery and soft decisions can be prototyped in NumPy against the generator and
compared with the curves in [Bit error rate and the reference receiver](ber-and-receiver.md).
