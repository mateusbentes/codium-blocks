from pathlib import Path
from PIL import Image

PROJECT = Path('/home/ubuntu/codium-blocks')
CONCEPT = PROJECT / 'assets/icons/source/codium-blocks-icon-input.png'
SOURCE = PROJECT / 'assets/icons/source/codium-blocks-icon-master.png'
WINDOWS = PROJECT / 'assets/icons/windows/CodiumBlocks.ico'
MACOS = PROJECT / 'assets/icons/macos/CodiumBlocks.icns'
LINUX = PROJECT / 'assets/icons/linux'

# Remove nearly transparent generation fringe/stray pixels, crop to the visible emblem,
# then add controlled transparent padding for predictable app-icon safe areas.
image = Image.open(CONCEPT).convert('RGBA')
alpha = image.getchannel('A').point(lambda value: 255 if value >= 16 else 0)
bbox = alpha.getbbox()
if bbox is None:
    raise RuntimeError('The selected concept has no visible alpha content')
image = image.crop(bbox)
side = max(image.size)
padded_side = int(round(side * 1.16))
canvas = Image.new('RGBA', (padded_side, padded_side), (0, 0, 0, 0))
canvas.alpha_composite(image, ((padded_side - image.width) // 2, (padded_side - image.height) // 2))
master = canvas.resize((1024, 1024), Image.Resampling.LANCZOS)

for path in (SOURCE,):
    path.parent.mkdir(parents=True, exist_ok=True)
    master.save(path, format='PNG', optimize=True)

sizes = (16, 24, 32, 48, 64, 128, 256, 512)
for size in sizes:
    output = master.resize((size, size), Image.Resampling.LANCZOS)
    destination = LINUX / 'hicolor' / f'{size}x{size}' / 'apps' / 'codium-blocks.png'
    destination.parent.mkdir(parents=True, exist_ok=True)
    output.save(destination, format='PNG', optimize=True)

# Keep a convenient Linux preview beside the hicolor tree.
master.resize((512, 512), Image.Resampling.LANCZOS).save(LINUX / 'codium-blocks.png', format='PNG', optimize=True)

WINDOWS.parent.mkdir(parents=True, exist_ok=True)
master.save(WINDOWS, format='ICO', sizes=[(size, size) for size in (16, 32, 48, 64, 128, 256)])

MACOS.parent.mkdir(parents=True, exist_ok=True)
master.save(MACOS, format='ICNS', sizes=[(size, size) for size in (16, 32, 64, 128, 256, 512, 1024)])

print('prepared:', SOURCE)
print('prepared:', WINDOWS)
print('prepared:', MACOS)
print('prepared Linux hicolor sizes:', ', '.join(str(size) for size in sizes))
