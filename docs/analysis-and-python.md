# Analysis, SigMF and Python

[Back to the README](../README.md)

Generated signals can leave siggen as [SigMF](https://sigmf.org) recordings, be measured
again from the command line, and be scripted from Python with NumPy. All three use the same
C++ library as the GUI, so the numbers agree everywhere.

## SigMF recordings

```sh
siggen --modulation 16-QAM --snr-db 20 --format sigmf -o tx.sigmf-data   # also writes tx.sigmf-meta
```

The format is described in [Presets and I/Q export](presets-and-export.md#sigmf). siggen
reads two datatypes back: `cf32_le` and `ci16_le` (scaled by 1/32768), so recordings from
SDR tools can be analyzed too; the file is checked for a whole number of samples and for
non-finite values.

## `siggen analyze`

```sh
siggen analyze tx.sigmf-meta
siggen analyze tx.sigmf-data --window blackman --segment 2048 --json
```

```text
samples:            2121
sample rate:        8000 Hz
mean power:         0.124863
PAPR:               5.3226 dB
spectrum peak:      -109.375 Hz (-32.1046 dB re 1/Hz)
99% bandwidth:      5273.44 Hz
welch:              Hann window, 3 x 1024 samples
waveform:           QPSK
symbols:            236
EVM:                5.7888 % (-24.7482 dB)
SNR after matched:  24.7482 dB
SNR per sample:     15.7173 dB
```

Power, PAPR and the spectrum need only the samples. **EVM and SNR need the symbols**, so
they appear when the file carries siggen's configuration (a SigMF file written by `siggen --format sigmf` or `siggen batch`) and its
length matches it: siggen regenerates the ideal symbols from `siggen:preset` and measures
the *file's* samples against them. If the samples were modified after export, the report
reflects the modification. Otherwise a `note` line says why EVM is absent. For a batch frame the meta also records the generator preset and where the frame was cut (`siggen:frame`); only symbols whose matched-filter window lies wholly inside the frame are scored, so a frame shorter than a few filter spans may report no EVM.
Errors
(missing file, unsupported datatype, truncated data, malformed JSON) print a message and exit
with status 1.

The occupied bandwidth is the span between the points where 0.5 % of the Welch power lies
below and above, rounded outward by one bin. The EVM is a coherent measurement against the
transmitted constellation: with a carrier offset it measures the rotation, not the
errors a differential receiver would make.

## Python package

The package is `python/siggen`: pure Python with `ctypes` over a small C interface, so it
needs only NumPy and no compiler for the Python side. Build the shared library, which the
`dev` and `headless` presets do (`-DSIGGEN_BUILD_PYTHON=ON` elsewhere):

```sh
cmake --workflow --preset headless
cd python && python3 -c "import siggen; print(siggen.version())"
```

The package finds `libsiggen_c` under `build/*/lib`; set `SIGGEN_LIBRARY` to point at it
explicitly. It is built for Linux and macOS.

```python
import siggen

tx = siggen.generate(modulation="16-QAM", sps=8, snr_db=20, cfo_hz=2)   # CLI option names
tx.samples            # complex64 array, identical to `siggen --format sigmf`
tx.symbols            # ideal mapped symbols (unit mean energy)
tx.symbol_accuracy()  # SymbolAccuracy(evm_percent, evm_db, snr_after_matched_db, ...)
obs, idx = tx.matched_symbols()   # decision observations and their symbol indices
t, i, q = tx.eye()                # eye-diagram traces
f, d = tx.psd(window="blackman")  # Welch PSD, as in the Spectrum tab

noisy = tx.with_samples(tx.samples + noise)   # same configuration, edited samples
tx.export("tx.sigmf-data", "sigmf")           # csv or sigmf
rx = siggen.load("tx.sigmf-meta")             # any SigMF recording; siggen ones keep their configuration
```

`generate` also accepts `preset=` (a preset path or text); keyword options override it.
`siggen.default_config()` lists the options and their defaults, and a signal with a given
configuration is bit-for-bit what the command line produces (a test compares them). `snr_db=None`
disables AWGN and `bits="0110..."` selects explicit data. Errors raise `ValueError`.

`symbol_accuracy`, `eye` and `matched_symbols` need the generated length and timing; after
`with_samples` they work only if the new array has the same length. A recording made by another
tool has samples, `sample_rate` and `psd` but no configuration (`preset` is `None`).

Tests live in `python/tests` and run under `ctest` as `Python.Package` when NumPy is installed
for the Python found by CMake.

## Notebooks

`python/notebooks` holds two notebooks (executed, so they render on GitHub; they need
`numpy`, `matplotlib` and Jupyter):

1. **Generate and measure**: waveform, constellation, eye and spectrum of a 16-QAM signal,
   EVM against SNR compared with theory (including the RRC truncation floor), and the Welch windows.
2. **SigMF files and carrier offset**: a SigMF round trip, and QPSK versus DQPSK symbol
   errors under a carrier offset, showing why EVM alone does not say how well a receiver
   would do.

Run them from `python/notebooks` after building the library.

## Where this leads

`matched_symbols()`, the ideal symbols and recordings on disk are what a reference demodulator
needs: carrier recovery, decisions, bit-error counting and BER against Eb/N0 can be written
and validated in NumPy against the generator, then moved into the library and the GUI.
