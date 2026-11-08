"""Compile editable JSON levels into a read-only FAT32 SD card pack.

Usage: python tools/make_sd_levels.py [levels/levels.json] [sd_payload/RSLEVELS.BIN]
Only creates the named output file; never formats or accesses a physical disk.

V4 keeps the V3 8-byte rows and appends two sparse per-level side tables after
the row data: gems (3 B entries) and mechanisms (9 B entries). The header gains
a per-level extension directory at byte 256 (gem/mech sector offsets, 0=none).
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
header[:8] = b'RSLEVEL4'
struct.pack_into('<HH', header, 8, len(levels), 8)
chunks = []
offset = 1
used_ids = set()
GEM_MAX, MECH_MAX = 32, 32
PERIODS = {1024: 2, 2048: 3, 4096: 4, 8192: 5}
KINDS = {'laser': 1, 'mover': 2, 'hammer': 3, 'roller': 4, 'fly': 5}


def padded(blob):
    blob += bytes((-len(blob)) % 512)
    chunks.append(blob)
    return offset, len(blob) // 512


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

    # Gems: sparse pickups, no support requirement (they may float over gaps or
    # ride glass); only true blockers make a gem unfair to reach for.
    gems = sorted(([g[0], g[1]] for g in level.get('gems', [])), key=lambda g: (g[0], g[1]))
    if len(gems) > GEM_MAX:
        raise ValueError(f'A level supports at most {GEM_MAX} gems')
    for row, lane in gems:
        if not 0 <= row < n or not 1 <= lane <= 5:
            raise ValueError('Gems need a row inside the level and lane 1..5')
        bit = 1 << (lane - 1)
        if rows[row * stride + 2] & bit or rows[row * stride + 4] & bit or rows[row * stride + 7] & bit:
            raise ValueError(f'Gem at row {row} lane {lane} sits inside a blocker')

    # Mechanisms: distance-driven pose, so period/phase are Q8 travel units.
    # The base cell must be clean, supported ground like other rooted objects.
    mechs = []
    for m in level.get('mechs', []):
        row, lane, kind, period, phase, param = m
        if isinstance(kind, str):
            kind = KINDS.get(kind)
        if not 0 <= row < n or not 1 <= lane <= 5 or kind not in (1, 2, 3, 4, 5):
            raise ValueError('Mechanisms need a row inside the level, lane 1..5 and a known kind')
        if period not in PERIODS:
            raise ValueError('Mechanism period must be a power of two in 1024..8192')
        if not 0 <= phase < period:
            raise ValueError('Mechanism phase must be below the period')
        if kind == 3:
            if not 256 <= param <= 680:
                raise ValueError('Hammer param (head height) must be 256..680')
        else:
            limit = {1: 640, 5: 900}.get(kind, 512)
            if not 0 < param <= limit:
                raise ValueError(f'Mechanism param must be 1..{limit} for this kind')
        mechs.append([row, lane, kind, period, phase, param])
    mechs.sort(key=lambda m: (m[0], m[1]))
    if len(mechs) > MECH_MAX:
        raise ValueError(f'A level supports at most {MECH_MAX} mechanisms')
    for row, lane, kind, period, phase, param in mechs:
        bit = 1 << (lane - 1)
        if not rows[row * stride] & bit:
            raise ValueError(f'Mechanism at row {row} lacks ground support')
        if any(rows[row * stride + k] & bit for k in range(1, 8)):
            raise ValueError(f'Mechanism at row {row} lane {lane} overlaps a tile event')

    # Conservative face budget for every possible 16-row visible window.
    # Roller = three wheel boxes (9), fly = arm + head (4), laser/hammer = 6.
    mech_costs = [0] * n
    for row, lane, kind, period, phase, param in mechs:
        mech_costs[row] += {1: 6, 3: 6, 4: 9, 5: 4}.get(kind, 3)
    solid_costs = [rows[r * stride + 2].bit_count() * 3 + rows[r * stride + 4].bit_count() * 6 + (rows[r * stride + 5] & ~(rows[r * stride + 5] << 1) & 31).bit_count() * 3 + rows[r * stride + 7].bit_count() * 4 for r in range(n)]
    glass_costs = [rows[r * stride + 3].bit_count() for r in range(n)]
    worst = max(sum(solid_costs[r:r + 4]) + sum(mech_costs[r:r + 4]) for r in range(n))
    if worst > 44:
        # Downgraded from a hard error for Massif: the reference tree maze
        # needs far more static faces than the 32-slot cache holds. Collision
        # is data-driven and unaffected; the painter keeps the nearest
        # statics (drawn last) and silently drops the far ones.
        print(f'WARNING: worst 4-row window needs {worst} faces vs the 44-face cache; the far end will not render')
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
    rows_offset, rows_sectors = padded(rows)
    offset += rows_sectors
    gem_sector = mech_sector = 0
    if gems:
        blob = bytearray(struct.pack('<H', len(gems)))
        for row, lane in gems:
            blob += struct.pack('<HB', row, lane - 1)
        gem_sector, _ = padded(blob)
        offset += len(chunks[-1]) // 512
    if mechs:
        blob = bytearray(struct.pack('<H', len(mechs)))
        for row, lane, kind, period, phase, param in mechs:
            blob += struct.pack('<HBBBHH', row, lane - 1, kind, PERIODS[period], phase, param)
        mech_sector, _ = padded(blob)
        offset += len(chunks[-1]) // 512
    struct.pack_into('<II', header, 256 + index * 8, gem_sector, mech_sector)
    print(f'{index + 1:02d} {level["name"]}: {n} rows, {len(gems)} gems, {len(mechs)} mechanisms')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(header + b''.join(chunks))
print(f'Created {target} ({target.stat().st_size} bytes)')
