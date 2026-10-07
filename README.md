# Siggen

[![CI](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml/badge.svg)](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml)

A C++23 signal generator with a command-line interface and optional ImGui/ImPlot
GUI. Generate reproducible complex I/Q samples for BPSK, QPSK, OQPSK, pi/4-DQPSK,
DBPSK, DQPSK, 8-PSK, 8-DPSK, 4-ASK, OOK, 4-PAM, 16/32/64/256-QAM, 2-FSK, 4-FSK, MSK,
and complex white Gaussian noise (WGN), with optional AWGN and simple channel
impairments on linear signals; inspect waveforms, constellations, eye diagrams,
a two-sided spectrum and measurements (PAPR, EVM, measured SNR); work through
guided lesson presets; save presets; export single signals; and
generate swept fixed-length frame datasets (`siggen batch`) for classifier
training. This application generates complex baseband, not an RF carrier.

<div align="center">
  <img width="900" src="docs/images/gui-waveform.png" alt="Waveform tab showing the I and Q traces of a 16-QAM signal with AWGN" />
  <p align="center"><em>Waveform - 16-QAM, RRC pulse shaping, 22 dB SNR</em></p>

  <img width="900" src="docs/images/gui-constellation.png" alt="Constellation tab showing matched-filter observations of 16-QAM" />
  <p align="center"><em>Constellation - matched-filter observations</em></p>

  <img width="900" src="docs/images/gui-spectrum.png" alt="Spectrum tab showing the two-sided Welch PSD" />
  <p align="center"><em>Spectrum - two-sided Welch PSD</em></p>

  <img width="900" src="docs/images/gui-eye.png" alt="Eye tab showing the eye diagram of 16-QAM" />
  <p align="center"><em>Eye - overlaid symbol-period traces</em></p>

  <img width="900" src="docs/images/gui-pipeline.png" alt="Pipeline tab showing bits, mapped symbols, upsampled symbols, filter output and noisy signal on one time axis" />
  <p align="center"><em>Pipeline - bits to transmitted samples, step by step</em></p>
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
this needs only the `stb` submodule (image export) and has no test step. `SIGGEN_BUILD_APP` controls
the executable; `SIGGEN_BUILD_GUI` controls its optional graphical interface. For a local
GUI or Release Clang profile, inherit `dev` or `release` and set the compiler path.
CMake 3.28 supports these built-in [workflow presets](https://cmake.org/cmake/help/v3.28/manual/cmake-presets.7.html).

## Documentation

Everything else lives in [docs/](docs/):

- [Command line, logging and batch datasets](docs/command-line.md): `siggen` options, presets on the command line, log levels and `siggen batch`.
- [Using the GUI](docs/using-the-gui.md): controls, measurements, the eye diagram, spectral windows, the Pipeline tab, views and spectrum calibration, and exporting plot images as PNG or SVG.
- [Modulations, mapping and reproducibility](docs/modulations.md): every mapping (PSK, QAM, PAM, ASK, differential schemes, OQPSK, FSK/MSK), seeds and bit order.
- [Noise, SNR and channel impairments](docs/noise-and-impairments.md): AWGN, SNR vs Es/N0 vs Eb/N0, CFO, phase noise, IQ imbalance, DC offset and quantization.
- [Pulse gain, length and timing](docs/signal-timing.md): amplitude gain, filter length, delay and sample counts.
- [Presets and I/Q export](docs/presets-and-export.md): guided lessons, preset format, CSV/binary export and metadata.
- [Architecture and validation](docs/architecture.md): source layout, tests and sanitizer workflows.

## License

Siggen is distributed under the GNU General Public License, version 3. See
[`LICENSE`](LICENSE) for the complete license text.
