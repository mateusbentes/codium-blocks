# Packaging and Distribution

Codium::Blocks uses a native CMake installation layout and CPack generators [1] for first-party package artifacts. Packaging is deliberately separate from ordinary build workflows. A package workflow may build and upload an artifact, but it does not publish a public GitHub release automatically.

## Installation layout

The installed native executable is placed in `bin` on Linux and Windows. The optional Node.js Extension Host, bundled demonstration extension, localization catalogs, and other runtime resources are installed below `share/codium-blocks` on those platforms. Documentation is installed below `share/doc/codium-blocks`. On macOS, the package workflow enables a native `Codium::Blocks.app` bundle; runtime resources are placed inside the application bundle under `Contents/Resources/codium-blocks`.

The executable searches for the Extension Host and resources relative to its installation prefix. This supports a staged install, a relocated archive, and a conventional system prefix in the usual CMake installation model [2]. Development builds retain the source-tree fallback required by the local build directory. User-installed VSIX extensions are not written into the installation prefix. They are stored under the platform data directory, or under `CODIUM_BLOCKS_DATA` when that variable is set.

A typical non-bundle installation therefore has this shape:

```text
<prefix>/bin/codium-blocks
<prefix>/share/codium-blocks/extension-host/
<prefix>/share/codium-blocks/extensions/
<prefix>/share/codium-blocks/locales/
<prefix>/share/doc/codium-blocks/
```

## Package formats and runtime boundary

| Platform | First package | CPack generator | Runtime boundary |
|---|---|---|---|
| Linux | Debian package and compressed tar archive | `DEB` and `TGZ` | The Debian package derives shared-library dependencies. The TGZ requires a compatible wxWidgets/GTK, OpenSSL, C++ runtime, and glibc environment. Node.js is recommended rather than required by the native core. |
| macOS | Disk image containing the application bundle | `DragNDrop` | The install step applies CMake BundleUtilities fixup and the package workflow checks external dependencies. The bundle remains unsigned and unnotarized until a later distribution decision. |
| Windows | Portable ZIP archive | `ZIP` | The package workflow includes configured wxWidgets runtime DLLs and the x64 Microsoft Visual C++ runtime DLLs. The artifact is still validated only on the `windows-2022` runner and is not a signed installer. |

The standard package is built with `CODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF`. The Code::Blocks adapter requires a matched SDK, resources, plugins, and ABI identity, so it remains a separately tested optional integration rather than a hidden dependency of the normal download.

## Local package build

On Linux, install the native build dependencies and run:

```bash
sudo apt-get install build-essential cmake pkg-config libwxgtk3.2-dev libssl-dev nodejs xvfb
cmake -S . -B build-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF
cmake --build build-package --parallel
ctest --test-dir build-package --output-on-failure
(cd build-package && cpack -G TGZ && cpack -G DEB)
```

To stage and inspect the installation without changing the host system:

```bash
cmake --install build-package --prefix "$PWD/staging"
test -x staging/bin/codium-blocks
test -f staging/share/codium-blocks/locales/en-US.tsv
test -f staging/share/codium-blocks/locales/pt-BR.tsv
```

To install the Debian artifact on a Debian-compatible system, review its dependencies first and then use the system package manager, for example:

```bash
dpkg-deb --info build-package/codium-blocks-*.deb
sudo apt install ./build-package/codium-blocks-*.deb
```

The TGZ is a relocatable archive, not a distribution-independent binary. Extract it into a chosen prefix and verify that the host provides the documented shared libraries:

```bash
mkdir -p "$HOME/opt/codium-blocks"
tar -xzf build-package/codium-blocks-*.tar.gz -C "$HOME/opt/codium-blocks" --strip-components=1
"$HOME/opt/codium-blocks/bin/codium-blocks" --help 2>/dev/null || true
```

On macOS, enable the application bundle:

```bash
brew install cmake wxwidgets node
export PATH="$(brew --prefix wxwidgets)/bin:$PATH"
cmake -S . -B build-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF \
  -DCODIUM_BLOCKS_MACOS_BUNDLE=ON
cmake --build build-package --config Release --parallel
ctest --test-dir build-package -C Release --output-on-failure
(cd build-package && cpack -C Release)
```

Mount the resulting DMG, copy `codium-blocks.app` to an application directory, and run it from there. The current artifact is unsigned and unnotarized; macOS security prompts and Gatekeeper behavior therefore remain a release concern rather than a solved claim.

On Windows, configure wxWidgets through vcpkg and pass the directory containing its runtime DLLs:

```powershell
cmake -S . -B build-package -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DBUILD_TESTING=ON `
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF `
  -DCODIUM_BLOCKS_WINDOWS_RUNTIME_DIR="$env:VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"
cmake --build build-package --config Release --parallel
ctest --test-dir build-package -C Release --output-on-failure
cpack --config build-package/CPackConfig.cmake -C Release
```

For a local ZIP, `CODIUM_BLOCKS_WINDOWS_RUNTIME_DIR` must contain both the vcpkg wxWidgets DLLs and the matching x64 Microsoft Visual C++ runtime DLLs. The package workflow creates this combined directory automatically; a local developer should copy the DLLs into a private staging directory rather than relying on DLLs installed only in the Visual Studio environment.

Extract the ZIP on a Windows x64 machine and start `bin\codium-blocks.exe`. The package workflow copies the matching wxWidgets and Microsoft Visual C++ runtime DLLs into the archive, but the CI runner is not a complete clean-consumer proof; SmartScreen, architecture, and future Windows servicing behavior remain release concerns.

Each package workflow creates a SHA-256 checksum with package basenames. A checksum confirms artifact integrity after transfer; it is not a code-signing mechanism, provenance attestation, notarization, or trust anchor.

## GitHub Actions policy

The package workflows are isolated by platform:

* `package-linux.yml` builds Linux Debian and tar artifacts on `ubuntu-24.04`.
* `package-macos.yml` builds the macOS application bundle and disk image on `macos-15`.
* `package-windows.yml` builds the Windows portable archive on `windows-2022`.

Each workflow runs on `workflow_dispatch` and on version tags matching `v*`. A tag build fails unless the tag matches the CMake project version, for example `v1.0.1` for `PROJECT_VERSION 1.0.1`. Manual runs are validation runs and are not release publication. Each workflow runs its own tests, generates its own artifacts, and uploads its own checksums. The workflows are maintained in the public project repository [3]. No package workflow publishes a release or assumes that another operating system has already completed.

The first release process should create a draft release from reviewed artifacts. Before public publication, a human should inspect package contents, run the application from a clean environment, verify documented dependencies, review high-contrast screenshots, and confirm the license and changelog. Automated packaging reduces repetitive work, but it cannot replace that final product check.

## Splash-screen policy

The native workbench currently does not use a mandatory splash screen. Its window is created directly, and a fixed splash delay would make the application feel slower without reporting real initialization progress. A splash screen should be reconsidered only if startup later performs measurable asynchronous work that takes long enough to require user feedback. If that happens, it should be dismissible, use the existing blue icon family, show actual initialization stages, and never impose an artificial wait.

## References

1. [CMake CPack documentation](https://cmake.org/cmake/help/latest/module/CPack.html)
2. [CMake installation and testing guide](https://cmake.org/cmake/help/latest/guide/tutorial/Installing%20and%20Testing%20CMake.html)
3. [Codium::Blocks public repository](https://github.com/mateusbentes/codium-blocks)

[1]: https://cmake.org/cmake/help/latest/module/CPack.html "CMake CPack documentation"
[2]: https://cmake.org/cmake/help/latest/guide/tutorial/Installing%20and%20Testing%20CMake.html "CMake installation and testing guide"
[3]: https://github.com/mateusbentes/codium-blocks "Codium::Blocks public repository"
