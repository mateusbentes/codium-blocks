# Codium::Blocks Icon Assets

The Codium::Blocks icon is an original modular-block mark created for this project. It uses a blue-only palette ranging from deep navy through cobalt and azure to a lighter electric blue. The mark is inspired by the general concept of interlocking software modules; it does not reproduce the Code::Blocks, Visual Studio Code, or VSCodium logos.

The icon assets are stored under `assets/icons/`:

| Platform | Location | Format |
|---|---|---|
| Windows | `assets/icons/windows/CodiumBlocks.ico` | Multi-resolution ICO: 16, 32, 48, 64, 128, and 256 pixels |
| macOS | `assets/icons/macos/CodiumBlocks.icns` | Multi-resolution ICNS: 16 through 1024 pixels |
| Linux | `assets/icons/linux/hicolor/<size>x<size>/apps/codium-blocks.png` | Hicolor PNG sizes from 16 through 512 pixels |
| Linux desktop entry | `assets/icons/linux/codium-blocks.desktop` | Desktop integration pointing to the hicolor icon name |
| Source master | `assets/icons/source/codium-blocks-icon-master.png` | 1024×1024 transparent PNG |

The generated PNGs have transparent backgrounds and are exported without embedded metadata. The ICO and ICNS files are generated from the cleaned master image and contain no text, watermark, or external-brand mark.

To regenerate the family after selecting a new source concept, update `scripts/prepare_icon_family.py` and run:

```bash
python3 scripts/prepare_icon_family.py
```

The script crops low-alpha generation fringe, adds a controlled transparent safe area, emits the source master, and creates the platform-specific variants.
