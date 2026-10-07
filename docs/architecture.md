# Architecture and validation

[Back to the README](../README.md)

## Architecture and validation

- `library/waveform.*`: waveform descriptor table (identifiers, families,
  capabilities) shared by CLI, GUI, validation, serialization and analysis.
- `library/generator.*`: configuration, validation, mapping and shaping.
- `library/noise.*`: documented Box–Muller Gaussian source and complex WGN.
- `library/batch.*`: fixed-length frame generation, seed derivation and the
  manifest-writing batch writer.
- `library/signal_processing.*`: stable RRC taps and convolution.
- `library/signal_analysis.*`: matched observations and Welch PSD with selectable window.
- `library/measurements.*`: power statistics (PAPR), EVM, measured SNR and eye traces.
- `library/pipeline.*`: the staged signal (bits, symbols, zero-inserted, filtered, noisy) behind the Pipeline tab.
- `library/impairments.*`: carrier offset, phase noise, IQ imbalance, DC offset and quantization.
- `library/plot_export.*`: renderer that draws a `Figure` (panels, series, axes, legend) to SVG or PNG; `application/plot_figures.h` builds the figures from the plotted data.
- `presets/`: guided lesson presets (`NN-name.preset`, with `#` note lines).
- `library/preset.*`, `library/iq_export.*`: validated file I/O (CSV, SigMF).
- `library/recording.*`, `library/analysis.*`: recording import (SigMF, legacy cf32; JSON via the vendored nlohmann/json) and the report behind `siggen analyze`.
- `python/`: C interface (`csrc/siggen_c.*`), the `siggen` Python package, its tests and the notebooks.
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
leak detection to make that environment pass. macOS is built and tested in CI (headless tests plus a GUI compile); the UI smoke tests are not run there.

This iteration excludes SDR streaming, RF carrier synthesis, analog modulation,
GFSK/GMSK, multipath and fading, AMCPy-specific dataset
conversion, GUI batch controls, batch resume and automatic cleanup remain
deferred. Large RRC spans take longer to generate;
there is no cancellation, and application closure waits for generation. The
waveform is an overview, the spectrum is a finite-buffer estimate, and exports
have no hardware timing or calibration guarantees.
