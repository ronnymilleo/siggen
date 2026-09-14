# Siggen: configurable generation, linear/noise expansion, and batches

## Handoff status

Implemented on 2026-09-13 on `feat/siggen-generation-batches`, created from
`main` at `dce1c2a`. Stages 1–3 of the summary are delivered: CLI configuration
with GUI initialization, 8-PSK/64-QAM/WGN/optional AWGN, and CLI-only batch
generation with fixed-length frames. Jay #17–20, #23 and new tasks #25–26 are
closed with reports; #21–22 (FSK/MSK) remain open and deferred. Validation
results are recorded in the Jay knowledge base.

The remainder of this document is the delivered specification; deviations are
recorded in Jay task reports (notably: one combined library-core commit for the
family model, both constellations and noise, and JSON `reference_interval`
objects instead of half-open interval notation).

Review fixes (2026-09-13): root signal/export options before `batch` are
rejected rather than silently discarded (`--log-level` remains global).
Mixed-family CLI overrides use all requested waveforms; dormant preset fields
start from family defaults. GUI family changes share the CLI transition helper
and reset hidden explicit-input/AWGN state. Waveform descriptors provide GUI
choices and explicit stable seed IDs. Single and batch AWGN share one checked
implementation; batch preflight rejects zero gain and unrepresentable SNR
ratios before creating output. JSON writers own their streams locally.
`reference_interval` objects retain the specified half-open semantics: `begin`
is inclusive and `end` is exclusive. The combined original commit remains a
delivery-history deviation; these fixes do not rewrite it.

Review-fix validation on Linux: all seven checked-in workflows pass (89 tests
each for `dev`/`release`, 86 each for headless GCC/Clang and sanitizers). ASan
required an approved run outside the ptrace sandbox with leak detection kept
enabled. Library-only builds pass. Added regressions cover family transitions,
mixed-preset precedence, ignored root options, SNR preflight without output,
write failures preserving completed frames, 8-PSK/64-QAM matched recovery and
maximum six-bit input generation. Independent CSV/binary reads agree for all
six waveforms; the default CSV retains baseline SHA-256
`f184619e34e05ae1e27e8359e3f79540f978546c7302745bde5a797ad252bc10`.
GUI validation uses ImGui context tests; real-desktop and non-Linux validation
remain deferred.

This document supersedes the scope, GUI-first assumptions, and
branch/delivery instructions of [the earlier modulation plan](modulation-expansion-plan.md);
that document remains the detailed numerical reference where explicitly noted.

User decisions:

- Implement CLI configuration, waveform expansion, and batch generation.
- Limit expansion to 8-PSK, 64-QAM, WGN, and AWGN; defer FSK/MSK.
- General signal batches, not AMCPy-specific conversion.
- CLI-only batch controls.
- Sweep waveform, seed, and SNR lists with shared preset/CLI settings.
- Produce fixed-length frames, rather than complete signals with filter tails.
- Allow preset and signal CLI options to initialize the GUI.
- In mixed batches, generate WGN separately without an SNR label.
- Batch defaults: 2048 samples, binary float32 I/Q, one frame per point.

## Summary

Implement the first three roadmap items in verified stages:

1. Complete CLI configuration and allow it to initialize the GUI.
2. Add 8-PSK, 64-QAM, WGN, and optional AWGN.
3. Add CLI-only batch generation with fixed-length frames.

FSK/MSK, AMCPy conversion, GUI batch controls, hardware streaming, and additional
waveform families remain deferred.

## CLI and shared configuration

- Preserve `siggen` as the single executable. No arguments retain the current
  default BPSK CSV export; `--gui` opens the interface.
- Add `--preset`, `--modulation`, `--symbols`, `--symbol-rate`, `--sps`, `--pulse`,
  `--roll-off`, `--span`, `--gain`, `--seed`, `--data-source`, `--bits`, and
  `--format csv|cf32`.
- Add noise controls: `--samples`, `--sample-rate`, `--noise-power`,
  `--noise-seed`, and `--snr-db`. Accept `off` for `--snr-db` to disable
  preset-provided AWGN.
- Resolve configuration as **defaults → loaded preset → explicitly supplied CLI
  options**, then validate through the shared library. CLI parsing must not
  replace preset values with unprovided option defaults.
- Accept canonical modulation names case-insensitively: `BPSK`, `QPSK`, `8-PSK`,
  `16-QAM`, `64-QAM`, `WGN`; pulse names are `rrc` and `rectangular`.
- `--bits` selects explicit input unless a conflicting `--data-source random`
  was explicitly supplied. Preserve exact bit-count validation.
- Allow preset and signal options with `--gui`, initializing its controls
  without automatically generating. Export and batch options remain
  incompatible with GUI mode.
- Keep logging precedence unchanged. Preserve `--output` and explicit overwrite
  protection. Without an output path, CSV uses `signal.csv` and binary uses
  `signal.iq`.
- Reject explicitly supplied options incompatible with the selected waveform.
  Switching waveform families starts with that family's defaults while
  retaining common gain/seeds; incompatible preset fields do not silently
  become active.

## Waveform model, persistence, and analysis

- Introduce tagged linear/noise settings and a shared waveform descriptor table.
  CLI, GUI, validation, serialization, and analysis use the same capabilities.
- Preserve existing BPSK/QPSK/16-QAM mappings, random-bit generation, clean
  samples, scaling, and full-tail single-export behavior.
- Implement the numerical contracts already specified in
  `docs/modulation-expansion-plan.md` for:
  - Gray-coded unit-radius 8-PSK.
  - Gray-coded square 64-QAM normalized by `sqrt(42)`.
  - Complex WGN with independent I/Q components, each having variance half the
    configured total noise power.
  - Optional AWGN using clean complex sample power, independent noise
    randomness, and explicitly recorded SNR semantics.
- WGN defaults: 2048 samples, 8000 Hz, unit noise power before gain. Default noise
  seed is 5490. AWGN remains disabled by default.
- Use documented Box–Muller conversion from `mt19937`, with open-interval
  uniforms and fixed pairing order. Do not use `std::normal_distribution`.
- For full linear signals, measure AWGN reference power over the fully
  supported interval: `[span*SPS, symbol_count*SPS)` for RRC and the entire
  signal for rectangular pulses. Reject empty or zero-power reference
  intervals. Do not force realized noise to achieve an exact measured SNR.
- Increase explicit-bit capacity to six bits per symbol while preserving
  existing allocation limits.
- Write version-2 presets and metadata. Continue importing legacy/version-1
  presets, assigning noise-disabled defaults. Keep CSV columns and binary byte
  layout unchanged.
- Metadata records waveform family, applicable settings, noise provenance,
  reference power/interval, and timing. WGN must not claim symbol energy or
  matched-symbol observations.
- GUI exposes both new constellations and noise controls. Preserve ideal versus
  noisy-observation distinctions; show WGN waveform, PSD, and power statistics
  without a symbol constellation.
- Keep cropped batch frames distinct from full generated signals so export
  validation does not confuse their lengths or timing.

## Batch generation

Add a `batch` subcommand:

```sh
siggen batch --modulations BPSK QPSK 8-PSK 16-QAM 64-QAM WGN \
  --seeds 42 43 --snrs-db=-10,0,10 \
  --frame-size 2048 --frames-per-point 100 \
  --output-dir dataset --format cf32
```

- Share preset loading and applicable signal overrides with single generation.
- Sweep explicit waveform, seed, and SNR lists. Defaults are the resolved
  waveform/seed, one frame per point, 2048 samples, and `cf32`.
- Without an SNR list, retain the resolved single-signal noise setting. WGN
  ignores the SNR axis: generate it once per seed/frame at configured power,
  with a null SNR label.
- Batch linear input is seeded random data. Reject explicit-bit input and
  explicitly supplied symbol/sample counts because frame size determines
  generation length.
- Generate independent frames sequentially, holding only one frame and its
  bounded working buffers in memory.
- For RRC frames of length `F`, use `G=span_symbols` guard symbols on each side
  and `N=ceil(F/SPS)` payload symbols. Generate `N+2G` symbols and crop `F`
  samples beginning at `G*SPS + filter_delay_samples`. Rectangular frames need
  no guards; WGN directly generates `F` samples.
- Apply batch AWGN after cropping, measuring reference power over the retained
  clean frame. Frame timestamps start at zero; metadata separately records
  crop offset and original filter delay.
- Derive per-frame data/noise seeds using `std::seed_seq` from the supplied base
  seed, waveform ID, frame index, and distinct stream tags. Include the
  configured noise seed in noise derivation. Fix and document these IDs/tags;
  exclude SNR from derivation so SNR comparisons share underlying data and
  noise realizations.
- Require a new output directory. Reject batch `--overwrite`; leave existing
  directories untouched. Validate the complete sweep and checked size
  arithmetic before creating outputs. Cap a request at 100,000 frames and
  retain existing per-generation symbol/sample limits, including guards.
- Write deterministic indexed filenames, per-frame JSON sidecars, and a
  versioned `manifest.jsonl`. Preserve user list order; reject duplicate axis
  entries.
- Manifest records contain relative paths, waveform, axis values, derived
  seeds, frame size, format, and crop/noise provenance. Append and flush each
  completion record only after its files close successfully; append a final
  completion summary.
- Fail fast on generation/write errors with a nonzero exit status. Preserve
  completed frames. An interrupted run lacks its completion summary; files
  without completion records are incomplete and are not advertised as valid.
  Resume and automatic cleanup are deferred.

## Delivery and verification

- Update Jay #17–20 for the revised CLI-first scope. Narrow #23 to linear/noise
  analysis and validation; leave #21–22 open and deferred. Add tasks for CLI
  configuration and batch generation.
- Use buildable commits for: shared CLI configuration, family/persistence
  changes, 8-PSK, 64-QAM, noise, frame/batch generation, and final
  integration/documentation. Record validation in Jay and push verified
  commits to the new branch.
- Test CLI/preset precedence, GUI initialization, invalid combinations, legacy
  imports, version-2 round trips, and unchanged default exports.
- Verify every constellation point, Gray adjacency, normalization, six-bit
  limits, and existing clean-output regression fixtures.
- Test noise determinism, independent streams, statistical
  mean/variance/correlation, SNR tolerances, and zero-power rejection.
- Test exact frame lengths, guard/crop timing, deterministic seeds, SNR pairing,
  WGN row counts, manifest ordering, output collisions, partial failures, and
  allocation limits. Independently inspect exported CSV/binary samples and
  JSON metadata.
- Run the preset workflows for GUI Debug/Release, headless GCC/Clang, and
  separate sanitizers. Retain leak detection and run ASan outside the ptrace
  sandbox when necessary. Verify library-only builds and source-preserving
  rebuilds.
- Document runnable CLI/batch examples and numerical conventions. Linux is the
  validated platform; broader platform testing remains deferred.
