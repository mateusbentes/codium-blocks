from pathlib import Path
from PIL import Image

root = Path('/home/ubuntu/codium-blocks/assets/icons')
required = [
    root / 'source/codium-blocks-icon-master.png',
    root / 'windows/CodiumBlocks.ico',
    root / 'macos/CodiumBlocks.icns',
    root / 'linux/codium-blocks.png',
    root / 'linux/codium-blocks.desktop',
]
for path in required:
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f'missing or empty: {path}')

for path in sorted(root.glob('linux/hicolor/*x*/apps/*.png')) + [root / 'source/codium-blocks-icon-master.png']:
    image = Image.open(path)
    if image.mode != 'RGBA':
        raise SystemExit(f'not RGBA: {path} ({image.mode})')
    if image.info:
        raise SystemExit(f'metadata found in {path}: {sorted(image.info)}')
    if image.getchannel('A').getextrema()[0] != 0:
        raise SystemExit(f'no transparent background in {path}')
    print(path.relative_to(root), image.size, 'metadata=none')

for path in (root / 'windows/CodiumBlocks.ico', root / 'macos/CodiumBlocks.icns'):
    image = Image.open(path)
    print(path.relative_to(root), image.format, 'metadata_keys=', sorted(image.info.keys()))

print('icon-family=ok')
