# Platform Support

Codium::Blocks is designed as a native C++/wxWidgets application for Windows, macOS, and Linux. The application core does not depend on Electron. Node.js is an optional runtime used only by the Extension Host.

## Linux

Install the native toolchain and wxWidgets development package:

```bash
sudo apt-get install build-essential cmake pkg-config libwxgtk3.2-dev nodejs
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The default Extension Host state directory is `$XDG_STATE_HOME/codium-blocks`, or `~/.local/state/codium-blocks` when `XDG_STATE_HOME` is not set.

## macOS

Install CMake, wxWidgets, and Node.js with Homebrew:

```bash
brew install cmake wxwidgets node
export PATH="$(brew --prefix wxwidgets)/bin:$PATH"
export PKG_CONFIG_PATH="$(brew --prefix wxwidgets)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For the application bundle and DMG workflow, add `-DCODIUM_BLOCKS_MACOS_BUNDLE=ON` and use `cpack -C Release` after the build, following the CMake installation and packaging model [1]. The default Extension Host state directory is `~/Library/Application Support/CodiumBlocks`.

## Windows

Install Visual Studio 2022 with the Desktop C++ workload, CMake, vcpkg, and Node.js. The supported CI/package baseline is **Visual Studio 17 2022, x64, and the vcpkg `x64-windows` triplet**. Configure the project from a **Developer PowerShell for VS 2022**; vcpkg's documented setup model is the source for this toolchain arrangement [2].

```powershell
vcpkg install wxwidgets:x64-windows
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF `
  -DCODIUM_BLOCKS_WINDOWS_RUNTIME_DIR="$env:VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The CMake toolchain file is what makes `find_package(wxWidgets CONFIG REQUIRED)` discover the vcpkg installation. The package workflow copies the wxWidgets DLLs from the configured runtime directory into the ZIP. The `x64-windows` triplet may also depend on the Microsoft Visual C++ runtime. A consumer machine without Visual Studio may need the matching Microsoft Visual C++ Redistributable; the current workflow runs on a developer runner and does not prove that this runtime is bundled.

The default Extension Host state directory is `%APPDATA%\CodiumBlocks`.

## Localization and portable configuration

The native UI catalogs are installed with the application under `share/codium-blocks/locales/` in a conventional Linux/Windows prefix and under `Contents/Resources/codium-blocks/locales/` in the macOS bundle. The writable language preference is stored below the platform data root; it is never written beside the executable. `CODIUM_BLOCKS_LANGUAGE=en-US` or `CODIUM_BLOCKS_LANGUAGE=pt-BR` can be set in PowerShell to select a deterministic language for testing. The environment override is not persisted.

All platforms support an explicit data directory for testing and portable deployments:

```text
CODIUM_BLOCKS_DATA=/path/to/state
```

On Windows PowerShell:

```powershell
$env:CODIUM_BLOCKS_DATA = "D:\CodiumBlocksData"
$env:CODIUM_BLOCKS_LANGUAGE = "pt-BR"
```

The platform abstraction must remain in the native core and Extension Host. New features must not assume POSIX paths, `/bin/sh`, Linux-only process signals, or a case-sensitive filesystem.

## Package artifacts

The repository also contains isolated package workflows. Linux produces Debian and tar archive artifacts, macOS produces a native application bundle inside a disk image, and Windows produces a portable ZIP archive. These workflows build the portable adapter-disabled configuration, run platform tests, and upload SHA-256 checksums. They do not publish a public release automatically. See [`PACKAGING.md`](PACKAGING.md) for installation layout, runtime dependencies, verification commands, and release policy.

## References

1. [CMake installation and testing guide](https://cmake.org/cmake/help/latest/guide/tutorial/Installing%20and%20Testing%20CMake.html)
2. [vcpkg getting started guide](https://vcpkg.io/en/getting-started.html)

[1]: https://cmake.org/cmake/help/latest/guide/tutorial/Installing%20and%20Testing%20CMake.html "CMake installation and testing guide"
[2]: https://vcpkg.io/en/getting-started.html "vcpkg getting started guide"
