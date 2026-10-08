# Architecture and validation

[Back to the README](../README.md)

## Architecture and validation

The code is split into three static libraries and the executable:

- `app-core/` → `siggen-core-lib`, namespace `Core`: generation, analysis and file
  I/O, with no graphics or command-line dependency.
- `app-cli/` → `siggen-cli-lib`, namespace `Console`: CLI11 parsing and
  configuration resolution.
- `app-gui/` → `siggen-gui-lib`, namespace `GUI`: the GLFW/OpenGL window, the ImGui
  layer and the generator window. Built only with `SIGGEN_BUILD_GUI=ON`.
- `main.cpp` → `siggen`: runs a single generation, a batch, or the GUI.

Core modules:

- `app-core/waveform.*`: waveform descriptor table (identifiers, families,
  capabilities) shared by CLI, GUI, validation, serialization and analysis.
- `app-core/generator.*`: configuration, validation, mapping and shaping.
- `app-core/noise.*`: documented Box–Muller Gaussian source and complex WGN.
- `app-core/batch.*`: fixed-length frame generation, seed derivation and the
  manifest-writing batch writer.
- `app-core/signal_processing.*`: stable RRC taps.
- `app-core/signal_analysis.*`: matched observations and Welch PSD with selectable window.
- `app-core/measurements.*`: power statistics (PAPR), EVM, measured SNR and eye traces.
- `app-core/pipeline.*`: the staged signal (bits, symbols, zero-inserted, filtered, noisy) behind the Pipeline tab.
- `app-core/demodulator.*`, `app-core/theory.*`, `app-core/ber.*`: the ideal reference receiver and its bit
  errors, the textbook BER curves, and the Eb/N0 sweep behind `siggen ber`, the BER tab and the Python `ber_curve`.
- `app-core/impairments.*`: carrier offset, phase noise, IQ imbalance, DC offset and quantization.
- `app-core/plot_export.*`: renderer that draws a `Figure` (panels, series, axes, legend) to SVG or PNG;
  `app-core/plot_figures.*` builds the figures from the plotted data.
- `app-core/preset.*`, `app-core/iq_export.*`: validated file I/O (CSV, SigMF).
- `app-core/recording.*`, `app-core/analysis.*`: recording import (SigMF; JSON via the vendored nlohmann/json)
  and the report behind `siggen analyze`.
- `app-core/generation_job.*`: owned worker and immutable completed snapshot; `app-core/ber_job.*` runs a BER
  sweep the same way, with cancellation and progress.
- `app-core/plot_data.*`: plot-ready, reduced copies of a generated signal.
- `presets/`: guided lesson presets (`NN-name.preset`, with `#` note lines).

Command line, GUI and Python:

- `app-cli/command_line.*`, `app-cli/cli_config.*`: CLI parsing and
  defaults → preset → explicit-option resolution.
- `app-gui/generator_session.*`: the document shared by the windows: settings,
  the owned generation job and BER sweep, and the analysis of the last result.
  Analysis runs once on result completion; generation and BER sweeps are
  asynchronous, while export and analysis are synchronous.
- `app-gui/windows/`: dockable windows deriving from `AppWindow` that keep only
  view state: Signal Setup (`controls_window.*`), Signal Summary
  (`summary_window.*`) and Signal Views (`views_window.*`, plots, BER and image export).
- `app-gui/window_manager.*`: owns the session and the windows, and builds the
  default dock layout (also from View > Reset Layout).
- `python/`: C interface (`csrc/siggen_c.*`), the `siggen` Python package, its tests and the notebooks. The
  `siggen_*` C functions are an ABI loaded with ctypes, so they keep C naming.

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
  build/dev/bin/siggen-gui-tests --gtest_filter=UI.GeneratedViewsAndPendingClosure
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
