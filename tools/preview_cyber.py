"""Convert verify_cyber.c's real renderer output to viewable PNG/GIF.

Usage: python tools/preview_cyber.py [preview/cyber]
Run the host verifier with that existing output directory first.
"""
import struct
import sys
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WIDTH = int(re.search(r'#define ROAD_WIDTH\s+(\d+)', (ROOT / 'firmware/road.h').read_text()).group(1))
HEIGHT = 320
SKINS = int(re.search(r'#define PLAYER_COUNT\s+(\d+)', (ROOT / 'firmware/player.h').read_text()).group(1))

def load(path):
    values = struct.unpack(f'<{WIDTH * HEIGHT}H', path.read_bytes())
    image = Image.new('RGB', (WIDTH, HEIGHT))
    def expand3(v):
        return (v << 5) | (v << 2) | (v >> 1)
    image.putdata([(expand3((v >> 13) & 7), expand3((v >> 8) & 7),
                    ((v >> 3) & 3) * 85) for v in values])
    return image

def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else 'preview/cyber')
    sheet = Image.new('RGB', (WIDTH * 4, HEIGHT * ((SKINS + 3) // 4)))
    for skin in range(SKINS):
        sheet.paste(load(folder / f'menu-{skin}-0.rgb565'),
                    ((skin & 3) * WIDTH, (skin >> 2) * HEIGHT))
    sheet.save(folder / 'skins.png')
    for name, source in [('home', 'menu-4-0'), ('game', 'game-4-0')]:
        load(folder / f'{source}.rgb565').resize(
            (WIDTH * 2, HEIGHT * 2), Image.Resampling.NEAREST).save(folder / f'{name}.png')
    frames = [load(folder / f'motion-4-{i}.rgb565').resize(
        (WIDTH * 2, HEIGHT * 2), Image.Resampling.NEAREST) for i in range(32)]
    frames[0].save(folder / 'menu.gif', save_all=True,
                   append_images=frames[1:], duration=128, loop=0)
    print(f'Wrote {folder}/home.png, game.png, skins.png, menu.gif')

if __name__ == '__main__':
    main()
