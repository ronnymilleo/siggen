# siggen

Complex baseband signal generator in C++23: a command-line tool and an optional Dear ImGui/ImPlot GUI
(GLFW + OpenGL 3.3) that generate, analyse and export I/Q signals.

- `app-core/` → static lib `siggen-core-lib`, namespace `Core` (generation, analysis, presets, I/Q and image
  export, plot data); no graphics or CLI dependency
  - Results must stay deterministic: same configuration and seeds give bit-identical samples, so PRNG draw order
    and floating-point evaluation order are part of the contract (tests compare outputs exactly)
  - `waveform.h` holds the descriptor table; `StableId` values feed seed derivation and must never be renumbered
- `app-cli/` → static lib `siggen-cli-lib`, namespace `Console` (CLI11 parsing; precedence is defaults → preset →
  explicitly supplied options)
- `app-gui/` → static lib `siggen-gui-lib`, namespace `GUI` (window, ImGui layer, dockable windows); built only with
  `SIGGEN_BUILD_GUI=ON`
  - `generator_session.h` is the document (settings, background generation, analysis of the last result); the
    `WindowManager` owns it and hands it to every window by reference
  - `windows/` holds the dockable windows: each derives from `AppWindow`, which wraps `Draw()` in Begin/End;
    windows keep only view state (selected view, zoom requests, dialogs) and read or change the session
- `main.cpp` → executable `siggen`
- `tests/` → GoogleTest executables `siggen-tests` (core), `siggen-cli-tests` (+ `CLI.EndToEnd`, which runs the real
  executable) and `siggen-gui-tests` (needs a display)
- `vendor/` is never edited: `imgui`, `implot`, `glfw`, `stb`, `spdlog`, `cli11` and `googletest` are submodules;
  `vendor/CMakeLists.txt` builds them
- `cmake/ProjectOptions.cmake` sets C++23, warnings and sanitizers per target through `siggen_target_options()`

## Commands

Presets share `build/<preset-name>` (`dev`, `release`, `headless`, `headless-release`, `clang-headless`, `asan`,
`tsan`):

```
cmake --workflow --preset dev        # configure, build (also runs clang-format on all sources) and test
./build/dev/bin/siggen --gui
```

Naming check (`CMAKE_CXX_SCAN_FOR_MODULES` is off, so the compile database has no GCC module flags):

```
clang-tidy -p build/dev $(git ls-files '*.cpp')
```

Verification is a clean build, all tests passing and clang-tidy without warnings. GitHub Actions
(`.github/workflows/ci.yml`) runs a lint job in an `archlinux` container (formatting check with
`git diff --exit-code` after the build, then clang-tidy), the headless workflows with GCC and Clang on Ubuntu, and
macOS (headless tests plus a GUI compile). Pure logic goes in `Core` with GoogleTest coverage; code that needs a
live ImGui context only gets smoke tests, so move testable logic out of the windows into `Core` free functions.

## Naming

Enforced by `.clang-tidy` (`readability-identifier-naming`).

| Element | Style | Example |
|---|---|---|
| Classes, structs, enums, enum values | PascalCase | `GenerationConfig`, `Modulation::QAM16` |
| Functions and methods | PascalCase | `Generate`, `MapSymbols` |
| Public struct fields | PascalCase | `config.SamplesPerSymbol` |
| Private/protected members | `m_` + PascalCase | `m_PipelineFirst` |
| Locals and parameters | snake_case, descriptive | `sample_rate_hz`, `bits_per_symbol` |
| `constexpr` and global constants | PascalCase | `Core::MaxSignalSamples` |
| Macros | UPPER_SNAKE | `SIGGEN_VERSION` |
| Namespaces | `Core`, `Console`, `GUI` | |
| Files | snake_case | `views_window.cpp` |
| Include guards | `SIGGEN_<FILE_NAME>_H` | `SIGGEN_GENERATOR_H` |

Acronyms stay uppercase (`GUI`, `RRC`, `SPS`) except proper names (`ImGui`). Names describe meaning, not type.
When a field shares its name with its type, qualify the type (`Core::Modulation Modulation`).

## Documentation (Doxygen)

- Every file starts with a short block, nothing else:
  ```
  /**
   * @file    generator.h
   * @brief   One sentence on what the file is for.
   */
  ```
  Add `@details` only when it adds information.
- Classes, structs and enums are documented in the header, right above the declaration
  (`@class`/`@struct`/`@enum` + `@brief`, optional `@details`).
- Functions and methods are documented in the `.cpp`, above the definition: `@brief`, then
  `@param[in|out|in,out]`, `@return` and `@note` when relevant. Headers keep declarations bare and grouped by
  theme; classes with many members label each group with a short `// Theme` line.
- Document every public function and method. Private methods and file-local helpers only when the intent is not
  obvious. Never leave empty tags. Inline `//` comments only for intent or constraints; no trailing period in
  single-line `//` comments.

## File layout

- No section banners and no "End of file" markers.
- Header: file block → include guard → includes → `namespace X {` → declarations → `} // namespace X` → `#endif // GUARD`.
- Source: file block → own header, blank line, other includes → `namespace X {` → file-local helpers in an
  anonymous `namespace { ... } // namespace` → definitions in the same order as the header → `} // namespace X`.
- Never `using namespace` in `.cpp` files; tests wrap their body in the namespace they test.
- Long `.cpp` files may group code with `// region Name` / `// endregion`.
- No function bodies in headers (except `= default`); constexpr data tables may stay.

## Code conventions

- Formatting comes from `.clang-format` (LLVM, 4 spaces, 120 columns, `InsertBraces`).
- C++ casts only.
- Errors are exceptions (`std::invalid_argument` for bad settings, `std::length_error` for size limits,
  `std::runtime_error` for I/O and graphics); messages are user-facing and tests check them. `main()` catches and
  logs them with spdlog and returns 1.
- List only `.cpp` files in each module's `CMakeLists.txt`; headers are found through `target_include_directories`.
