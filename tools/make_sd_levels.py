"""Compile editable JSON levels into a read-only FAT32 SD card pack.

Usage: python tools/make_sd_levels.py [levels/levels.json] [sd_payload/RSLEVELS.BIN]
Only creates the named output file; never formats or accesses a physical disk.
"""
import json
import struct
import sys
from pathlib import Path

base = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else base / 'levels/levels.json'
target = Path(sys.argv[2]) if len(sys.argv) > 2 else base / 'sd_payload/RSLEVELS.BIN'
levels = json.loads(source.read_text(encoding='utf-8'))['levels']
if not 1 <= len(levels) <= 24:
    raise ValueError('The current card catalog supports 1..24 levels')
header = bytearray(512)
header[:8] = b'RSLEVEL3'
struct.pack_into('<HH', header, 8, len(levels), 8)
chunks = []
offset = 1
used_ids = set()
for index, level in enumerate(levels):
    n = level['rows']
    if not isinstance(n, int) or not 16 <= n <= 65535:
        raise ValueError('Level row count must be 16..65535')
    stride = 8
    rows = bytearray([31, 0, 0, 0, 0, 0, 0, 0] * n)
    level_id = level.get('id', index + 1)
    if not isinstance(level_id, int) or not 1 <= level_id <= 65535:
        raise ValueError('Level ID must be 1..65535; it contains no appearance data')
    if level_id in used_ids:
        raise ValueError('Level IDs must be unique')
    used_ids.add(level_id)
    struct.pack_into('<H', header, 208 + index * 2, level_id)
    previous_end = 0
    for first, count, mask in level['ground']:
        if first < previous_end or count < 1 or first + count > n or not 0 <= mask <= 31:
            raise ValueError('Ground runs must be ordered, non-overlapping and within the level')
        for row in range(first, first + count):
            rows[row * stride] = mask
        previous_end = first + count
    previous_row = -1
    wall_rows = set()
    for row, columns, kind in level['tiles']:
        if row < previous_row or row >= n or not 1 <= columns <= 31 or kind not in (1, 2, 3, 4, 5, 6, 7):
            raise ValueError('Tile events must be ordered, with valid row, columns and type')
        if kind != 5 and columns & ~rows[row * stride]:
            raise ValueError('Special objects must be supported by ground')
        rows[row * stride + kind] |= columns
        if kind == 2:
            wall_rows.add(row)
        previous_row = row
    if any(rows[r * stride + 3] & (rows[r * stride + 1] | rows[r * stride + 2] | rows[r * stride + 4] | rows[r * stride + 5] | rows[r * stride + 6] | rows[r * stride + 7]) for r in range(n)):
        raise ValueError('Glass cannot overlap another collision type in this version')
    if any(rows[r * stride + 1] & rows[r * stride + 6] for r in range(n)):
        raise ValueError('Normal and short jump triggers cannot overlap')
    # Conservative face budget for every possible 16-row visible window.
    solid_costs = [rows[r * stride + 2].bit_count() * 3 + rows[r * stride + 4].bit_count() * 9 + (rows[r * stride + 5] & ~(rows[r * stride + 5] << 1) & 31).bit_count() * 3 + rows[r * stride + 7].bit_count() * 4 for r in range(n)]
    glass_costs = [rows[r * stride + 3].bit_count() for r in range(n)]
    if any(sum(solid_costs[r:r + 16]) + min(12, sum(glass_costs[r:r + 16])) > 32 for r in range(n)):
        raise ValueError('Visible object faces exceed the 32-face cache')
    wall_rows = sorted(wall_rows)
    if any(b - a < 16 for a, b in zip(wall_rows, wall_rows[1:])):
        raise ValueError('Keep wall rows at least 16 rows apart for the current face budget')
    route_rows = set()
    for first, count, lane, mode in level.get('route', []):
        if mode not in ('ground', 'air') or not 1 <= lane <= 5 or first < 0 or count < 1 or first + count > n:
            raise ValueError('Route segments require a valid row range, lane 1..5 and ground/air mode')
        bit = 1 << (lane - 1)
        for row in range(first, first + count):
            if row in route_rows:
                raise ValueError('Route segments overlap')
            route_rows.add(row)
            if rows[row * stride + 4] & bit:
                raise ValueError('Authored route intersects a solid blocker')
            if mode == 'ground' and (not rows[row * stride] & bit or (rows[row * stride + 2] | rows[row * stride + 7]) & bit):
                raise ValueError('Ground route lacks support or intersects a low wall')
    if route_rows and len(route_rows) != n:
        raise ValueError('Authored route must cover every row')
    for row, lane, action in level.get('gates', []):
        if not 0 <= row < n or not 1 <= lane <= 5 or action not in ('ground', 'jump', 'short'):
            raise ValueError('Invalid mandatory gate descriptor')
        safe = rows[row * stride] & ~(rows[row * stride + 4] | rows[row * stride + 2] | rows[row * stride + 7]) & 31
        if safe != 1 << (lane - 1):
            raise ValueError(f'Gate at row {row} has a ground bypass or no support')
        if action in ('jump', 'short') and not rows[row * stride + (1 if action == 'jump' else 6)] & safe:
            raise ValueError(f'Gate at row {row} is missing its jump trigger')
    struct.pack_into('<II', header, 16 + index * 8, n, offset)
    padded = rows + bytes((-len(rows)) % 512)
    chunks.append(padded)
    offset += len(padded) // 512
    print(f'{index + 1:02d} {level["name"]}: {n} rows, {len(padded)} bytes')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(header + b''.join(chunks))
print(f'Created {target} ({target.stat().st_size} bytes)')
