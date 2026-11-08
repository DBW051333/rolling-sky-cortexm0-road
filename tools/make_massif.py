"""Convert the reference Tiled export of Massif (Hills) into project JSON.

Usage: python tools/make_massif.py [reference_bundle_Level.txt]

The reference file is extracted game data and deliberately stays OUT of the
repository (default lookup: .wiki-ref/bundle_Level.txt, next to the toolchain
paths that HANDOFF_GUIDE also keeps unrecorded). Only layout facts are
re-encoded here, into levels/levels.json (level id 5) plus palettes and
row-range bindings in levels/appearance.json. Run make_appearance.py and
make_sd_levels.py afterwards to refresh the card pack.

VARIANT: the reference matches the Massif B anchor percentages (tree maze
44-60% vs wiki 56%, rollers 74-76% vs 75%, star 81% vs 80%) and the port
catalogue's own "Massif B" note; an earlier C-version claim here was a
percentage-conversion slip. C-only content (40 gems, future/ice/desert
scenes) is absent from this snapshot.

Tile semantics come from the game's own decoder (decompiled TileVal.cs), not
from guessing at screenshot percentages; Tiled stores the rows bottom-up, so
play order is the reverse of the file:
  1 base floor, 10 EnemyCrusherSafe (rolled through),
  19/20 SliderLeft/Right = the moving floor, static-walkable here
  3..8 every GroundFragile variant -> TILE_FRAGILE (the devil-path checker
      sheet and the fantasy row are whole glass fields, not scenery)
  2/32/112/116 Jump / SuperJump / StartJump / EndJump -> TILE_JUMP (our
      contact-triggered 4-row flight, ROAD_JUMP_MS 1366, clears the spacing)
  13..16 EnemyRiser -> TILE_GROUND_BLOCK (the square pillars)
  21..24 EnemyTallTree (+slider riders) -> TILE_SOLID (maze walls)
  109/110/113/114 Start/End strips -> walkable; 111/115 -> glass
  49..56 the Pickup family -> gems, positions taken from the map itself
  9 EnemyCrusher -> MECH_FLY, 101 EnemyLaser -> MECH_LASER,
  25/26 EnemyRollerLeft/Right -> MECH_ROLLER
  everything else (29/30/31 Floater, 61/62/65/66/77/78 Binary twins,
  97..100 Spotlight lamps, rare stamps) has no engine expression yet -> void,
  with a per-row fallback that promotes the row's cells to plain floor when
  nothing else walkable remains (backdrop rows such as 306 [0,24,20,20,24]
  then stay passable instead of walling the road).

Tree rule: a row whose ONLY non-floor content is tree brushes keeps them as
colliding solids when some other lane stays walkable (maze walls); a row
with no other walkable cell at all is backdrop and its trees flatten to
floor (the ball could never be grounded there anyway without a pad).

Palettes: floor colours are SAMPLED from reference screenshots of the
scenes this map runs through -- the opening orange from the gameplay video,
the devil path's lavender checker from the horizontal-pendulum screenshot,
the maze green and blue glass from the 56% tree-maze screenshot, the
fantasy dark cyan from the roller screenshot. The art cache cannot hold one
palette per scene plus the squashed background, so four are written
(hills/forest/gold/fantasy) and the later scenes reuse them; levels 1-3 are
bound explicitly to 'hills' because the unbound fallback slot is gone.
"""
import collections
import json
import re
import sys
from pathlib import Path

base = Path(__file__).resolve().parents[1]
reference = Path(sys.argv[1]) if len(sys.argv) > 1 else base / '.wiki-ref/bundle_Level.txt'

LEVEL_ID = 5
LEVEL_NAME = 'Massif'

# Decode table = the game's own TileVal enum (decompiled TileVal.cs), not
# guesses. Every brush below is authoritative:
FLOOR = {1, 10, 19, 20}    # Ground; EnemyCrusherSafe (harmless twin, rolled
                           # through); SliderLeft/Right = the moving floor,
                           # static-walkable here (Hills "just walk straight")
GLASS = {3, 4, 5, 6, 7, 8} # every GroundFragile variant: single panels plus
                           # the big checker/row/column sheets of the devil
                           # path and the fantasy section
PAD = {2, 32, 112, 116}    # Jump / SuperJump / StartJump / EndJump
RISER = {13, 14, 15, 16}   # EnemyRiser family -> TILE_GROUND_BLOCK (方柱)
TREE = {21, 22, 23, 24}    # EnemyTallTree (+slider riders) -> TILE_SOLID
STRIP = {109, 110, 113, 114}   # Start/End ground+empty -> walkable strips
STRIPGLASS = {111, 115}    # Start/End fragile -> glass
PICKUP = {49, 50, 51, 52, 53, 54, 55, 56}   # diamonds; 54 = gem on a pad
CRUSHER, LASER = 9, 101
# EnemyRollerLeft/Right also carry the sweep phase. A saw group is 2.5 tiles of
# artwork but only 2 tiles of collision, and the five-lane road narrows to two
# lanes right before and after this section, so the phase is not decorative: it
# decides whether a lane stays open. These two values come from
# tools/audit_mechs.py, which propagates the reachable x-interval through the
# whole level. With them every row of both groups keeps at least 265 Q8 of
# corridor (a bit over one lane); the old pseudo-random phase closed row 379.
ROLLERS = {25: 1216, 26: 1984}
ROLLER_SWEEP = 192
# Binary doors (61..68, 73..80): paired blocks that slide out of the ground on
# alternating halves of a cycle. MECH_MOVER is exactly that motion, so these
# 34 cells are emitted as movers instead of being dropped -- until 2026-11-03
# they were invisible empty track that still blocked the lane.
BINARY = {61, 62, 63, 64, 65, 66, 67, 68, 73, 74, 75, 76, 77, 78, 79, 80}
# void/decor (no engine expression yet): 29/30/31 Floater (harmless in this
# level), 61/62/65/66/77/78 Binary twins (alternating, deferred),
# 97-100 Spotlight lamps, 105, 117-132 star pickups, 121-144 stamps.
# kind codes of make_sd_levels: 1 jump, 3 fragile, 4 solid, 7 ground block
TILE_JUMP, TILE_FRAGILE, TILE_SOLID, TILE_GROUND_BLOCK = 1, 3, 4, 7


def parse_first_layer(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    m = re.search(r'\[layer\]\s*type=([^\n]*)\s*data=\n(.*?)(?=\n\[|\Z)', text, re.S)
    if not m:
        raise SystemExit('no [layer] section found in reference file')
    rows = []
    for line in m.group(2).strip().splitlines():
        line = line.strip().rstrip(',')
        if not line:
            continue
        vals = [int(v) for v in line.split(',') if v.strip() != '']
        if vals:
            rows.append(vals)
    rows = rows[::-1]                       # Tiled stores the level bottom-up
    last = max(i for i, r in enumerate(rows) if any(r))
    return rows[:last + 1]


def build_level(all_rows):
    part = [r[:5] for r in all_rows]
    n = len(part)
    tiles, mechs, promoted, walk = [], [], [], []
    gems, crushers, rollers = [], [], []
    for y, r in enumerate(part):
        base_mask = 0
        tree_bits = 0
        for x, v in enumerate(r):
            if v in FLOOR or v in GLASS or v in PAD or v in RISER or v in STRIP or v in STRIPGLASS:
                base_mask |= 1 << x
            elif v in TREE:
                tree_bits |= 1 << x
            elif v == CRUSHER or v == LASER or v in ROLLERS or v in BINARY:
                base_mask |= 1 << x   # the mechanism stands/crosses on this cell
            elif v in (49, 50, 51, 54, 55):   # 49 = Pickup on plain ground
                base_mask |= 1 << x   # gem combos ride their glass/slider/pad
        if base_mask == 0 and any(r):
            for x, v in enumerate(r):
                if v != 0:
                    base_mask |= 1 << x
            promoted.append(y)
            walk.append(base_mask)
            continue                  # backdrop row: no solid tiles emitted
        for x, v in enumerate(r):
            if v in PICKUP:
                gems.append([y, x + 1])   # every Pickup brush is a diamond
        for x, v in enumerate(r):
            bit = 1 << x
            if v in RISER or v in BINARY:
                tiles.append([y, bit, TILE_GROUND_BLOCK])
            elif v in TREE:
                tiles.append([y, bit, TILE_SOLID])
            elif v in GLASS or v in STRIPGLASS or v in (50, 51):
                tiles.append([y, bit, TILE_FRAGILE])   # 50/51 = gem on glass
            elif v in PAD or v == 54:
                tiles.append([y, bit, TILE_JUMP])      # 54 = gem on a pad
        for x, v in enumerate(r):
            if v == CRUSHER:
                mechs.append([y, x + 1, 'fly', 2048, (y * 137) & 2047, 450])
            elif v == LASER:
                mechs.append([y, x + 1, 'laser', 2048, 0, 640])
            elif v in ROLLERS:
                mechs.append([y, x + 1, 'roller', 2048, ROLLERS[v], ROLLER_SWEEP])
        walk.append(base_mask | tree_bits)
    if len(walk) != n:
        raise SystemExit('row walk mismatch')
    mechs.sort(key=lambda m: (m[0], m[1]))
    tiles.sort(key=lambda t: (t[0], t[1]))
    gems.sort()
    return n, walk, tiles, mechs, gems, promoted


def ground_runs_of(walk):
    runs, start, cur = [], 0, walk[0]
    for y in range(1, len(walk)):
        if walk[y] != cur:
            runs.append([start, y - start, cur])
            start, cur = y, walk[y]
    runs.append([start, len(walk) - start, cur])
    return runs




def update_levels(levels_path, n, runs, tiles, mechs, gems):
    doc = json.loads(levels_path.read_text(encoding='utf-8'))
    entry = {'name': LEVEL_NAME, 'id': LEVEL_ID, 'rows': n,
             'ground': runs, 'tiles': tiles, 'mechs': mechs, 'gems': gems}
    doc['levels'] = [l for l in doc['levels'] if l.get('id') != LEVEL_ID]
    doc['levels'].append(entry)
    levels_path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + '\n',
                           encoding='utf-8')
    return len(doc['levels'])


def update_appearance(appearance_path):
    doc = json.loads(appearance_path.read_text(encoding='utf-8'))
    # All four scene palettes sampled from reference material: the opening
    # orange from the gameplay video the user compared against, the devil
    # path purple from the horizontal-pendulum screenshot, the maze green
    # and blue glass from the 56% tree-maze screenshot, the fantasy dark
    # cyan from the roller screenshot. hills is mutated in place -- it is
    # shared with level 4 on purpose (level 4 is a Hills homage).
    hills = next(p for p in doc['palettes'] if p['name'] == 'hills')
    hills['background'] = 0xf4cb   # salmon sky
    # The reference floor is near-uniform orange with a soft seam -- no strong
    # two-tone checker (that read as harsh stripes on the panel).
    hills['light'] = 0xfda8        # #ffb546 tile top (the video's start)
    # `dark` is no longer a checker partner -- the reference floor is a single
    # colour seamed by thin grid lines (maze_hardest.png). The slot now carries
    # the blocked-cell tint: a much darker plate that reads as "a pillar stands
    # here" from the horizon, which the face cache cannot afford to draw.
    hills['dark'] = 0xb283         # #b0521a sunken plate for occupied cells
    hills['grid'] = 0xcbc3         # #cd7918 seam: 0xe096 was magenta (230,16,180)
    forest = next(p for p in doc['palettes'] if p['name'] == 'forest')
    # Log-forest maze: green tiles and blue glass panels (56% screenshot).
    forest['light'] = 0x668d       # #65d16b tile top
    forest['dark'] = 0x2b47        # #2f6b3a sunken plate for occupied cells
    forest['grid'] = 0x3caa        # #3c9650 seam, one step below the floor
    forest['fragile'] = 0x445f     # #448afd glass panel
    forest['fragile_edge'] = 0x66bd
    gold = dict(hills)
    gold['name'] = 'massif_gold'
    # Devil path: lavender/purple checker (horizontal-pendulum screenshot).
    # Its floor IS fragile checker glass, so fragile/edge carry the purple.
    gold['background'] = 0xc38e
    gold['light'] = 0xeedf         # #eadbfb
    gold['dark'] = 0x7af5          # #7a5cae sunken plate for occupied cells
    gold['grid'] = 0x93d9          # #9678c8 seam (0x8c2a was magenta)
    gold['fragile'] = 0xb43a
    gold['fragile_edge'] = 0xeedf
    fantasy = dict(forest)
    fantasy['name'] = 'massif_fantasy'
    # Fantasy-Light: near-black blue tiles with cyan grid (roller screenshot).
    fantasy['light'] = 0x1104      # #172027
    # The blocked-cell plate has to be *warmer* than the floor here: a lighter
    # navy vanished into the near-black tiles at the far end.
    fantasy['dark'] = 0x7a43       # #7a4a1f amber plate for occupied cells
    fantasy['grid'] = 0x02cd        # #005868 muted teal seam (0x0517 glared)
    # Glass must read as glass (reference: blue panels on a dark floor); the
    # old 0x18e6 was the same near-black as the floor.
    fantasy['fragile'] = 0x2b7a    # #2b6fd0
    fantasy['fragile_edge'] = 0x7eff
    # Rebuilt from scratch every run so the pack stays idempotent. The
    # background was squashed to 160 rows to make room for the fourth
    # palette; rows past it extend the flat bottom colour.
    doc['palettes'] = [hills, forest, gold, fantasy]
    doc['bindings'] = [
        {'level_id': 1, 'first_row': 0, 'end_row': 65535, 'palette': 'hills'},
        {'level_id': 2, 'first_row': 0, 'end_row': 65535, 'palette': 'hills'},
        {'level_id': 3, 'first_row': 0, 'end_row': 65535, 'palette': 'hills'},
        {'level_id': 4, 'first_row': 0, 'end_row': 32, 'palette': 'hills'},
        {'level_id': 4, 'first_row': 32, 'end_row': 96, 'palette': 'forest'},
        {'level_id': 4, 'first_row': 96, 'end_row': 192, 'palette': 'hills'},
        {'level_id': 4, 'first_row': 192, 'end_row': 224, 'palette': 'forest'},
        {'level_id': 4, 'first_row': 224, 'end_row': 65535, 'palette': 'hills'},
        {'level_id': LEVEL_ID, 'first_row': 0, 'end_row': 70, 'palette': 'hills'},
        {'level_id': LEVEL_ID, 'first_row': 70, 'end_row': 172, 'palette': 'massif_gold'},
        {'level_id': LEVEL_ID, 'first_row': 172, 'end_row': 312, 'palette': 'forest'},
        {'level_id': LEVEL_ID, 'first_row': 312, 'end_row': 372, 'palette': 'massif_fantasy'},
        {'level_id': LEVEL_ID, 'first_row': 372, 'end_row': 65535, 'palette': 'hills'},
    ]
    appearance_path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + '\n',
                               encoding='utf-8')


if __name__ == '__main__':
    rows = parse_first_layer(reference)
    print(f'reference: {len(rows)} playable rows in section "Level 1" (Massif B)')
    n, walk, tiles, mechs, gems, promoted = build_level(rows)
    runs = ground_runs_of(walk)
    print(f'level: {n} rows, {len(runs)} ground runs, {len(tiles)} tiles, '
          f'{len(mechs)} mechs, {len(gems)} gems (from the map\'s Pickup brushes)')
    kinds = collections.Counter(m[2] for m in mechs)
    print('mech histogram:', dict(kinds))
    print('tile histogram:', {k: sum(1 for t in tiles if t[2] == k) for k in (1, 3, 4, 7)})
    if promoted:
        print('backdrop rows promoted to floor:', promoted)
    bad = [(y, walk[y]) for y in range(len(walk)) if walk[y] == 0]
    print('all-void rows:', bad if bad else 'none')
    count = update_levels(base / 'levels/levels.json', n, runs, tiles, mechs, gems)
    update_appearance(base / 'levels/appearance.json')
    print(f'levels.json now holds {count} levels (id {LEVEL_ID} replaced); appearance.json updated')
