"""Convert verify_cyber.c's real renderer output to viewable PNG/GIF.

Usage: python tools/preview_cyber.py [preview/cyber]
Run the host verifier with that existing output directory first.
"""
import struct
import sys
from pathlib import Path
from PIL import Image

def load(path):
    values = struct.unpack('<76800H', path.read_bytes())
    image = Image.new('RGB', (240, 320))
    image.putdata([(((v >> 11) & 31) * 255 // 31,
                    ((v >> 5) & 63) * 255 // 63,
                    (v & 31) * 255 // 31) for v in values])
    return image

def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else 'preview/cyber')
    sheet = Image.new('RGB', (960, 640))
    for skin in range(8):
        sheet.paste(load(folder / f'menu-{skin}-0.rgb565'),
                    ((skin & 3) * 240, (skin >> 2) * 320))
    sheet.save(folder / 'skins.png')
    for name, source in [('home', 'menu-4-0'), ('game', 'game-4-0')]:
        load(folder / f'{source}.rgb565').resize(
            (480, 640), Image.Resampling.NEAREST).save(folder / f'{name}.png')
    frames = [load(folder / f'motion-4-{i}.rgb565').resize(
        (480, 640), Image.Resampling.NEAREST) for i in range(32)]
    frames[0].save(folder / 'menu.gif', save_all=True,
                   append_images=frames[1:], duration=128, loop=0)
    print(f'Wrote {folder}/home.png, game.png, skins.png, menu.gif')

if __name__ == '__main__':
    main()
