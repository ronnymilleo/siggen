# User interaction

Talk to the user in Brazilian Portuguese (chat only), everything else should be done in English.

# Build and validation

Use the CMake presets for builds and validation. The standard command is
`cmake --workflow --preset <name>` (configure, build, test).

- `dev` and `release`: application and full test suite.
- `headless` and `headless-release`: CLI application, DSP and export tests without GUI dependencies.
- `clang-headless`: Linux Clang validation.
- `asan` and `tsan`: separate Linux GCC sanitizer workflows.

Individual steps use matching names: `cmake --preset <name>`,
`cmake --build --preset <name>`, and `ctest --preset <name>`.
Keep machine-specific compiler paths, parallelism, and fresh validation profiles
in ignored `CMakeUserPresets.json`; inherit the closest checked-in preset.
Do not switch compilers inside an existing build directory. See README.md for
local overrides and library-only builds.

Retain leak detection. If LeakSanitizer reports a ptrace limitation, rerun the
sanitizer workflow outside that sandbox with the required tool approval, or
report the exact environment limitation. Do not interpret it as a passing test.
Ordinary builds must not rewrite tracked sources; formatting is explicit only.

The application executable is `siggen`: no arguments generates the default signal
and writes `signal.csv` plus metadata in the current directory. Use isolated output
directories during validation; existing outputs require explicit `--overwrite`.
Use `siggen --gui` for the desktop interface and `--log-level` for logging verbosity.
