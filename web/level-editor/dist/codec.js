(function (root) {
  "use strict";
  const kinds = { laser: 1, mover: 2, hammer: 3, roller: 4, fly: 5 };
  function check(ok, message) {
    if (!ok) throw new Error(message);
  }
  function integer(value, min, max, label) {
    check(
      Number.isInteger(value) && value >= min && value <= max,
      `${label}应为 ${min}～${max} 的整数`,
    );
    return value;
  }
  function create(id, name, rows = 128) {
    integer(rows, 16, 65535, "长度");
    const data = new Uint8Array(rows * 8);
    for (let r = 0; r < rows; r++) data[r * 8] = 31;
    return { id, name, rows, data, gems: [], mechs: [] };
  }
  function fromJSON(source) {
    check(source && Array.isArray(source.levels), "JSON 缺少 levels 数组");
    const levels = source.levels.map((item, index) => {
      integer(item.rows, 16, 65535, "关卡长度");
      const level = create(
        item.id ?? index + 1,
        String(item.name ?? `关卡 ${index + 1}`).slice(0, 80),
        item.rows,
      );
      let end = 0;
      for (const [row, count, mask] of item.ground ?? []) {
        integer(row, end, level.rows - 1, "地面起点");
        integer(count, 1, level.rows - row, "地面长度");
        integer(mask, 0, 31, "地面掩码");
        for (let r = row; r < row + count; r++) level.data[r * 8] = mask;
        end = row + count;
      }
      for (const [row, mask, kind] of item.tiles ?? []) {
        integer(row, 0, level.rows - 1, "物件行号");
        integer(mask, 1, 31, "物件掩码");
        integer(kind, 1, 7, "物件类型");
        level.data[row * 8 + kind] |= mask;
      }
      level.gems = (item.gems ?? []).map((g) => [...g]);
      level.mechs = (item.mechs ?? []).map((m) => [
        m[0],
        m[1],
        typeof m[2] === "string" ? kinds[m[2]] : m[2],
        ...m.slice(3),
      ]);
      return level;
    });
    validate(levels, { wallSpacing: false });
    return levels;
  }
  function toJSON(levels) {
    return {
      levels: levels.map((l) => {
        const ground = [],
          tiles = [];
        for (let r = 0; r < l.rows; ) {
          const mask = l.data[r * 8],
            start = r++;
          while (r < l.rows && l.data[r * 8] === mask) r++;
          if (mask !== 31) ground.push([start, r - start, mask]);
        }
        for (let r = 0; r < l.rows; r++)
          for (let k = 1; k < 8; k++)
            if (l.data[r * 8 + k]) tiles.push([r, l.data[r * 8 + k], k]);
        return {
          id: l.id,
          name: l.name,
          rows: l.rows,
          ground,
          tiles,
          gems: l.gems.map((g) => [...g]),
          mechs: l.mechs.map((m) => [...m]),
        };
      }),
    };
  }
  function validate(levels, { wallSpacing = true } = {}) {
    integer(levels.length, 1, 24, "关卡数量");
    const ids = new Set(),
      warnings = [];
    for (const l of levels) {
      integer(l.id, 1, 65535, "关卡 ID");
      check(!ids.has(l.id), `关卡 ID ${l.id} 重复`);
      ids.add(l.id);
      integer(l.rows, 16, 65535, "关卡长度");
      check(l.data.length === l.rows * 8, "地块数据长度错误");
      const costs = new Uint16Array(l.rows),
        walls = [];
      const pop = (n) => {
        let count = 0;
        for (; n; n &= n - 1) count++;
        return count;
      };
      for (let r = 0; r < l.rows; r++) {
        const d = l.data.subarray(r * 8, r * 8 + 8),
          where = `ID ${l.id} 第 ${r} 行`;
        for (const mask of d) integer(mask, 0, 31, where);
        for (let k = 1; k < 8; k++)
          if (k !== 5) check(!(d[k] & ~d[0]), `${where} 物件缺少地板`);
        check(
          !(d[3] & (d[1] | d[2] | d[4] | d[5] | d[6] | d[7])),
          `${where} 玻璃与物件重叠`,
        );
        check(!(d[1] & d[6]), `${where} 两种跳板重叠`);
        if (d[2]) walls.push(r);
        costs[r] =
          pop(d[2]) * 3 +
          pop(d[4]) * 6 +
          pop(d[5] & ~(d[5] << 1) & 31) * 3 +
          pop(d[7]) * 4;
      }
      if (wallSpacing)
        for (let i = 1; i < walls.length; i++)
          check(
            walls[i] - walls[i - 1] >= 16,
            `ID ${l.id} 低墙第 ${walls[i - 1]} / ${walls[i]} 行间距应至少 16 行`,
          );
      integer(l.gems.length, 0, 32, "钻石数量");
      integer(l.mechs.length, 0, 32, "机关数量");
      const positions = (list) => {
        const seen = new Set();
        for (const entry of list) {
          integer(entry[0], 0, l.rows - 1, "行号");
          integer(entry[1], 1, 5, "列号");
          const key = entry[0] * 5 + entry[1];
          check(!seen.has(key), `ID ${l.id} 同格物件重复`);
          seen.add(key);
        }
      };
      positions(l.gems);
      positions(l.mechs);
      for (const [r, lane] of l.gems)
        check(
          !(
            (l.data[r * 8 + 2] | l.data[r * 8 + 4] | l.data[r * 8 + 7]) &
            (1 << (lane - 1))
          ),
          `ID ${l.id} 第 ${r} 行钻石位于障碍内部`,
        );
      for (const [r, lane, kind, period, phase, param] of l.mechs) {
        integer(kind, 1, 5, "机关类型");
        check([1024, 2048, 4096, 8192].includes(period), "机关周期无效");
        integer(phase, 0, period - 1, "机关相位");
        integer(
          param,
          kind === 3 ? 256 : 1,
          { 1: 640, 3: 680, 5: 900 }[kind] ?? 512,
          "机关参数",
        );
        const bit = 1 << (lane - 1);
        check(l.data[r * 8] & bit, `ID ${l.id} 第 ${r} 行机关缺少地板`);
        for (let k = 1; k < 8; k++)
          check(
            !(l.data[r * 8 + k] & bit),
            `ID ${l.id} 第 ${r} 行机关与物件重叠`,
          );
        costs[r] += [0, 6, 3, 6, 9, 4][kind];
      }
      let worst = 0;
      for (let r = 0; r < l.rows; r++)
        worst = Math.max(
          worst,
          costs.subarray(r, r + 4).reduce((a, b) => a + b, 0),
        );
      if (worst > 44)
        warnings.push(
          `ID ${l.id} 障碍密集，最密 4 行需 ${worst} 个面，远处部分物件可能省略绘制。`,
        );
    }
    return warnings;
  }
  function encode(levels) {
    validate(levels);
    const sorted = (list) =>
      [...list].sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    const header = new Uint8Array(512),
      view = new DataView(header.buffer),
      chunks = [header];
    header.set(new TextEncoder().encode("RSLEVEL4"));
    view.setUint16(8, levels.length, true);
    view.setUint16(10, 8, true);
    let sector = 1;
    function append(data) {
      const padded = new Uint8Array(Math.ceil(data.length / 512) * 512);
      padded.set(data);
      chunks.push(padded);
      const start = sector;
      sector += padded.length / 512;
      return start;
    }
    levels.forEach((l, i) => {
      view.setUint32(16 + i * 8, l.rows, true);
      view.setUint32(20 + i * 8, append(l.data), true);
      view.setUint16(208 + i * 2, l.id, true);
      for (const [entries, size, offset] of [
        [l.gems, 3, 256],
        [l.mechs, 9, 260],
      ]) {
        if (!entries.length) continue;
        const bytes = new Uint8Array(2 + entries.length * size),
          v = new DataView(bytes.buffer);
        v.setUint16(0, entries.length, true);
        sorted(entries).forEach((e, index) => {
          const p = 2 + index * size;
          v.setUint16(p, e[0], true);
          v.setUint8(p + 2, e[1] - 1);
          if (size === 9) {
            v.setUint8(p + 3, e[2]);
            v.setUint8(p + 4, Math.log2(e[3] / 256));
            v.setUint16(p + 5, e[4], true);
            v.setUint16(p + 7, e[5], true);
          }
        });
        view.setUint32(offset + i * 8, append(bytes), true);
      }
    });
    const result = new Uint8Array(sector * 512);
    let offset = 0;
    for (const chunk of chunks) {
      result.set(chunk, offset);
      offset += chunk.length;
    }
    return result;
  }
  function decode(input) {
    const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
    check(
      bytes.length >= 512 &&
        bytes.length <= 16 * 1024 * 1024 &&
        bytes.length % 512 === 0,
      "关卡包长度无效",
    );
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength),
      magic = new TextDecoder().decode(bytes.subarray(0, 8));
    check(
      magic === "RSLEVEL4",
      "请导入 RSLEVEL4 格式关卡包，旧版可先使用 JSON 源文件",
    );
    const count = integer(v.getUint16(8, true), 1, 24, "关卡数量");
    check(v.getUint16(10, true) === 8, "关卡行宽错误");
    const used = new Set([0]);
    function region(sector, length) {
      integer(sector, 1, bytes.length / 512 - 1, "数据扇区");
      check(sector * 512 + length <= bytes.length, "关卡包数据越界");
      for (let s = sector; s < sector + Math.ceil(length / 512); s++) {
        check(!used.has(s), "关卡包数据区重叠");
        used.add(s);
      }
      return sector * 512;
    }
    const levels = [];
    for (let i = 0; i < count; i++) {
      const rows = integer(
          v.getUint32(16 + i * 8, true),
          16,
          65535,
          "关卡长度",
        ),
        id = v.getUint16(208 + i * 2, true);
      const level = create(id, `关卡 ${id}`, rows),
        start = region(v.getUint32(20 + i * 8, true), rows * 8);
      level.data.set(bytes.subarray(start, start + rows * 8));
      for (const [size, offset, target] of [
        [3, 256, level.gems],
        [9, 260, level.mechs],
      ]) {
        const sector = v.getUint32(offset + i * 8, true);
        if (!sector) continue;
        check(sector * 512 + 2 <= bytes.length, "稀疏表越界");
        const n = integer(v.getUint16(sector * 512, true), 1, 32, "稀疏表数量"),
          p = region(sector, 2 + n * size);
        for (let j = 0; j < n; j++) {
          const at = p + 2 + j * size,
            e = [v.getUint16(at, true), v.getUint8(at + 2) + 1];
          if (size === 9) {
            const shift = integer(v.getUint8(at + 4), 2, 5, "机关周期位移");
            e.push(
              v.getUint8(at + 3),
              256 * 2 ** shift,
              v.getUint16(at + 5, true),
              v.getUint16(at + 7, true),
            );
          }
          target.push(e);
        }
      }
      levels.push(level);
    }
    validate(levels);
    return levels;
  }
  const api = { create, fromJSON, toJSON, validate, encode, decode };
  if (typeof module !== "undefined") module.exports = api;
  else root.RSCodec = api;
})(typeof window === "undefined" ? globalThis : window);
