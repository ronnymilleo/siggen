# Siggen

[![Release](https://img.shields.io/github/v/release/ronnymilleo/siggen)](https://github.com/ronnymilleo/siggen/releases)
[![Release Date](https://img.shields.io/github/release-date/ronnymilleo/siggen)](https://github.com/ronnymilleo/siggen/releases)
[![Coverage Status](https://coveralls.io/repos/github/ronnymilleo/siggen/badge.svg?branch=main)](https://coveralls.io/github/ronnymilleo/siggen?branch=main)
[![CodeFactor](https://www.codefactor.io/repository/github/ronnymilleo/siggen/badge)](https://www.codefactor.io/repository/github/ronnymilleo/siggen)
[![CI](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml/badge.svg)](https://github.com/ronnymilleo/siggen/actions/workflows/ci.yml)

A C++23 signal generator that produces reproducible complex baseband I/Q samples (not an RF
carrier). It ships as a Dear ImGui/ImPlot desktop application, `siggen`, and a Python package,
`siggen`, that scripts the same library.

- **Waveforms**: BPSK, QPSK, OQPSK, pi/4-DQPSK, DBPSK, DQPSK, 8-PSK, 8-DPSK, 4-ASK, OOK, 4-PAM,
  16/32/64/256-QAM, 2-FSK, 4-FSK, MSK and complex white Gaussian noise (WGN)
- **Channel**: optional AWGN and simple impairments on linear signals (CFO, phase noise, IQ imbalance,
  DC offset, quantization)
- **Views**: waveform, constellation, eye diagram, two-sided spectrum, BER and a step-by-step pipeline,
  with measurements such as PAPR, EVM and measured SNR
- **Learning**: guided lesson examples from the File menu
- **Export**: presets, CSV, binary and SigMF recordings, and plot images as PNG or SVG
- **Analysis and datasets**: measure recordings, generate swept fixed-length frame datasets for
  classifier training, and sweep BER, all scriptable from Python/NumPy

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
  Arch); make it executable (`chmod +x siggen-*.AppImage`) and run it. The Vulkan loader and the driver
  for your GPU come from the system
- **macOS**: a disk image for Apple silicon, with `siggen.app` to drag into Applications. It needs
  macOS 26 or newer, and, being ad-hoc signed and not notarized, must be opened once through
  **System Settings > Privacy & Security** (or `xattr -d com.apple.quarantine /Applications/siggen.app`)
- **Windows**: a zip to extract anywhere; `siggen.exe` needs Windows 10 or newer and no runtime beyond
  what Windows ships
- **Python**: a `siggen-*.whl` per platform (Linux `manylinux_2_39_x86_64`, macOS `macosx_26_0_arm64`,
  Windows `win_amd64`), installable with `pip install siggen-*.whl` on Python 3.10 or newer; it needs
  only NumPy at run time

Each release carries a `SHA256SUMS` file (check a download with `sha256sum -c --ignore-missing SHA256SUMS`
on Linux, `shasum -a 256 -c --ignore-missing SHA256SUMS` on macOS, or `certutil -hashfile <file> SHA256` on
Windows) and a build attestation verifiable with `gh attestation verify <file> --repo ronnymilleo/siggen`.

## Quick start

`siggen` opens the desktop application. The Python package generates, measures, sweeps BER and writes
datasets from the same library:

```sh
pip install siggen-*.whl        # from a release, on Python 3.10+
```

```python
import siggen

tx = siggen.generate(modulation="16-QAM", sps=8, snr_db=20)
tx.samples                      # complex64 NumPy array
tx.symbol_accuracy().evm_percent

# A swept fixed-length frame dataset for classifier training
siggen.batch("dataset", modulations=["BPSK", "QPSK", "16-QAM"], seeds=[42, 43],
             snrs_db=[-10, 0, 10], frame_size=2048, frames_per_point=100)
```

See [Analysis, SigMF and Python](docs/analysis-and-python.md) for the full Python API and batch
datasets. To build the library from source instead of installing a wheel, see
[Build and test](#build-and-test).

## Build and test

On every system the build needs:

- CMake 3.28 or newer
- A C++23 compiler and standard library, including `std::ios::noreplace` (the configure step
  probes for it); tested with GCC 16, Apple Clang 21 and Visual Studio 2026
- Ninja (or another CMake generator)
- Git, for the submodules in `vendor/`: SDL3, Dear ImGui (`docking` branch), ImPlot, nlohmann/json,
  spdlog, stb and GoogleTest. SDL is compiled and linked statically with the project, so
  every platform carries the same version

Clone with the submodules, or, on an existing clone, run `git submodule update --init --recursive`:

```sh
git clone --recurse-submodules https://github.com/ronnymilleo/siggen.git
```

The binaries land in `out/<build-type>/bin/` (for example `./out/debug/bin/siggen`).

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

### Windows

Build with Visual Studio 2026 (the "Desktop development with C++" workload, which brings CMake and
Ninja), from a Developer PowerShell. The GUI renders with Direct3D 12, part of Windows, and SDL3
comes from its submodule, so nothing else is needed:

```powershell
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
| `SIGGEN_BUILD_TESTS` | `OFF` | Build the GoogleTest executables |
| `SIGGEN_BUILD_PYTHON` | `OFF` | Build the `siggen_c` shared library used by the Python package |
| `SIGGEN_ENABLE_WARNINGS` | `ON` | Enable project compiler warnings |
| `SIGGEN_ENABLE_COVERAGE` | `OFF` | Enable compiler coverage instrumentation |
| `SIGGEN_SANITIZER` | `none` | `none`, `address-undefined`, or `thread` (Linux with GCC or Clang) |

Common configurations:

```sh
# Release build of the GUI
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release

# Headless: tests and the Python library only, no graphics dependencies at all (also the sanitizer configuration)
cmake -S . -B build-headless -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_GUI=OFF \
  -DSIGGEN_BUILD_TESTS=ON -DSIGGEN_BUILD_PYTHON=ON

# Address + undefined-behavior sanitizers on the headless tests
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSIGGEN_BUILD_GUI=OFF \
  -DSIGGEN_BUILD_TESTS=ON -DSIGGEN_SANITIZER=address-undefined

# Python library only (no executables, no tests): needs just the stb and json submodules
cmake -S . -B build-lib -G Ninja -DSIGGEN_BUILD_GUI=OFF -DSIGGEN_BUILD_PYTHON=ON
```

### Build notes

- Executables go to `out/<build-type>/bin/` and libraries to `out/<build-type>/lib/` of the source
  tree, while the CMake cache and objects stay in the build directory (`build*` in `.gitignore`). A
  build directory with another name than `build`, such as `build-asan`, writes to
  `out/<build-type>-asan/`, so trees with the same build type do not overwrite each other.
  In-source builds are rejected.
- `compile_commands.json` of the last build is copied to the repository root for editors and `clang-tidy`.
- If clang-format is available, building the executable first formats every project source
  (the `format` target), so commit the formatted files. Identifier naming is checked with
  `run-clang-tidy -p build '/(app-core|app-gui|tests)/|/main\.cpp$'`.
- GoogleTest is included only when `SIGGEN_BUILD_TESTS=ON`, and Threads is discovered only for the GUI
  or the tests. Unused SDL subsystems, its tests and examples, GoogleMock and the ImGui/ImPlot
  demo sources are left out of the build.
- The Python wheel is built separately with `pip wheel .` or `python -m build` (scikit-build-core,
  see `pyproject.toml`); it compiles only `siggen_c` with `SIGGEN_BUILD_GUI=OFF`.

### Packaging

`packaging/appimage/build.sh` builds the Linux AppImage inside an Ubuntu 24.04 container, and
`packaging/macos/build.sh` builds a release bundle and a disk image of it. On Windows,
`cmake --install build --prefix <folder>` lays out the folder the release zips. The Python wheel of each
platform is built with scikit-build-core (see `pyproject.toml`). Pushing a `v*` tag runs the same steps in
the release workflow and publishes the packs and the wheels on GitHub Releases.

## Documentation

Everything else lives in [docs/](docs/):

- [Using the GUI](docs/using-the-gui.md): controls, measurements, the eye diagram, spectral windows, the Pipeline tab, views and spectrum calibration, and exporting plot images as PNG or SVG.
- [Modulations, mapping and reproducibility](docs/modulations.md): every mapping (PSK, QAM, PAM, ASK, differential schemes, OQPSK, FSK/MSK), seeds and bit order.
- [Noise, SNR and channel impairments](docs/noise-and-impairments.md): AWGN, SNR vs Es/N0 vs Eb/N0, CFO, phase noise, IQ imbalance, DC offset and quantization.
- [Pulse gain, length and timing](docs/signal-timing.md): amplitude gain, filter length, delay and sample counts.
- [Presets and I/Q export](docs/presets-and-export.md): guided lessons, preset format, CSV, binary and SigMF export and metadata.
- [Bit error rate and the reference receiver](docs/ber-and-receiver.md): the ideal receiver, BER against Eb/N0 with theory, the BER tab and the Python API.
- [Analysis, SigMF and Python](docs/analysis-and-python.md): reading recordings, batch datasets, the NumPy-based Python package and the notebooks.
- [Architecture and validation](docs/architecture.md): source layout, tests and sanitizer workflows.

## License

Siggen is distributed under the GNU General Public License, version 3. See
[`LICENSE`](LICENSE) for the complete license text.
