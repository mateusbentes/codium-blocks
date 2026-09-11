# Platform Support

Codium::Blocks is designed as a native C++/wxWidgets application for Windows, macOS, and Linux. The application core does not depend on Electron. Node.js is an optional runtime used only by the Extension Host.

## Linux

Install the native toolchain and wxWidgets development package:

```bash
sudo apt-get install build-essential cmake pkg-config libwxgtk3.2-dev nodejs
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The default Extension Host state directory is `$XDG_STATE_HOME/codium-blocks`, or `~/.local/state/codium-blocks` when `XDG_STATE_HOME` is not set.

## macOS

Install CMake, wxWidgets, and Node.js with Homebrew:

```bash
brew install cmake wxwidgets node
export PATH="$(brew --prefix wxwidgets)/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The default Extension Host state directory is `~/Library/Application Support/CodiumBlocks`.

## Windows

Install Visual Studio 2022 with the Desktop C++ workload, CMake, wxWidgets, and Node.js. Configure the project from a **Developer PowerShell for VS 2022**:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The default Extension Host state directory is `%APPDATA%\CodiumBlocks`.

The native UI catalogs are installed with the application under `locales/`. The writable language preference is stored below `%APPDATA%\CodiumBlocks`; it is never written beside the executable. `CODIUM_BLOCKS_LANGUAGE=en-US` or `CODIUM_BLOCKS_LANGUAGE=pt-BR` can be set in PowerShell to select a deterministic language for testing.

## Portable configuration

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

The repository also contains isolated package workflows. Linux produces Debian and tar archive artifacts, macOS produces a native application bundle inside a disk image, and Windows produces a portable ZIP archive. These workflows build the portable adapter-disabled configuration, run platform tests, and upload SHA-256 checksums. They do not publish a public release automatically. See [`PACKAGING.md`](PACKAGING.md) for the installation layout and release policy.
