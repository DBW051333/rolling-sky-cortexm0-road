"""Reachability audit for the mechanisms of a level.

Mirrors the firmware's own maths -- obstacle.c poses, level_step's ground
support test, ball radius 24 -- and asks a stricter question than the play
simulator does: is there a continuous steering path, given that the ball can
only move 2 Q8 sideways per ms while it advances 0.75 Q8 forward?

The answer is computed as reachable x-intervals over distance, so it is exact
for a grounded ball instead of sampling lanes at row centres. Output is the
first z where the reachable set becomes empty plus the mechanism responsible,
which is what "根本过不去" looks like numerically.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RADIUS = 24
ROLLER_HALF = 256      # keeps the firmware's saw-group box in one place
STEP_Z = 4                 # Q8 of travel per propagation step
PAN_PER_Z = 2.0 / 0.75     # lateral Q8 per Q8 of travel
CAM_MIN, CAM_MAX = 128, 1152

SWING = [0, 98, 181, 237, 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98]

TILE_JUMP, TILE_LOW_BARRIER, TILE_FRAGILE, TILE_SOLID, TILE_AIR_BLOCK, TILE_SHORT, TILE_GROUND_BLOCK = 1, 2, 3, 4, 5, 6, 7
STATIC_RULES = {
    TILE_LOW_BARRIER: (48, 208, 24, 40, 0, 680),
    TILE_SOLID: (48, 208, 48, 208, 0, 1024),
    TILE_AIR_BLOCK: (48, 208, 64, 192, 600, 1024),
    TILE_GROUND_BLOCK: (48, 208, 48, 208, 0, 680),
}


def phase(m, z):
    return (z - m[0] * 256 + m[4]) & ((256 << m[3]) - 1)


def mover_offset(m, z):
    period = 256 << m[3]
    quarter = period >> 2
    ph = phase(m, z)
    pos = ph if ph < period // 2 else period - ph
    return ((pos - quarter) * m[5]) >> (m[3] + 6)


def fly_height(m, z):
    idx = (phase(m, z) >> (m[3] + 4)) & 15
    return 450 + (SWING[idx] * m[5] >> 8)


def mech_box(m, z):
    row, lane, kind, shift, ph0, param = m
    col = lane - 1                      # the pack stores lanes 1..5, the
    base = row * 256                    # firmware keeps columns 0..4
    if kind == 2:                                   # mover
        center = col * 256 + 128 + mover_offset(m, z)
        return (center - 80, center + 80, base + 48, base + 208, 0, 256)
    if kind == 3:                                   # hammer
        center = 640 + ((SWING[(phase(m, z) >> (shift + 4)) & 15] * 196 >> 8) * 1100 >> 8)
        s = (SWING[(phase(m, z) >> (shift + 4)) & 15] * 196) >> 8
        height = 1256 - (((256 - ((s * s) >> 9)) * 1100) >> 8)
        return (center - 112, center + 112, base + 48, base + 208,
                max(0, height - 160), height + 160)
    if kind == 4:                                   # roller
        center = col * 256 + 128 + mover_offset(m, z)
        return (center - ROLLER_HALF, center + ROLLER_HALF, base + 48, base + 208, 0, 280)
    if kind == 5:                                   # fly
        h = fly_height(m, z)
        return (col * 256 + 32, col * 256 + 224, base + 48, base + 208,
                max(0, h - 88), h + 88)
    if kind == 1:                                   # laser
        d = base - z
        if d > 2304 or d < -208:
            return None
        return (col * 256 + 48, col * 256 + 208, base - 1152, base + 208, 0, 256)
    return None


def hits(box, x, z, bottom):
    x0, x1, z0, z1, y0, y1 = box
    cy = bottom + RADIUS
    dx = x0 - x if x < x0 else (x - x1 if x > x1 else 0)
    dy = y0 - cy if cy < y0 else (cy - y1 if cy > y1 else 0)
    dz = z0 - z if z < z0 else (z - z1 if z > z1 else 0)
    return dx * dx + dy * dy + dz * dz <= RADIUS * RADIUS


class Level:
    def __init__(self, path, lid):
        doc = json.loads(Path(path).read_text(encoding='utf-8'))
        lv = next(l for l in doc['levels'] if l['id'] == lid)
        self.rows = lv['rows']
        self.ground = lv['ground']
        self.tiles = {}
        for row, cols, kind in lv['tiles']:
            self.tiles.setdefault(row, []).append((cols, kind))
        # levels.json stores the period (1024..8192); the firmware and the
        # card pack store its shift (2..5). Convert once, here.
        kinds = {'laser': 1, 'mover': 2, 'hammer': 3, 'roller': 4, 'fly': 5}
        shifts = {1024: 2, 2048: 3, 4096: 4, 8192: 5}
        self.mechs = [tuple([m[0], m[1], kinds.get(m[2], m[2]),
                             shifts.get(m[3], m[3]), m[4], m[5]])
                      for m in lv.get('mechs', [])]
        self.gems = lv.get('gems', [])

    def mask(self, row):
        for first, count, mask in self.ground:
            if row < first:
                break
            if first <= row < first + count:
                return mask
        return 31


def merge(iv):
    iv = sorted(iv)
    out = []
    for a, b in iv:
        if out and a <= out[-1][1]:
            out[-1][1] = max(out[-1][1], b)
        else:
            out.append([a, b])
    return out


def subtract(spans, cuts):
    for a, b in cuts:
        nxt = []
        for lo, hi in spans:
            if b <= lo or a >= hi:
                nxt.append([lo, hi])
                continue
            if lo < a:
                nxt.append([lo, a])
            if b < hi:
                nxt.append([b, hi])
        spans = nxt
        if not spans:
            break
    return spans


def audit(level, start=0, stop=None, verbose=False):
    spans = [[CAM_MIN, CAM_MAX]]
    failures = []
    tight = (10 ** 9, 0)        # narrowest reachable width and where
    prev_report = -1
    z = start * 256
    end = (level.rows if stop is None else stop) * 256
    while z < end:
        row = z >> 8
        cuts = []
        # ground support: the ball falls when its centre column is not walkable
        g = level.mask(row)
        for c in range(5):
            if not (g >> c) & 1:
                cuts.append((c * 256, (c + 1) * 256))
        # static blockers
        for r in {(z - RADIUS) >> 8, row, (z + RADIUS) >> 8}:
            for cols, kind in level.tiles.get(r, []):
                rule = STATIC_RULES.get(kind)
                if not rule:
                    continue
                x0, x1, z0, z1, y0, y1 = rule
                base = r * 256
                if base + z1 + RADIUS < z or base + z0 - RADIUS > z:
                    continue
                for c in range(5):
                    if (cols >> c) & 1 and hits((base + x0 + c * 256, base + x1 + c * 256,
                                                 base + z0, base + z1, y0, y1), c * 256 + 128, z, 0):
                        cuts.append((base + x0 + c * 256 - RADIUS, base + x1 + c * 256 + RADIUS))
        # mechanisms
        for m in level.mechs:
            base = m[0] * 256
            if base > z + 1280 or base + 640 < z:
                continue
            box = mech_box(m, z)
            if not box:
                continue
            x0, x1, z0, z1, y0, y1 = box
            if z1 + RADIUS < z or z0 - RADIUS > z:
                continue
            if not (y0 - RADIUS <= RADIUS <= y1 + RADIUS):   # grounded ball centre
                continue
            cuts.append((x0 - RADIUS, x1 + RADIUS))
        # one propagation step: spread laterally, then subtract blocked spans
        grown = []
        for lo, hi in spans:
            grown.append([lo - int(STEP_Z * PAN_PER_Z), hi + int(STEP_Z * PAN_PER_Z)])
        spans = subtract(merge(grown), merge(cuts))
        spans = [[max(lo, CAM_MIN), min(hi, CAM_MAX)] for lo, hi in spans]
        spans = [s for s in spans if s[0] < s[1]]
        if spans:
            width = sum(b - a for a, b in spans)
            if width < tight[0]:
                tight = (width, row)
        if not spans:
            failures.append((z, row, merge(cuts)))
            if not verbose:
                break
        if verbose and row != prev_report and row % 10 == 0:
            prev_report = row
            print(f'  row {row:3d} z={z:6d} gap={sum(b - a for a, b in spans):5d} Q8 spans={spans[:4]}')
        z += STEP_Z
    level.tight = getattr(level, 'tight', {})
    level.tight[(start, stop)] = tight
    return failures


def describe(level, failure, window=8):
    z, row, cuts = failure
    print(f'blocked at row {row} (z={z}, {100 * row // level.rows}%)')
    for m in level.mechs:
        if abs(m[0] - row) <= window:
            box = mech_box(m, m[0] * 256 + 128)
            print(f'   mech row {m[0]} lane {m[1]} kind {m[2]} phase {m[4]} param {m[5]}'
                  + (f' -> box x {box[0]}..{box[1]}' if box else ''))
    for r in range(max(0, row - 2), min(level.rows, row + 3)):
        print(f'   row {r}: ground {level.mask(r):05b} tiles {level.tiles.get(r, [])}')


def islands(level):
    """Maximal row ranges with ground, i.e. the places the ball can be
    grounded; gaps between them are crossed on a pad and audited separately."""
    out, start = [], None
    for row in range(level.rows):
        if level.mask(row):
            if start is None:
                start = row
        elif start is not None:
            out.append((start, row))
            start = None
    if start is not None:
        out.append((start, level.rows))
    return out


if __name__ == '__main__':
    lid = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    level = Level(ROOT / 'levels/levels.json', lid)
    bad = 0
    for a, b in islands(level):
        f = audit(level, start=a, stop=b)
        if f:
            bad += 1
            print(f'island rows {a}..{b - 1}: FAIL')
            describe(level, f[0])
        elif len(sys.argv) > 2:
            w, at = level.tight[(a, b)]
            print(f'island rows {a}..{b - 1}: ok, tightest reachable width {w} Q8 at row {at}')
    print('islands with no grounded path:', bad)
    failures = []
    if failures:
        for f in failures[:4]:
            describe(level, f)
    else:
        print(f'level {lid}: fully passable on the ground ({level.rows} rows)')
