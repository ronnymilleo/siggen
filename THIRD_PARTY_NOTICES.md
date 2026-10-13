# Third-party notices

siggen is licensed under the GNU General Public License, version 3 or (at your option) any later version
(`GPL-3.0-or-later`, see `LICENSE`). Its binaries include or bundle the following software, each under its own
license.

## Compiled into the program

| Component | License | Copyright | Source |
|---|---|---|---|
| Dear ImGui (docking branch) | MIT | Omar Cornut | https://github.com/ocornut/imgui |
| ImPlot | MIT | Evan Pezent | https://github.com/epezent/implot |
| nlohmann/json | MIT | Niels Lohmann | https://github.com/nlohmann/json |
| SDL 3 | zlib license | Sam Lantinga | https://github.com/libsdl-org/SDL |
| stb (stb_image, stb_image_write) | MIT or CC0-1.0, at your option | Sean Barrett | https://github.com/nothings/stb |
| spdlog | MIT | Gabi Melman | https://github.com/gabime/spdlog |
| CLI11 | BSD 3-Clause | University of Cincinnati, Henry Schreiner | https://github.com/CLIUtils/CLI11 |
| Inter | SIL Open Font License 1.1 | The Inter Project Authors | https://github.com/rsms/inter |

Their license texts are in `vendor/imgui/LICENSE.txt`, `vendor/implot/LICENSE`, `vendor/json/LICENSE.MIT`,
`vendor/SDL/LICENSE.txt`, `vendor/stb/LICENSE`, `vendor/spdlog/LICENSE`, `vendor/cli11/LICENSE` and
`assets/fonts/Inter-LICENSE.txt`, and are installed with the program under `share/doc/siggen/licenses/` (Linux),
`licenses/` (the Windows zip) or `siggen.app/Contents/Resources/licenses/` (macOS).

## Bundled with the AppImage

The AppImage carries the GCC runtime libraries next to the program, unmodified, with their license texts in
`usr/share/doc/` inside the image:

| Component | License | Source |
|---|---|---|
| GCC runtime libraries (libgcc_s) | GPL 3 with the GCC Runtime Library Exception | https://gcc.gnu.org/ |

The C++ standard library is linked statically (`-static-libstdc++`), under the same exception.

Everything else, including SDL 3, is linked statically into the program. The GPU driver and the graphics API (the
Vulkan loader on Linux, Direct3D 12 on Windows, Metal on macOS) are not bundled; they come from the system.

GoogleTest, in `vendor/googletest`, only builds the tests and is not part of any binary.
