"""Reject overlap between linked code and the writable SD artwork cache."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def check(map_path, header_path):
    header = Path(header_path).read_text(encoding='utf-8')
    def value(name):
        return int(re.search(r'#define ' + name + r'\s+(0x[0-9a-fA-F]+|\d+)', header).group(1), 0)
    start, capacity = value('ART_CACHE_ADDRESS'), value('ART_CACHE_BYTES')
    text = Path(map_path).read_text(encoding='utf-8')
    match = re.search(r'Execution Region ER_IROM1 \(Base: (0x[\da-fA-F]+), Size: (0x[\da-fA-F]+), Max: (0x[\da-fA-F]+)', text)
    if not match:
        raise ValueError('Missing linked code region')
    base, size, limit = (int(v, 16) for v in match.groups())
    if base + size > start or base + limit != start or start + capacity != 32768:
        raise ValueError(f'Code/cache overlap or mismatch: code end={base+size:#x}, limit={base+limit:#x}, cache={start:#x}+{capacity}')
    print(f'PASS memory: code {base:#06x}..{base+size:#06x}, limit {start:#06x}; SD cache {start:#06x}..0x8000 ({capacity} B)')

if __name__ == '__main__':
    try:
        check(sys.argv[1] if len(sys.argv)>1 else ROOT/'firmware/Output/code.map',
              sys.argv[2] if len(sys.argv)>2 else ROOT/'firmware/appearance.h')
    except (ValueError, AttributeError) as exc:
        raise SystemExit(str(exc))
