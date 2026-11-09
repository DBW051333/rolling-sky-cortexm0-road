"""Build a tour contact sheet and a 4x time-lapse from verify_cyber output.

Usage: python tools/preview_flyover.py tmp/flyover
The firmware renderer must have produced tour-4-0..199.rgb565 first.
"""
import sys
from pathlib import Path
from PIL import Image
from preview_cyber import load, WIDTH, HEIGHT

def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else 'tmp/flyover')
    frames = [load(folder / f'tour-4-{i}.rgb565') for i in range(200)]
    sheet = Image.new('RGB', (WIDTH * 4, HEIGHT))
    for stage in range(4):
        sheet.paste(frames[stage * 50 + 25], (stage * WIDTH, 0))
    sheet.save(folder / 'tour-scenes.png')
    frames[0].save(folder / 'tour.gif', save_all=True, append_images=frames[1:],
                   duration=100, loop=0)
    print(f'Wrote {folder}/tour-scenes.png and tour.gif (4x time-lapse)')

if __name__ == '__main__':
    main()
