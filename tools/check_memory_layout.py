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
    if base!=0 or size>limit or base+limit>32768:
        raise ValueError('Code exceeds physical 32 KiB BRAM')
    ram=re.search(r'Execution Region RW_IRAM1 \(Base: (0x[\da-fA-F]+), Size: (0x[\da-fA-F]+), Max: (0x[\da-fA-F]+)',text)
    if not ram:
        raise ValueError('Missing data RAM region')
    ram_base,ram_size,ram_limit=(int(v,16) for v in ram.groups())
    if start<32768:
        valid=base+limit==start and start+capacity==32768
    else:
        valid=ram_base==0x20000000 and ram_base+ram_limit==start and start+capacity==0x20004000
    if start>=0x20000000:
        startup=(ROOT/'firmware/startup.s').read_text(encoding='utf-8')
        stack=re.search(r'__Vectors\s+DCD\s+(0x[0-9a-fA-F]+)',startup)
        valid=valid and stack is not None and int(stack.group(1),16)==start
    if not valid or ram_size>ram_limit:
        raise ValueError('SD cache overlaps code/data/stack or exceeds physical memory')
    print(f'PASS memory: code end {base+size:#x}/{base+limit:#x}; data {ram_size}/{ram_limit}; SD cache {start:#x}+{capacity}')

if __name__ == '__main__':
    try:
        check(sys.argv[1] if len(sys.argv)>1 else ROOT/'firmware/Output/code.map',
              sys.argv[2] if len(sys.argv)>2 else ROOT/'firmware/appearance.h')
    except (ValueError, AttributeError) as exc:
        raise SystemExit(str(exc))
