/* Collision model from firmware/level.c, road.c and obstacle.c.
 * Coordinates are Q8. Logical jump height is independent of sprite animation. */
((root) => {
  "use strict";
  const left = 128, right = 1152, center = 640, radius = 24;
  const spawn = Math.floor(((39104 - 20 * 384) * 256) / (2496 + 12 * 384));
  const normal = 1366, short = 768;
  const sine = [0, 98, 181, 237, 256, 237, 181, 98, 0, -98, -181, -237, -256, -237, -181, -98];
  const staticRules = [
    [2, 24, 40, 0, 680], [4, 48, 208, 0, 1024],
    [5, 64, 192, 600, 1024], [7, 48, 208, 0, 680],
  ];
  const distance = (p, lo, hi) => Math.max(lo - p, p - hi, 0);

  function mechanismBox([row, lane, kind, period, phase, param], z) {
    const base = row * 256, x = (lane - 1) * 256 + 128;
    if (base > z + 1280 || base + 640 < z) return null;
    const p = (z - base + phase) & (period - 1);
    const sample = sine[Math.floor(p * 16 / period)];
    const sweep = Math.floor((Math.min(p, period - p) - period / 4) * param * 4 / period);
    const a = base + 48, b = base + 208;
    switch (kind) {
      case 1: return [x - 80, x + 80, base - 1152, b, 0, 256];
      case 2: return [x + sweep - 80, x + sweep + 80, a, b, 0, 256];
      case 3: {
        const s = (sample * 196) >> 8;
        const cx = center + ((s * 1100) >> 8);
        const h = 1256 - (((256 - ((s * s) >> 9)) * 1100) >> 8);
        return [cx - 112, cx + 112, a, b, Math.max(0, h - 160), h + 160];
      }
      case 4: return [x + sweep - 256, x + sweep + 256, a, b, 0, 280];
      case 5: {
        const h = (450 + ((sample * param) >> 8)) >>> 0;
        return [x - 96, x + 96, a, b, (h > 88 ? h - 88 : 0) & 65535, (h + 88) & 65535];
      }
      default: return null;
    }
  }

  function merge(intervals, stride) {
    intervals.sort((a, b) => a[0] - b[0]);
    const result = [];
    for (const [lo, hi] of intervals) {
      const last = result[result.length - 1];
      if (last && lo <= last[1] + stride) last[1] = Math.max(last[1], hi);
      else result.push([lo, hi]);
    }
    return result;
  }

  function buildCourse(level) {
    const { data, rows, mechs } = level;
    const staticBoxes = new Map(), dynamic = new Map(), interesting = new Uint8Array(rows);
    for (let r = 0; r < rows; r++) {
      const p = r * 8;
      if (data[p] !== 31 || data[p + 1] || data[p + 6]) interesting[r] = 1;
      for (const [kind, start, end, low, high] of staticRules) {
        const mask = data[p + kind];
        if (!mask) continue;
        const boxes = staticBoxes.get(r) ?? [];
        for (let lane = 0; lane < 5; lane++) if (mask & (1 << lane))
          boxes.push([lane * 256 + 48, lane * 256 + 208, r * 256 + start, r * 256 + end, low, high]);
        staticBoxes.set(r, boxes);
      }
    }
    for (const r of staticBoxes.keys())
      interesting.fill(1, Math.max(0, r - 1), Math.min(rows, r + 2));
    for (const m of mechs) {
      for (let r = Math.max(0, m[0] - 5); r <= Math.min(rows - 1, m[0] + 2); r++) {
        interesting[r] = 1;
        if (!dynamic.has(r)) dynamic.set(r, []);
        dynamic.get(r).push(m);
      }
    }
    const nextInteresting = new Uint32Array(rows + 1);
    nextInteresting[rows] = rows;
    for (let r = rows - 1; r >= 0; r--)
      nextInteresting[r] = interesting[r] ? r : nextInteresting[r + 1];
    return { data, rows, staticBoxes, dynamic, nextInteresting };
  }

  function collisionSlices(course, z, row) {
    const slices = [];
    function add(box) {
      const dz = distance(z, box[2], box[3]);
    if (dz <= radius) slices.push([box[0], box[1], box[4], box[5], radius * radius - dz * dz]);
    }
    for (let r = (z - radius) >> 8; r <= (z + radius) >> 8; r++)
      for (const box of course.staticBoxes.get(r) ?? []) add(box);
    for (const m of course.dynamic.get(row) ?? []) {
      const box = mechanismBox(m, z);
      if (box && box[4] > 2048) {
        // A negative fly-hammer height is cast to unsigned in the firmware.
        // Its squared distance can then overflow signed C arithmetic.
        slices.uncertain = true;
      } else if (box) add(box);
    }
    return slices;
  }

  function removeHits(intervals, slices, height, stride) {
    for (const [lo, hi, bottom, top, radiusSquared] of slices) {
      const dy = distance(height + radius, bottom, top);
      if (dy * dy > radiusSquared) continue;
      const dx = Math.floor(Math.sqrt(radiusSquared - dy * dy));
      const before = Math.floor((lo - dx - 1) / stride) * stride;
      const after = Math.ceil((hi + dx + 1) / stride) * stride;
      const safe = [];
      for (const [a, b] of intervals) {
        if (a <= before) safe.push([a, Math.min(b, before)]);
        if (b >= after) safe.push([Math.max(a, after), b]);
      }
      intervals = safe;
      if (!intervals.length) break;
    }
    return intervals;
  }

  function addState(states, flight, lo, hi) {
    if (!states.has(flight)) states.set(flight, []);
    states.get(flight).push([lo, hi]);
  }

  // The signed flight counter identifies both duration and remaining time.
  // A flight always advances more than one row, so the previous pad row
  // cannot be retriggered on landing.
  function step(states, direction, relaxed, data, row, slices, initial) {
    const next = new Map(), stride = relaxed ? 1 : 2, offset = row * 8;
    const clamp = (x) => Math.max(left, Math.min(right, x));
    for (const [flight, intervals] of states) {
      const remaining = Math.max(0, Math.abs(flight) - (initial ? 0 : 1));
      const height = (remaining * ((flight < 0 ? short : normal) - remaining) * (flight < 0 ? 12 : 7)) >> 12;
      const moved = relaxed && !initial ? [[left, right]]
        : intervals.map(([lo, hi]) => [clamp(lo + direction), clamp(hi + direction)]);
      for (const [lo, hi] of removeHits(moved, slices, height, stride)) {
        if (remaining) {
          addState(next, flight < 0 ? -remaining : remaining, lo, hi);
          continue;
        }
        for (let lane = lo >> 8; lane <= hi >> 8; lane++) {
          const bit = 1 << lane;
          if (!(data[offset] & bit)) continue;
          const pad = data[offset + 6] & bit ? -short : data[offset + 1] & bit ? normal : 0;
          addState(next, pad, Math.max(lo, lane * 256), Math.min(hi, (lane + 1) * 256 - stride));
        }
      }
    }
    for (const [flight, intervals] of next) next.set(flight, merge(intervals, stride));
    return next;
  }

  function* search(course, relaxed, maxWork, flightLimit = Infinity) {
    const { data, rows, nextInteresting } = course;
    const frameDurations = [33, 33, 34];
    let t = 0, frame = 0, frameEnd = 0, work = 0;
    // Realistic pass holds one key input per frame. Relaxed pass can place
    // the ball at any lateral coordinate each tick, supersetting ADC shifts.
    let branches = [new Map([[0, [[center, center]]]])];
    while (true) {
      const z = spawn + Math.floor(t * 3 / 4), row = z >> 8;
      if (row >= rows) return { status: "pass", row: rows - 1 };
      const slices = collisionSlices(course, z, row);
      if (slices.uncertain && !relaxed) return { status: "unknown", row };
      branches = branches.map((states, i) =>
        step(states, t && !relaxed ? (i - 1) * 2 : 0, relaxed, data, row, slices, t === 0));
      if (branches.every((states) => states.size === 0)) return { status: "blocked", row };
      if (nextInteresting[row] === rows && branches.some((states) => states.has(0)))
        return { status: "pass", row: rows - 1 };

      if (!relaxed && t === frameEnd) {
        const combined = new Map();
        for (const states of branches)
          for (const [flight, intervals] of states)
            for (const [lo, hi] of intervals) addState(combined, flight, lo, hi);
        for (const [flight, intervals] of combined) combined.set(flight, merge(intervals, 2));
        // The first pass samples take-off times to find a witness quickly.
        // Dropping states here can only hide routes, never invent one.
        if (combined.size > flightLimit)
          for (const sign of [-1, 1]) {
            const flights = [...combined.keys()].filter((k) => k * sign > 0).sort((a, b) => a - b);
            if (flights.length <= flightLimit) continue;
            const keep = new Set(Array.from({ length: flightLimit }, (_, i) =>
              flights[Math.round(i * (flights.length - 1) / (flightLimit - 1))]));
            for (const k of flights) if (!keep.has(k)) combined.delete(k);
          }
        branches = [combined, combined, combined];
        // A full, grounded lateral range remains full at frame boundaries
        // throughout an unobstructed stretch.
        const ground = combined.get(0);
        if (combined.size === 1 && ground?.length === 1 && ground[0][0] === left && ground[0][1] === right) {
          const until = Math.ceil((nextInteresting[row] * 256 - spawn) * 4 / 3);
          t += Math.max(0, Math.floor((until - t - 1) / 100)) * 100;
        }
        frameEnd = t + frameDurations[frame++ % 3];
      } else if (relaxed && branches[0].size === 1 && branches[0].has(0)) {
        t = Math.max(t, Math.ceil((nextInteresting[row] * 256 - spawn) * 4 / 3) - 1);
      }
      work += 1 + branches.reduce((sum, states) => sum + states.size, 0);
      if (work > maxWork) return { status: "unknown", row, limited: true };
      if ((t & 63) === 0) yield row;
      t++;
    }
  }

  function* analyze(level, maxWork = 1500000) {
    const course = buildCourse(level);
    const quick = yield* search(course, false, maxWork * 4, 12);
    if (quick.status === "pass") return quick;
    const keyboard = yield* search(course, false, maxWork);
    if (keyboard.status === "pass") return keyboard;
    const relaxed = yield* search(course, true, maxWork);
    if (relaxed.status === "blocked") return relaxed;
    return {
      status: "unknown",
      row: keyboard.row,
      limited: keyboard.limited || relaxed.limited,
      uncertainCollision: keyboard.status === "unknown" && !keyboard.limited,
    };
  }
  if (typeof module !== "undefined") module.exports = { analyze };
  else root.RSReachability = { analyze };
})(typeof window === "undefined" ? globalThis : window);
