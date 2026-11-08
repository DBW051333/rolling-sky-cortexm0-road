"""One-off level tuning: drop unreachable collision cells, add lasers/movers.

Both edits are checked against the same rules the packer enforces, and the
reachability model here is the one used for the audit: a cell is walkable when
the ground run covers it and no collision tile sits on it, lateral moves need
the neighbouring cell walkable, and a jump pad bridges from its own row to the
full-width strip four rows later (which is how chain() builds every flight).

Run it, then tools/make_sd_levels.py, then rebuild the image.
"""
import json
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / 'levels/levels.json'
BLOCK = {2, 4, 5, 7}
KIND_NAMES = {1: 'laser', 2: 'mover', 3: 'hammer', 4: 'roller', 5: 'fly'}


def floor_of(level):
    n = level['rows']
    floor = [31] * n
    for first, count, mask in level['ground']:
        for r in range(first, first + count):
            floor[r] = mask
    return floor


def grids(level):
    n = level['rows']
    floor = floor_of(level)
    block = [[0] * 5 for _ in range(n)]
    pads = [[0] * 5 for _ in range(n)]
    for row, cols, kind in level['tiles']:
        for c in range(5):
            if cols >> c & 1:
                if kind in BLOCK:
                    block[row][c] = 1
                if kind in (1, 6):
                    pads[row][c] = 1
    return floor, block, pads


def reachable(level, block, pads):
    n = level['rows']
    floor = floor_of(level)

    def walk(r, c):
        return 0 <= r < n and 0 <= c < 5 and bool(floor[r] >> c & 1) and not block[r][c]

    start = []
    for r in range(n):
        start = [(r, c) for c in range(5) if walk(r, c)]
        if start:
            break
    seen = set(start)
    queue = deque(start)
    while queue:
        r, c = queue.popleft()
        for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            cell = (r + dr, c + dc)
            if walk(*cell) and cell not in seen:
                seen.add(cell)
                queue.append(cell)
        if pads[r][c] and r + 4 < n:
            for cc in range(5):
                cell = (r + 4, cc)
                if walk(*cell) and cell not in seen:
                    seen.add(cell)
                    queue.append(cell)
    return seen


def strip_unreachable(level):
    """Remove collision cells that the ball can never stand on even if the
    obstacle were gone: those boxes are drawn every frame and can never be hit.

    Two cells are deliberately left alone:
    * anything on a mandatory gate row - those boxes are the walls that force
      the player into the gate lane, so "unreachable" is exactly their job and
      deleting one opens a floor bypass the packer rejects;
    * a full five-lane air beam - the overhead arch reads as a portal and its
      ends hang over the abyss on purpose.
    """
    floor, block, pads = grids(level)
    gate_rows = {g[0] for g in level.get('gates', [])}
    removed = []
    kept = []
    for row, cols, kind in level['tiles']:
        if kind not in BLOCK or cols == 31 or row in gate_rows:
            kept.append([row, cols, kind])
            continue
        mask = cols
        for c in range(5):
            if not mask >> c & 1:
                continue
            block[row][c] = 0
            seen = reachable(level, block, pads)
            block[row][c] = 1
            if (row, c) not in seen:
                mask &= ~(1 << c)
                removed.append((row, c + 1, kind))
        if mask:
            kept.append([row, mask, kind])
    level['tiles'] = kept
    return removed


def trim_floating_air(level):
    """Air cubes are the only tile type the packer lets float, and a few of them
    sit over lanes that have no floor at all in that row: a cube hanging in
    empty space with nothing to frame it. Keep the full-five-lane beams (they
    read as arches) and clip every other cube back to the road under it."""
    floor = floor_of(level)
    out = []
    trimmed = []
    for row, cols, kind in level['tiles']:
        if kind == 5 and cols != 31:
            keep = cols & floor[row]
            if keep != cols:
                trimmed.append((row, bin(cols & ~floor[row] & 31).count('1')))
            if not keep:
                continue
            cols = keep
        out.append([row, cols, kind])
    level['tiles'] = out
    return trimmed


def free_windows(level, min_lanes=3, span=6, spacing=14):
    n = level['rows']
    floor, block, pads = grids(level)
    tiles = {}
    for row, cols, kind in level['tiles']:
        tiles.setdefault(row, []).append(cols)
    busy = sorted(m[0] for m in level.get('mechs', []))
    out = []
    for r in range(6, n - 8):
        if any(abs(r - m) < spacing for m in busy):
            continue
        lanes = []
        for lane in range(5):
            ok = True
            for rr in range(r - 5, r + 1):
                if bin(floor[rr]).count('1') < min_lanes or not floor[rr] >> lane & 1:
                    ok = False
                    break
                if any(cols >> lane & 1 for cols in tiles.get(rr, [])):
                    ok = False
                    break
            if ok:
                lanes.append(lane + 1)
        if len(lanes) >= 2:
            out.append((r, lanes))
    return out


def add_mechs(level, lasers, movers, gap=40):
    """Place extra hazards where the road is wide enough to dodge them.

    A laser owns its lane for six rows (five of telegraph plus the beam), so a
    candidate needs three lanes of floor across that whole window; a mover
    drifts sideways and only needs its base cell to be clean. Added hazards
    keep 40 rows from anything else, or the level turns into a wall of posts.
    """
    floor = floor_of(level)
    have = [list(m) for m in level.get('mechs', [])]
    added = []
    windows = free_windows(level)
    for kind, want, period in (('laser', lasers, 2048), ('mover', movers, 2048)):
        picked = 0
        for row, lanes in windows:
            if picked >= want:
                break
            if any(abs(row - m[0]) < gap for m in have):
                continue
            lane = lanes[len(lanes) // 2]
            if not floor[row] >> (lane - 1) & 1:
                continue
            phase = (row * 7) % period
            param = 384 if kind == 'laser' else 256
            have.append([row, lane, kind, period, phase, param])
            added.append((row, lane, kind))
            picked += 1
    have.sort(key=lambda m: (m[0], m[1]))
    level['mechs'] = have
    return added


def main():
    doc = json.loads(PATH.read_text(encoding='utf-8'))
    for level in doc['levels']:
        trimmed = trim_floating_air(level)
        if trimmed:
            print(f"id={level['id']} {level['name']}: clipped floating air {trimmed}")
        removed = strip_unreachable(level)
        if removed:
            print(f"id={level['id']} {level['name']}: removed {len(removed)} unreachable cells {removed}")
    for lid, lasers, movers, gap in ((1, 2, 1, 20), (2, 2, 1, 20), (3, 6, 6, 40)):
        level = next(l for l in doc['levels'] if l['id'] == lid)
        added = add_mechs(level, lasers, movers, gap)
        print(f"id={lid} {level['name']}: added {added}")
    PATH.write_text(json.dumps(doc, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
