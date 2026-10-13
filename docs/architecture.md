# Architecture and validation

[Back to the README](../README.md)

## Architecture and validation

The code is split into two static libraries and one executable, plus an optional shared C library for
Python:

- `app-core/` → `siggen-core-lib`, namespace `Core`: generation, analysis and file
  I/O, with no graphics dependency.
- `app-gui/` → `siggen-gui-lib`, namespace `GUI`: the SDL3 window and GPU device, the ImGui
  layer, the `Application` main loop and the generator windows. Built only with
  `SIGGEN_BUILD_GUI=ON`.
- `main.cpp` → `siggen`: opens the desktop application, which takes no arguments.
- `python/csrc/` → `siggen_c`: a shared C ABI over `siggen-core-lib`, loaded by the `siggen` Python
  package with ctypes. Built only with `SIGGEN_BUILD_PYTHON=ON`.

Core modules:

- `app-core/waveform.*`: waveform descriptor table (identifiers, families,
  capabilities) shared by GUI, validation, serialization and analysis.
- `app-core/generator.*`: configuration, validation, mapping and shaping.
- `app-core/noise.*`: documented Box–Muller Gaussian source and complex WGN.
- `app-core/batch.*`: fixed-length frame generation, seed derivation and the
  manifest-writing batch writer.
- `app-core/signal_processing.*`: stable RRC taps.
- `app-core/signal_analysis.*`: matched observations and Welch PSD with selectable window.
- `app-core/measurements.*`: power statistics (PAPR), EVM, measured SNR and eye traces.
- `app-core/pipeline.*`: the staged signal (bits, symbols, zero-inserted, filtered, noisy) behind the Pipeline tab.
- `app-core/demodulator.*`, `app-core/theory.*`, `app-core/ber.*`: the ideal reference receiver and its bit
  errors, the textbook BER curves, and the Eb/N0 sweep behind the BER tab and the Python `ber_curve`.
- `app-core/impairments.*`: carrier offset, phase noise, IQ imbalance, DC offset and quantization.
- `app-core/plot_export.*`: renderer that draws a `Figure` (panels, series, axes, legend) to SVG or PNG;
  `app-core/plot_figures.*` builds the figures from the plotted data.
- `app-core/preset.*`, `app-core/iq_export.*`: validated file I/O (CSV, SigMF).
- `app-core/recording.*`, `app-core/analysis.*`: recording import (SigMF; JSON via the vendored nlohmann/json)
  and the analysis report behind the Python `siggen.load` measurements.
- `app-core/generation_job.*`: owned worker and immutable completed snapshot; `app-core/ber_job.*` runs a BER
  sweep the same way, with cancellation and progress.
- `app-core/plot_data.*`: plot-ready, reduced copies of a generated signal.
- `app-gui/examples/`: guided lesson presets (`NN-name.preset`, with `#` note lines), compiled into the
  binary by `app-gui/CMakeLists.txt` as strings (`example_files.h`, generated); `examples.cpp` takes the
  menu title from the first note line and the tooltip description from the rest.

GUI and Python:

- `app-gui/application.*`: owns the SDL3 window, the SDL GPU device, the ImGui contexts and the
  frame loop (`Init`, `Run`, `Shutdown`); `main.cpp` only drives it.
- `app-gui/theme.*`: the color themes (ImGui and ImPlot styles), the bundled Inter font and the theme
  choice kept in imgui.ini; **View > Theme** switches themes while running.
- `app-gui/generator_session.*`: the document shared by the windows: settings,
  the owned generation job and BER sweep, and the analysis of the last result.
  Analysis runs once on result completion; generation and BER sweeps are
  asynchronous, while export and analysis are synchronous.
- `app-gui/windows/`: dockable windows deriving from `AppWindow` that keep only
  view state: Signal Setup (`controls_window.*`), Signal Summary
  (`summary_window.*`) and Signal Views (`views_window.*`, plots, BER and image export).
- `app-gui/window_manager.*`: owns the session and the windows, builds the default dock layout (also from
  View > Reset Layout), and owns the File and View menus: preset open/save, the compiled-in examples, the
  theme switch, the shortcuts, and the quit confirmation for unsaved settings.
- `python/`: C interface (`csrc/siggen_c.*`), the `siggen` Python package, its tests and the notebooks. The
  `siggen_*` C functions are an ABI loaded with ctypes, so they keep C naming.

The Python package tests (`python/tests`, run as `Python.Package`) exercise generation, measurement,
SigMF round trips, BER and batch datasets against the compiled `siggen_c` library.

Tests combine analytical limits and independent fixtures with mapping/Gray
adjacency for every constellation, deterministic bit vectors, shaping
superposition, matched timing, PSD power/sign, preset transactionality and
version migration, export precision, noise determinism, independent streams,
statistical mean/variance/correlation, SNR tolerances, zero-power rejection,
exact frame lengths, guard/crop timing, derived-seed pairing, manifest
ordering, and worker lifetime.
GUI tests exercise actual ImGui frames, window sizes, all views and failure states
without requiring a display server by default. Optional real SDL GPU captures:

```sh
SIGGEN_CAPTURE_DIR=build/ui-captures \
  out/debug/bin/siggen-gui-tests --gtest_filter=UI.GeneratedViewsAndPendingClosure
```

That command needs a working display and saves screenshots of each modulation's
waveform, mapped constellation, matched constellation and spectrum.

Run the sanitizer configurations:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_GUI=OFF \
  -DSIGGEN_BUILD_TESTS=ON -DSIGGEN_SANITIZER=address-undefined
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_GUI=OFF \
  -DSIGGEN_BUILD_TESTS=ON -DSIGGEN_SANITIZER=thread
cmake --build build-tsan && ctest --test-dir build-tsan --output-on-failure
```

`SIGGEN_SANITIZER` accepts exactly `none`, `address-undefined`, or `thread`.
Instrumentation is applied to project targets through compile and link options,
including static-library consumers; vendor compilation and global CMake flags are
untouched. Sanitizers require Linux with GCC or Clang and a successful compiler
and linker probe. Address/undefined and thread instrumentation cannot be combined.
Compiler/linker support does not guarantee
that the runtime can operate under a particular kernel or sandbox.

Leak detection remains enabled. LeakSanitizer cannot run under ptrace, and
sanitizer tests may need to run outside a ptrace-based sandbox. Do not disable
leak detection to make that environment pass. macOS and Windows are built and tested in CI
with the full suite; the UI smoke tests draw headless ImGui frames, so no display is needed.

This iteration excludes SDR streaming, RF carrier synthesis, analog modulation,
GFSK/GMSK, multipath and fading, AMCPy-specific dataset
conversion, GUI batch controls, batch resume and automatic cleanup remain
deferred. Large RRC spans take longer to generate;
there is no cancellation, and application closure waits for generation. The
waveform is an overview, the spectrum is a finite-buffer estimate, and exports
have no hardware timing or calibration guarantees.
