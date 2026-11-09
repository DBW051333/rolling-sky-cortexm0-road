#!/usr/bin/env python3
"""Offline frame preview: renders one gameplay frame the way road.c does.

This is not a mock-up. Every number here is copied from the firmware so a
picture from this script and a picture on the panel differ only by the parts
listed at the bottom (walls, mechanisms, backdrop art):

  depth[y]  = (19552-20y)*256 / (1248+12y)      road_init()
  scale[y]  = (1248+12y)*256 / 52               road_init()
  screen x  = W/2 + trunc(world_x_q8*scale/65536)   projected_x()
  screen y  = 56 + trunc(384*(48*256-5*depth)
                         / (12*depth+20*256))   projected_y()
  row       = (camera + depth) >> 8             draw()
  tile cols = ((row+col)&1) ? light : dark      draw()
  row edge  = grid colour for the whole tile    draw()  (horizontal)
  backdrop  = sky ramp + two-layer silhouette   Backdrop (road.c)
  inside    = 1 px grid column, then tile fill  draw()

Usage:
    python tools/preview_frame.py                 # default: Massif, a few rows
    python tools/preview_frame.py 5 300 4 200 1   # level, row... , camera x

A camera x of 640 is the centre lane. Output goes to .workbuddy/preview/.
"""
import json
import os
import re
import sys
from math import isqrt
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
W, H = 198, 320
BALL_Y = 192
BALL_LEFT, BALL_TOP, BALL_W, BALL_H = (W-68)//2, 158, 68, 68


def rgb565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def depth_at(y):
    return (19552 - 20 * y) * 256 // (1248 + 12 * y)


def scale_at(y):
    return (1248 + 12 * y) * 256 // 52


def projected_x(wx_q8, sc):
    n = wx_q8 * sc
    return W//2 + (n >> 16 if n >= 0 else -((-n) >> 16))


def projected_y(d):
    num = 384 * (48 * 256 - 5 * d)
    den = 12 * d + 20 * 256
    return 56 + (num // den if num >= 0 else -((-num) // den))


def load_levels():
    return json.load(open(ROOT / 'levels/levels.json', encoding='utf-8'))['levels']


def load_look(level_id, row):
    a = json.load(open(ROOT / 'levels/appearance.json', encoding='utf-8'))
    names = {p['name']: p for p in a['palettes']}
    for b in a['bindings']:
        if b['level_id'] == level_id and b['first_row'] <= row < b['end_row']:
            return names[b['palette']]
    return a['palettes'][0]


def cue_span(level_id, row):
    """Row range of the scene the ball is in, mirroring appearance_cue_span()."""
    a = json.load(open(ROOT / 'levels/appearance.json', encoding='utf-8'))
    for b in a['bindings']:
        if b['level_id'] == level_id and b['first_row'] <= row < b['end_row']:
            return b['first_row'], b['end_row']
    return None


def ground_mask(level, row):
    if row >= level['rows']:
        return 0
    mask = 31
    for first, count, m in level['ground']:
        if row < first:
            break
        if first <= row < first + count:
            mask = m
            break
    return mask


def tile_mask(level, row, kind):
    m = 0
    for r, cols, k in level['tiles']:
        if r > row:
            break
        if r == row and k == kind:
            m |= cols
    return m


def ball_sprite():
    txt = open(ROOT / 'firmware/ball_sprite.h', encoding='utf-8').read()

    def arr(name):
        m = re.search(name + r'\[\]\s*=\s*\{([^}]*)\}', txt, re.S)
        body = m.group(1)
        return [int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+', body)]

    return arr('ball_row_offset'), arr('ball_runs'), arr('ball_palette')



# ---------------------------------------------------------------- solids --
# Port of walls.c: axis-aligned boxes are projected with the same formula and
# resolved per scanline, so a pillar or a tree covers the road exactly where
# the firmware puts it.
def tdiv(n, d):
    return -((-n) // d) if n < 0 else n // d


def project(x, z, h):
    d = 12 * z + 20 * 256 - 5 * h
    if d == 0:
        d = 1
    return (W//2 + tdiv(4992 * x, d), 56 + tdiv(384 * (48 * 256 - 5 * z - 12 * h), d))


def build_faces(level, look, models, camera, camera_x, depth_ball, z_ball,
                budget=True):
    # walls.c keeps 20 faces for scenery and lets the mechanisms fill the rest
    # of a 32-face cache; a row of trees therefore LOSES pillars on hardware.
    # The preview reproduces that, so a picture here is what the panel shows,
    # not what the level was drawn to be.
    face_limit = [32 if budget else 10 ** 9]
    faces = []

    def add(pts, color, fg):
        if len(faces) >= face_limit[0]:
            return
        ys = [q[1] for q in pts]
        faces.append({'p': pts, 'color': color & 0xFFFF, 'fg': fg,
                      'top': min(ys), 'bottom': max(ys)})

    def box(x, z, width, depth, bottom, top, front, side, roof, fg):
        a = project(x, z, bottom)
        b = project(x + width, z, bottom)
        c = project(x + width, z, top)
        d = project(x, z, top)
        e = project(x, z + depth, top)
        f = project(x + width, z + depth, top)
        if x + width // 2 < 0:
            add((b, project(x + width, z + depth, bottom), f, c), side, fg)
        else:
            add((project(x, z + depth, bottom), a, d, e), side, fg)
        add((d, c, f, e), roof, fg)
        add((a, b, c, d), front, fg)

    def model_boxes(index):
        if index < len(models):
            return models[index]['boxes']
        return [{'bounds': [0, 0, 256, 256, 0, 256],
                 'colors': ['solid_front', 'solid_side', 'solid_roof'], 'flags': 3}]

    reach = depth_at(0) + 256   # walls.c: depth_q8[0]+256, i.e. the far edge
    # walls.c picks the first record the camera has passed and takes the next
    # SCENERY_TILES, then paints far-to-near; reproduce that window exactly.
    lo = 0
    while lo < len(level['tiles']) and level['tiles'][lo][0] * 256 - camera < 512:
        lo += 1
    hi = len(level['tiles'])
    for idx in range(lo, hi):
        tile = level['tiles'][idx]
        row, columns, kind = tile
        z = row * 256 - camera
        if z < 512 or z > depth_ball + 4 * 256:   # SCENERY_ROWS
            continue
        fg = z < depth_ball
        if kind in (4, 5, 7):
            index = {4: look['solid_model'], 5: look['air_model'], 7: look['ground_model']}[kind]
            col = 0
            while col < 5:
                if not (columns >> col) & 1:
                    col += 1
                    continue
                x = col * 256 - camera_x
                width = 1
                if kind == 5:
                    while col + width < 5 and (columns >> (col + width)) & 1:
                        width += 1
                gap = z - depth_ball
                for b in model_boxes(index):
                    if (b['flags'] & 4) and gap > 512:
                        continue
                    bx, bz, bw, bd, bottom, top = b['bounds']
                    if b['flags'] & 2:
                        if gap >= 1024:
                            scale = 8
                        elif gap > 512:
                            scale = (1024 - gap) >> 1
                        else:
                            scale = 256
                        top = bottom + ((top - bottom) * scale >> 8)
                    cols = [look[c] for c in b['colors']]
                    box(x + bx * width, z + bz, bw * width, bd, bottom, top,
                        cols[0], cols[1], cols[2], fg)
                    if b['flags'] & 1:
                        add((project(x + bx * width + 28, z + bz + 28, top),
                             project(x + (bx + bw) * width - 28, z + bz + 28, top),
                             project(x + (bx + bw) * width - 28, z + bz + bd - 28, top),
                             project(x + bx * width + 28, z + bz + bd - 28, top)),
                            look['solid_inset'], fg)
                col += width
            continue
        if kind != 2:
            continue
        for col in range(5):
            if not (columns >> col) & 1:
                continue
            x = col * 256 - camera_x
            a = project(x, z, 0)
            b = project(x + 256, z, 0)
            c = project(x + 256, z, 128)
            d = project(x, z, 128)
            e = project(x, z + 64, 128)
            f = project(x + 256, z + 64, 128)
            if x + 256 < 0 and not (columns >> (col + 1)) & 1:
                add((b, project(x + 256, z + 64, 0), f, c), look['barrier_side'], fg)
            if x > 0 and (col == 0 or not (columns >> (col - 1)) & 1):
                add((project(x, z + 64, 0), a, d, e), look['barrier_side'], fg)
            add((d, c, f, e), look['barrier_top'], fg)
            add(((a[0] + 1, a[1]), (b[0] - 1, b[1]), (c[0] - 1, c[1]), (d[0] + 1, d[1])),
                look['barrier_front'], fg)
    static_count = len(faces)
    face_limit[0] = 44 if budget else 10 ** 9
    # Mechanisms, same poses as obstacle.c: the phase is driven by the
    # distance still to travel, so a snapshot is a well-defined moment.
    for m in level.get('mechs', []):
        row, col, kind = m[0], m[1], m[2]
        shift, phase, param = m[3], m[4], m[5]
        kind = {'laser': 1, 'mover': 2, 'hammer': 3, 'roller': 4, 'fly': 5}[kind]
        shift = {2048: 3, 1024: 2, 4096: 4, 8192: 5}.get(shift, 3) if shift in (1024, 2048, 4096, 8192) else shift
        z = row * 256 - camera
        if z < 64 or z > reach + 144:
            continue
        x = col * 256 - camera_x
        fg = z < depth_ball
        period = 256 << shift
        ph = (z_ball - row * 256 + phase) & (period - 1)
        pos = ph if ph < period // 2 else period - ph
        offset = ((pos - period // 4) * param) >> (shift + 6)
        sine = (0, 98, 181, 237, 256, 237, 181, 98, 0, -98, -181, -237,
                -256, -237, -181, -98)
        s = (sine[(ph >> (shift + 4)) & 15] * 196) >> 8
        if kind == 2:
            base = x + 128
            center = base + offset
            box(base - 112 - param, z + 160, (112 + param) * 2, 48, 0, 8,
                0x18c3, 0x10a2, 0x2945, fg)
            box(center - 112, z + 64, 224, 128, 0, 224,
                look['solid_front'], look['solid_side'], look['solid_roof'], fg)
        elif kind == 3:
            pivot = 640 - camera_x
            head = pivot + ((s * 1100) >> 8)
            height = 1256 - (((256 - ((s * s) >> 9)) * 1100) >> 8)
            add((project(pivot - 8, z + 96, 1256), project(pivot + 8, z + 96, 1256),
                 project(head + 16, z + 96, height + 40),
                 project(head - 16, z + 96, height + 40)), 0x7bcf, fg)
            box(head - 112, z + 48, 224, 160, height - 160, height + 160,
                0xd6bb, 0x8c71, 0xffff, fg)
        elif kind == 4:
            center = x + 128 + offset
            for dx in (-256, -80, 96):
                box(center + dx, z + 64, 160, 128, 0, 256, 0xfd20, 0x7a40, 0xffe0, fg)
        elif kind == 5:
            h = 450 + ((sine[(ph >> (shift + 4)) & 15] * param) >> 8)
            add((project(x + 108, z + 96, 1256), project(x + 148, z + 96, 1256),
                 project(x + 148, z + 96, h + 60),
                 project(x + 108, z + 96, h + 60)), 0x7bcf, fg)
            box(x + 32, z + 48, 192, 160, h - 88, h + 88, 0x8c71, 0x528a, 0xffff, fg)
        else:
            d = row * 256 - z_ball
            state = 0 if d > 2304 else 1 if d > 1280 else 2
            box(x + 80, z + 96, 96, 96, 0, 128, 0x2945, 0x1082, 0x4a69, fg)
            if state == 2:
                near = max(24, z - 1152)
                box(x + 16, near, 224, z + 48 - near, 0, 224,
                    0xe7ff, 0x0ad0, 0x05fa, fg)
                add((project(x + 88, near, 160), project(x + 152, near, 160),
                     project(x + 152, z + 40, 160), project(x + 88, z + 40, 160)),
                    0xd7ff, fg)
            rim = 0xf858 if state == 2 else 0xfd20 if state == 1 else 0x4a69
            eye = 0x0ef8 if state == 2 else 0x9a40 if state == 1 else 0x2945
            add((project(x + 16, z + 48, 16), project(x + 240, z + 48, 16),
                 project(x + 240, z + 48, 240), project(x + 16, z + 48, 240)), rim, fg)
            add((project(x + 36, z + 44, 36), project(x + 220, z + 44, 36),
                 project(x + 220, z + 44, 220), project(x + 36, z + 44, 220)), eye, fg)
            add((project(x + 88, z + 40, 88), project(x + 168, z + 40, 88),
                 project(x + 168, z + 40, 168), project(x + 88, z + 40, 168)), 0x1082, fg)
    return faces, static_count


def line_spans(faces, y, static_count=None):
    """Same test as walls_line(): each face contributes the span between its
    leftmost and rightmost edge crossing on this scanline. Scenery comes in
    nearest-first and is painted in reverse; mechanisms stay on top."""
    out = []
    n = len(faces) if static_count is None else static_count
    order = list(range(n - 1, -1, -1)) + list(range(n, len(faces)))
    for f in (faces[i] for i in order):
        if y < f['top'] or y >= f['bottom']:
            continue
        left, right = 32767, -32768
        p = f['p']
        for i in range(4):
            a, b = p[i], p[(i + 1) & 3]
            if a[1] > b[1]:
                a, b = b, a
            if y < a[1] or y >= b[1]:
                continue
            hit = a[0] + (y - a[1]) * (b[0] - a[0]) // (b[1] - a[1])
            left = min(left, hit)
            right = max(right, hit)
        left = max(0, left)
        right = min(W, right)
        if left < right:
            out.append((left, right, f['color'], f['fg']))
    return out


def draw_ball(img, lift=0):
    offsets, runs, palette = ball_sprite()
    px = img.load()
    for r in range(BALL_H):
        y = BALL_TOP - lift + r
        if y < 0 or y >= H:
            continue
        x = BALL_LEFT
        for i in range(offsets[r], offsets[r + 1]):
            run = runs[i]
            wdt, col = run >> 3, run & 7
            if col:
                c = rgb565(palette[col])
                for k in range(wdt):
                    if 0 <= x + k < W:
                        px[x + k, y] = c
            x += wdt


def draw_gems(img, level, camera, camera_x, picked=0, look=None):
    px = img.load()
    ball_row = (camera + depth_at(BALL_Y)) >> 8
    first = (camera + depth_at(H - 1)) >> 8
    last = (camera + depth_at(0)) >> 8
    for i, (row, col) in enumerate(level['gems']):
        if i < picked or row <= ball_row or row < first or row > last:
            continue
        d = (row << 8) + 128 - camera
        if d <= 0:
            continue
        py = projected_y(d)
        sc = scale_at(max(0, min(H - 1, py)))
        tile_w = sc >> 8
        hw = max(3, tile_w >> 2)
        hh = max(4, (tile_w * 3) >> 3)
        cx = projected_x(col * 256 + 128 - camera_x, sc)
        for dy in range(-hh, hh + 1):
            y = py + dy
            if not (0 <= y < H):
                continue
            wdt = hw * (hh - abs(dy)) // hh
            if wdt < 2:
                if 0 <= cx < W:
                    px[cx, y] = rgb565(0xf842)
                continue
            edge = 2 if wdt > 6 else 1
            inner = (wdt * 11) >> 5
            for x in range(cx - wdt, cx + wdt + 1):
                if x < 0 or x >= W:
                    continue
                if x < cx - wdt + edge or x >= cx + wdt - edge:
                    c = 0x0233
                elif x < cx - inner:
                    c = 0xc020 if dy < 0 else 0xd000
                elif x <= cx + inner:
                    c = 0xf842 if dy < 0 else 0xc020
                else:
                    c = 0xd000 if dy < 0 else 0x8000
                px[x, y] = rgb565(c)


# ------------------------------------------------------------- backdrop --
# Mirror of the scene backdrop in road.c: a sky ramp, a two-layer horizon
# silhouette built from a 32-entry height profile, and a void colour. Keep
# the numbers identical or the picture stops predicting the panel.
BD_COLS, BD_COL_W, BD_HORIZON, BD_BANDS = (W+7)//8, 8, 100, 8
BD_MOON_X, BD_RADIUS, BD_HOLE_R, BD_HOLE_DX = W-24, 22, 19, -10
BD_PROFILES = {
    'hills': (40, 52, 66, 84, 96, 110, 124, 120, 104, 88, 74, 62, 54, 48, 44, 42,
              46, 58, 72, 86, 98, 92, 80, 70, 62, 56, 52, 48, 44, 42, 40, 38),
    'forest': (96, 120, 140, 150, 144, 128, 112, 98, 86, 78, 72, 68, 66, 70, 80, 92,
               104, 110, 102, 88, 76, 68, 62, 58, 60, 68, 80, 94, 106, 112, 104, 98),
    'massif_gold': (28, 64, 96, 132, 160, 182, 196, 188, 170, 148, 120, 96, 78, 66, 58, 54,
                    60, 80, 108, 140, 168, 186, 178, 156, 130, 104, 84, 70, 58, 46, 36, 30),
    'massif_fantasy': (54, 58, 66, 80, 98, 118, 136, 146, 142, 130, 112, 94, 80, 72, 68, 66,
                       70, 82, 100, 120, 136, 140, 128, 110, 92, 78, 68, 62, 58, 56, 54, 52),
}
BD_BASE = {'hills': 0, 'forest': 1, 'massif_gold': 2, 'massif_fantasy': 3}
BD_PLAIN, BD_DISC, BD_CRESCENT, BD_STARS, BD_AURORA = 0, 1, 2, 3, 4
# Eight looks, two per scene, exactly as backdrops[] in road.c.
BD_MOON_HW = (22, 21, 21, 21, 21, 21, 21, 20, 20, 20, 19, 19, 18, 17, 16, 16,
              15, 13, 12, 11, 9, 6, 0)
BD_HOLE_HW = (19, 18, 18, 18, 18, 18, 18, 17, 17, 16, 16, 15, 14, 13, 12, 11,
              10, 8, 6, 0)
BD_LOOKS = [
    dict(sky=(0x2b7a, 0x43fa, 0x5c7b, 0x751c, 0x8d9c, 0xa61d, 0xbe9e, 0xd71e),
         far=0x3bc9, near=0x2245, ground=0x19a4, flare=0xff72,
         far_amp=44, near_amp=64, flare_y=22, feature=BD_DISC),
    dict(sky=(0x3bfb, 0x547b, 0x6cfc, 0x857c, 0x9ddd, 0xb65e, 0xcede, 0xe75f),
         far=0x442a, near=0x2245, ground=0x19a4, flare=0xfff8,
         far_amp=36, near_amp=56, flare_y=26, feature=BD_DISC),
    dict(sky=(0x0aeb, 0x1b8c, 0x2c0e, 0x3caf, 0x4d31, 0x5dd2, 0x6e54, 0x7ef6),
         far=0x3bcc, near=0x1a47, ground=0x11a5, flare=0xd7ff,
         far_amp=40, near_amp=72, flare_y=28, feature=BD_DISC),
    dict(sky=(0x0947, 0x11a8, 0x1a0a, 0x224c, 0x2aad, 0x3b0f, 0x4371, 0x4bd2),
         far=0x2a4b, near=0x1187, ground=0x1125, flare=0xdfff,
         far_amp=44, near_amp=68, flare_y=24, feature=BD_CRESCENT),
    dict(sky=(0x1845, 0x2066, 0x20a7, 0x28c9, 0x30ea, 0x390b, 0x412c, 0x494d),
         far=0x3909, near=0x20a6, ground=0x1064, flare=0xeedf,
         far_amp=28, near_amp=44, flare_y=24, feature=BD_CRESCENT),
    dict(sky=(0x0823, 0x1043, 0x1064, 0x1865, 0x1886, 0x20a7, 0x20a8, 0x28c9),
         far=0x30e8, near=0x1885, ground=0x1043, flare=0xffff,
         far_amp=30, near_amp=46, flare_y=0, feature=BD_STARS),
    dict(sky=(0x0001, 0x0042, 0x0063, 0x0085, 0x00c6, 0x08e7, 0x0928, 0x0949),
         far=0x2104, near=0x1082, ground=0x0021, flare=0x5dff,
         far_amp=34, near_amp=52, flare_y=30, feature=BD_AURORA),
    dict(sky=(0x0001, 0x0042, 0x0063, 0x0085, 0x08c6, 0x08e7, 0x0928, 0x1149),
         far=0x2104, near=0x1082, ground=0x0021, flare=0x9eff,
         far_amp=34, near_amp=52, flare_y=0, feature=BD_STARS),
]


def bd_sample(prof, phase, amp):
    phase &= 0xFFFFFF
    i = (phase >> 8) & 31
    f = phase & 255
    a, b = prof[i], prof[(i + 1) & 31]
    v = (a * (256 - f) + b * f) >> 8
    drop = (v * amp) >> 8
    return 0 if drop >= BD_HORIZON else BD_HORIZON - drop


def bd_ramp(a, b, f):
    """Linear step between two RGB565 stops; mirrors bd_ramp() in road.c."""
    ar, ag, ab = (a >> 11) & 31, (a >> 5) & 63, a & 31
    dr = (((b >> 11) & 31) - ar) * f >> 4
    dg = (((b >> 5) & 63) - ag) * f >> 4
    db = ((b & 31) - ab) * f >> 4
    return ((ar + dr) << 11) | ((ag + dg) << 5) | (ab + db)


class Backdrop:
    """Same two looks per scene, same feature set, same parallax source."""

    def __init__(self, name, travel, second=0):
        base = BD_BASE[name]
        b = BD_LOOKS[base * 2 + (1 if second else 0)]
        prof = BD_PROFILES[name]
        far_off = (travel >> 3) + 320
        near_off = (travel >> 2) + 1024
        self.far = [bd_sample(prof, far_off + c * 256, b['far_amp']) for c in range(BD_COLS)]
        self.near = [bd_sample(prof, near_off + c * 256, b['near_amp']) for c in range(BD_COLS)]
        self.b = b

    def at(self, x, y):
        b = self.b
        if y >= BD_HORIZON:
            return b['ground']
        if x < 0 or x >= W:
            return b['ground']
        c = x // BD_COL_W
        if self.near[c] <= y:
            return b['near']
        if self.far[c] <= y:
            return b['far']
        band = min(BD_BANDS - 2, y >> 4)
        sky = bd_ramp(b['sky'][band], b['sky'][band + 1], y & 15)
        feat = b['feature']
        if feat == BD_AURORA and b['flare_y'] <= y < b['flare_y'] + 8:
            sky = bd_ramp(b['flare'], sky, (y - b['flare_y']) * 2)
        if feat in (BD_DISC, BD_CRESCENT):
            dy = abs(y - b['flare_y'])
            if dy < BD_RADIUS:
                hw = BD_MOON_HW[dy]
                if abs(x - BD_MOON_X) < hw:
                    if feat == BD_DISC:
                        return b['flare']
                    dy2 = abs(y - b['flare_y'] + 2)
                    hw2 = BD_HOLE_HW[dy2] if dy2 < BD_HOLE_R else 0
                    if abs(x - BD_MOON_X - BD_HOLE_DX) >= hw2:
                        return b['flare']
        if feat == BD_STARS and y < 64:
            h = (y * 2654435761) & 0xFFFFFFFF
            if not ((h >> 9) & 3):
                sx = 6 + ((h >> 17) % (W-12))
                if sx <= x < sx + 2:
                    return b['flare']
        return sky


DIGITS = [(7, 5, 5, 5, 7), (2, 6, 2, 2, 7), (7, 1, 7, 4, 7), (7, 1, 7, 1, 7),
          (5, 5, 7, 1, 1), (7, 4, 7, 1, 7), (7, 4, 7, 5, 7), (7, 1, 1, 1, 1),
          (7, 5, 7, 5, 7), (7, 5, 7, 1, 7)]


def glyph(px, x, y, rows, on, off):
    for r in range(5):
        bits = rows[r]
        for c in range(3):
            col = on if (bits >> (2 - c)) & 1 else off
            for dy in range(2):
                for dx in range(2):
                    if 0 <= x + c * 2 + dx < W and 0 <= y + r * 2 + dy < H:
                        px[x + c * 2 + dx, y + r * 2 + dy] = rgb565(col)


def draw_hud(img, level, camera, gems, volume=None):
    """hud_overlay(): a 2 px progress bar plus the gem counter. There is no
    percentage readout during play -- that pill only exists on the menu.
    `volume` mirrors hud_volume_bars(): ten 4 px steps at the far end of the
    HUD band, drawn only for the 1.2 s after a key changes the level."""
    px = img.load()
    total = level['rows'] * 256
    z = camera + depth_at(BALL_Y)
    pct = min(100, z * 100 // total)
    done = W * pct // 100
    for y in range(2):
        for x in range(W):
            px[x, y] = rgb565(0xffe0 if x < done else 0x18e3)
    for x in range(3, 31):
        for y in range(4, 20):
            px[x, y] = rgb565(0x0000)
    widths = (1, 3, 5, 3, 1)
    for r, wd in enumerate(widths):
        for x in range(6 + (5 - wd) // 2, 6 + (5 - wd) // 2 + wd):
            for dy in range(2):
                px[x, 5 + r * 2 + dy] = rgb565(0x07ff)
    tens = gems // 10
    glyph(px, 16, 5, DIGITS[tens], 0xffff, 0x0000)
    glyph(px, 23, 5, DIGITS[gems - tens * 10], 0xffff, 0x0000)
    if volume is not None:
        lit = 0x07e0 if volume > 6 else 0xffe0 if volume > 3 else 0xf800
        for y in range(8, 14):
            for x in range(196, 235):
                px[x, y] = rgb565(0x0000)      # opaque track: the sun sits here
            for x in range(196, 196 + max(0, volume * 4 - 1)):
                px[x, y] = rgb565(lit)


def render(level_id, ball_row, camera_x=640, gems=0, limit=32, out=None, volume=None):
    levels = load_levels()
    level = [l for l in levels if l['id'] == level_id][0]
    app = json.load(open(ROOT / 'levels/appearance.json', encoding='utf-8'))
    look = load_look(level_id, ball_row)
    camera = ball_row * 256 - depth_at(BALL_Y)
    # Parallax follows forward travel, not the ball's lateral position: the
    # firmware change that stopped the skyline shivering with the slider.
    # Second look of the scene starts at the middle of the stretch, same rule
    # as scene_second() in road.c.
    span = cue_span(level_id, ball_row)
    second = 1 if span and span[1] - span[0] >= 48 and ball_row * 2 >= sum(span) else 0
    bd = Backdrop(look['name'], camera, second)
    # Distant static bodies are tinted with their own front material, read
    # from the same model walls.c builds geometry from (body_color in road.c).
    _models = app['models']
    _mi = look['solid_model']
    body_color = (look[_models[_mi]['boxes'][0]['colors'][0]]
                  if _mi < len(_models) else look['solid_front'])
    depth_ball = depth_at(BALL_Y)
    z_ball = camera + depth_ball
    all_faces, _ = build_faces(level, look, app['models'], camera, camera_x,
                               depth_ball, z_ball, budget=False)
    faces, static_count = build_faces(level, look, app['models'], camera, camera_x,
                                      depth_ball, z_ball, budget=limit)
    img = Image.new('RGB', (W, H), rgb565(bd.b['ground']))
    px = img.load()
    for yy in range(H):
        for xx in range(W):
            px[xx, yy] = rgb565(bd.at(xx, yy))
    previous = None
    mask = 31
    pads = shorts = glass = bodies = barrier = warn = 0
    # Finishing gate: a ring portal, same geometry as gate_prepare() in road.c.
    gate_y = gate_top = gate_cy = gate_cx = None
    gate_r = gate_rc = 0
    gd = ((level['rows'] - 1) * 256 + 128) - camera
    if gd > 0:
        gpy = projected_y(gd)
        if -96 <= gpy < H + 96:
            gsc = scale_at(max(0, min(H - 1, gpy)))
            gtw = gsc >> 8
            gate_y = gpy
            gate_r = gtw + (gtw >> 1)
            gate_rc = gate_r - (gate_r >> 1) + (gate_r >> 4)
            gate_cy = gpy - gate_r + (gate_r >> 2)
            gate_cx = (projected_x(-camera_x, gsc) + projected_x(5 * 256 - camera_x, gsc)) // 2
            gate_top = gate_cy - gate_r
    for y in range(H):
        d = depth_at(y)
        sc = scale_at(y)
        row = (camera + d) >> 8
        if row != previous:
            mask = ground_mask(level, row)
            pads = tile_mask(level, row, 1)
            shorts = tile_mask(level, row, 6)
            glass = tile_mask(level, row, 3)
            # Cells a blocker stands on. Static bodies take the colour of the
            # object standing there (they are simply part of the level); only
            # the risers and the mechanisms, which really do emerge, keep the
            # sunken plate. See body_mask()/warn_mask() in road.c.
            bodies = tile_mask(level, row, 4) | tile_mask(level, row, 5)
            barrier = tile_mask(level, row, 2)
            warn = tile_mask(level, row, 7)
            for m in level.get('mechs', []):
                if m[0] != row:
                    continue
                lane = m[1] - 1
                warn |= 1 << lane
                if m[2] == 'roller':
                    if lane:
                        warn |= 1 << (lane - 1)
                    if lane < 4:
                        warn |= 1 << (lane + 1)
        horizontal = row != previous
        x = projected_x(-camera_x, sc)
        for col in range(5):
            right = projected_x((col + 1) * 256 - camera_x, sc)
            c = look['light']
            if pads & (1 << col):
                c = look['pad']
            if shorts & (1 << col):
                c = look['short_pad']
            if glass & (1 << col):
                c = look['fragile']
            if bodies & (1 << col):
                c = body_color
            if barrier & (1 << col):
                c = look['barrier_front']
            if warn & (1 << col):
                c = look['dark']
            g = look['fragile_edge'] if glass & (1 << col) else look['grid']
            x0, x1 = max(0, x), min(W, right)
            if x1 > x0:
                if not (mask & (1 << col)):
                    for xx in range(x0, x1):
                        px[xx, y] = rgb565(bd.at(xx, y))
                elif horizontal:
                    gc = rgb565(g)
                    for xx in range(x0, x1):
                        px[xx, y] = gc
                else:
                    px[x0, y] = rgb565(g)
                    cc = rgb565(c)
                    for xx in range(x0 + 1, x1):
                        px[xx, y] = cc
            x = right
        for left, right, color, fg in line_spans(faces, y, static_count):
            if fg:
                continue
            cc = rgb565(color)
            for xx in range(left, right):
                px[xx, y] = cc
        if gate_y is not None and gate_top <= y <= gate_y:
            # Ring portal: orange rim, one thin yellow band, cyan disc inside.
            dy = abs(y - gate_cy)
            hw = isqrt(max(0, gate_r * gate_r - dy * dy))
            hc = isqrt(max(0, gate_rc * gate_rc - dy * dy))
            ring = max(1, hw - hc)
            for xx in range(max(0, gate_cx - hw), min(W, gate_cx + hw + 1)):
                ax = abs(xx - gate_cx)
                if ax > hw:
                    continue
                if ax <= hc:
                    if ax * 4 < hc:
                        c = 0xdfff
                    elif ax * 2 < hc:
                        c = 0x7fbf
                    elif ax * 4 < hc * 3:
                        c = 0x467e
                    else:
                        c = 0x14b9
                else:
                    t = ax - hc
                    if ax > hw - 2:
                        c = 0x8a83
                    elif t * 8 < ring:
                        c = 0xf707
                    elif t * 20 < ring * 11:
                        c = 0xe509
                    else:
                        c = 0xb3a5
                px[xx, y] = rgb565(c)
        previous = row
    draw_gems(img, level, camera, camera_x, picked=0, look=look)
    draw_ball(img)
    for y in range(H):
        for left, right, color, fg in line_spans(faces, y, static_count):
            if not fg:
                continue
            cc = rgb565(color)
            for xx in range(left, right):
                px[xx, y] = cc
    draw_hud(img, level, camera, gems, volume)
    out = out or ROOT / '.workbuddy/preview' / f'L{level_id}_r{ball_row}_x{camera_x}.png'
    out.parent.mkdir(parents=True, exist_ok=True)
    img.resize((W * 2, H * 2), Image.NEAREST).save(out)
    return out, len(faces), len(all_faces)


if __name__ == '__main__':
    args = sys.argv[1:]
    # Optional trailing `--volume N` renders one extra frame with the HUD
    # volume bar up, for eyeballing that band without a board:
    #   python tools/preview_frame.py 5 40 --volume 7
    volume = None
    if '--volume' in args:
        at = args.index('--volume')
        volume = int(args[at + 1])
        del args[at:at + 2]
    if not args:
        jobs = [(1, 20), (4, 80), (5, 40), (5, 260), (5, 430)]
        for lid, row in jobs:
            print(*render(lid, row))
    else:
        lid = int(args[0])
        rows = [int(a) for a in args[1:]] or [40]
        for r in rows:
            print(*render(lid, r))
        if volume is not None:
            print(*render(lid, rows[0], volume=volume,
                          out=ROOT / '.workbuddy/preview' / 'volume_bar.png'))
