# Siggen

[![Release](https://img.shields.io/github/v/release/ronnymilleo/siggen)](https://github.com/ronnymilleo/siggen/releases)
[![Release Date](https://img.shields.io/github/release-date/ronnymilleo/siggen)](https://github.com/ronnymilleo/siggen/releases)
[![Coverage Status](https://coveralls.io/repos/github/ronnymilleo/siggen/badge.svg?branch=main)](https://coveralls.io/github/ronnymilleo/siggen?branch=main)
[![CodeFactor](https://www.codefactor.io/repository/github/ronnymilleo/siggen/badge)](https://www.codefactor.io/repository/github/ronnymilleo/siggen)
[![CI](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml/badge.svg)](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml)

A C++23 signal generator with a Dear ImGui/ImPlot desktop application (the
default executable, `siggen`) and a separate command-line tool (`siggen-cli`).
Generate reproducible complex I/Q samples for BPSK, QPSK, OQPSK, pi/4-DQPSK,
DBPSK, DQPSK, 8-PSK, 8-DPSK, 4-ASK, OOK, 4-PAM, 16/32/64/256-QAM, 2-FSK, 4-FSK, MSK,
and complex white Gaussian noise (WGN), with optional AWGN and simple channel
impairments on linear signals; inspect waveforms, constellations, eye diagrams,
a two-sided spectrum and measurements (PAPR, EVM, measured SNR); work through
guided lesson examples from the File menu; save presets; export single signals; and
export SigMF recordings, measure recordings with `siggen-cli analyze`, script it all from Python/NumPy, and generate swept fixed-length frame datasets (`siggen-cli batch`) for classifier
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

## Install

Prebuilt releases are published on [GitHub Releases](https://github.com/ronnymilleo/siggen/releases):

- **Linux**: an AppImage that runs on distributions with glibc 2.39 or newer (Ubuntu 24.04, Fedora 40,
  Arch); make it executable and run it. The Vulkan loader and the driver for your GPU come from the system
- **macOS**: a disk image for Apple silicon, with `siggen.app` to drag into Applications. It needs
  macOS 26 or newer, and, being ad-hoc signed and not notarized, must be opened once through
  **System Settings > Privacy & Security** (or `xattr -d com.apple.quarantine /Applications/siggen.app`)
- **Windows**: a zip to extract anywhere; `siggen.exe` and `siggen-cli.exe` need Windows 10 or newer and
  no runtime beyond what Windows ships

Each release carries a `SHA256SUMS` file and a build attestation verifiable with
`gh attestation verify <file> --repo ronnymilleo/siggen`.

## Build and test

On every system the build needs:

- CMake 3.28 or newer
- A C++23 compiler and standard library, including `std::ios::noreplace` (the configure step
  probes for it); tested with GCC 16, Apple Clang 21 and Visual Studio 2026
- Ninja (or another CMake generator)
- Git, for the submodules in `vendor/`: SDL3, Dear ImGui (`docking` branch), ImPlot, nlohmann/json,
  spdlog, CLI11, stb and GoogleTest. SDL is compiled and linked statically with the project, so
  every platform carries the same version

Clone with the submodules (`git clone --recurse-submodules <repository-url>`), or, on an existing
clone, run `git submodule update --init --recursive`. The binaries land in `out/<build-type>/bin/`:
the default executable `siggen` opens the desktop application, and `siggen-cli` is the command-line
tool (for example `./out/debug/bin/siggen-cli --help`).

### Linux

The GUI renders with Vulkan. Besides the common tools, it needs:

- the build dependencies of SDL3 (X11, Wayland, libxkbcommon)
- the Vulkan loader at run time (`vulkan-icd-loader` on Arch, `libvulkan1` on Ubuntu), which SDL
  opens when the program starts, and the Vulkan driver for your GPU

On Arch Linux:

```sh
sudo pacman -S cmake ninja gcc pkgconf vulkan-icd-loader \
    libxcursor libxi libxfixes libxinerama libxrandr libxss libxtst libxkbcommon wayland wayland-protocols libdecor mesa
```

Also install the Vulkan driver for your GPU (for example `vulkan-radeon`). On Ubuntu 25.04 or newer:

```sh
sudo apt install cmake ninja-build g++ pkgconf libvulkan1 mesa-vulkan-drivers \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev
```

These Ubuntu instructions are not tested regularly; please report what differs. Build and run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./out/debug/bin/siggen
```

### macOS

The GUI renders with Metal, part of macOS, and builds as an application bundle (`siggen.app`). The C++
library provides `std::from_chars` for doubles only from macOS 26, which the project pins as its deployment
target, so the build needs the Xcode 26 Command Line Tools (`xcode-select --install`) or newer; then, with
[Homebrew](https://brew.sh):

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./out/debug/bin/siggen.app/Contents/MacOS/siggen
```

`packaging/macos/build.sh` builds a release bundle and a disk image of it, and `packaging/appimage/build.sh`
builds the Linux AppImage inside an Ubuntu 24.04 container.

### Windows

Build with Visual Studio 2026 (the "Desktop development with C++" workload, which brings CMake and
Ninja), from a Developer PowerShell. The GUI renders with Direct3D 12, part of Windows, and SDL3
comes from its submodule, so nothing else is needed:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
out\debug\bin\siggen.exe
```

### Build options

The build is shaped by cache options:

| Option | Default | Effect |
|---|---|---|
| `SIGGEN_BUILD_GUI` | `ON` | Build the `siggen` desktop application and its library |
| `SIGGEN_BUILD_CLI` | `ON` | Build the `siggen-cli` command-line tool and its library |
| `SIGGEN_BUILD_TESTS` | `OFF` | Build the GoogleTest executables |
| `SIGGEN_BUILD_PYTHON` | `OFF` | Build the `siggen_c` shared library used by the Python package |
| `SIGGEN_ENABLE_WARNINGS` | `ON` | Enable project compiler warnings |
| `SIGGEN_ENABLE_COVERAGE` | `OFF` | Enable compiler coverage instrumentation |
| `SIGGEN_SANITIZER` | `none` | `none`, `address-undefined`, or `thread` (Linux with GCC or Clang) |

Common configurations:

```sh
# Release build of the GUI and the CLI
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release

# Headless: CLI and tests only, no graphics dependencies at all (also the sanitizer configuration)
cmake -S . -B build-headless -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_GUI=OFF -DSIGGEN_BUILD_TESTS=ON

# Address + undefined-behavior sanitizers on the headless tests
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_GUI=OFF \
  -DSIGGEN_BUILD_TESTS=ON -DSIGGEN_SANITIZER=address-undefined

# Library only (no executables): needs just the stb and json submodules
cmake -S . -B build-lib -G Ninja -DSIGGEN_BUILD_GUI=OFF -DSIGGEN_BUILD_CLI=OFF
```

Tests print failures on run. GoogleTest is included
only when `SIGGEN_BUILD_TESTS=ON`; Threads is discovered only for the CLI, GUI, or tests.
Unused SDL subsystems, tests and examples default off. GoogleMock and ImGui/ImPlot demo sources are excluded from default builds.

Executables are in `out/<build-type>/bin/` and libraries in `out/<build-type>/lib/`
of the source tree, while the CMake cache and objects stay in the build directory
(`build*` in `.gitignore`). `compile_commands.json` of the last build is copied to the
repository root for editors and `clang-tidy`. In-source builds are rejected. If clang-format is
available, building either executable first formats every project source
(the `format` target), so commit the formatted files. Identifier naming is checked
with `run-clang-tidy -p build '/(app-core|app-cli|app-gui|tests)/|/main\.cpp$'`.

## Documentation

Everything else lives in [docs/](docs/):

- [Command line, logging and batch datasets](docs/command-line.md): `siggen-cli` options, presets on the command line, log levels and `siggen-cli batch`.
- [Using the GUI](docs/using-the-gui.md): controls, measurements, the eye diagram, spectral windows, the Pipeline tab, views and spectrum calibration, and exporting plot images as PNG or SVG.
- [Modulations, mapping and reproducibility](docs/modulations.md): every mapping (PSK, QAM, PAM, ASK, differential schemes, OQPSK, FSK/MSK), seeds and bit order.
- [Noise, SNR and channel impairments](docs/noise-and-impairments.md): AWGN, SNR vs Es/N0 vs Eb/N0, CFO, phase noise, IQ imbalance, DC offset and quantization.
- [Pulse gain, length and timing](docs/signal-timing.md): amplitude gain, filter length, delay and sample counts.
- [Presets and I/Q export](docs/presets-and-export.md): guided lessons, preset format, CSV, binary and SigMF export and metadata.
- [Bit error rate and the reference receiver](docs/ber-and-receiver.md): the ideal receiver, BER against Eb/N0 with theory, `siggen-cli ber`, the BER tab and the Python API.
- [Analysis, SigMF and Python](docs/analysis-and-python.md): `siggen-cli analyze`, reading recordings, the NumPy-based Python package and the notebooks.
- [Architecture and validation](docs/architecture.md): source layout, tests and sanitizer workflows.

## License

Siggen is distributed under the GNU General Public License, version 3. See
[`LICENSE`](LICENSE) for the complete license text.
