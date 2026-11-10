"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const os = require("node:os");
const { spawnSync } = require("node:child_process");
const C = require("../dist/codec.js");
const repo = path.resolve(__dirname, "../../..");
const seed = C.fromJSON(
  JSON.parse(fs.readFileSync(path.join(repo, "levels/levels.json"), "utf8")),
);
const original = fs.readFileSync(path.join(repo, "sd_payload/RSLEVELS.BIN"));
assert.deepEqual(
  Buffer.from(C.encode(seed)),
  original,
  "Existing card pack must remain byte-identical",
);
assert.deepEqual(
  Buffer.from(C.encode(C.decode(original))),
  original,
  "BIN roundtrip",
);
assert.deepEqual(
  Buffer.from(C.encode(C.fromJSON(C.toJSON(seed)))),
  original,
  "JSON roundtrip",
);
let checks = 3;
function rejects(change, pattern) {
  const l = C.create(99, "Invalid", 128);
  change(l);
  assert.throws(() => C.encode([l]), pattern);
  checks++;
}
rejects((l) => (l.id = 0), /ID/);
rejects((l) => (l.rows = 15), /长度/);
rejects((l) => (l.data[0] = 32), /整数/);
rejects((l) => {
  l.data[0] = 0;
  l.data[1] = 1;
}, /地板/);
rejects((l) => {
  l.data[3] = 1;
  l.data[4] = 1;
}, /玻璃/);
rejects((l) => {
  l.data[1] = 1;
  l.data[6] = 1;
}, /跳板/);
rejects((l) => {
  l.data[2] = 1;
  l.data[8 + 2] = 2;
}, /间距/);
rejects((l) => {
  l.gems = [
    [0, 1],
    [0, 1],
  ];
}, /重复/);
rejects((l) => {
  l.gems = Array.from({ length: 33 }, (_, i) => [i, 1]);
}, /数量/);
rejects((l) => {
  l.gems = [[0, 1]];
  l.data[4] = 1;
}, /障碍/);
rejects((l) => (l.mechs = [[0, 1, 1, 512, 0, 10]]), /周期/);
rejects((l) => (l.mechs = [[0, 1, 3, 1024, 0, 255]]), /参数/);
rejects((l) => (l.mechs = [[0, 1, 3, 1024, 1024, 300]]), /相位/);
rejects((l) => {
  l.mechs = [[0, 1, 1, 1024, 0, 10]];
  l.data[1] = 1;
}, /重叠/);
assert.throws(() => C.encode([C.create(1, "A"), C.create(1, "B")]), /重复/);
checks++;
assert.throws(() => C.fromJSON({ levels: [{ name: "Missing rows" }] }), /长度/);
checks++;
const incomplete = C.create(1, "In progress");
incomplete.data[2] = 1;
incomplete.data[10] = 1;
assert.equal(
  C.fromJSON(C.toJSON([incomplete]))[0].data[10],
  1,
  "Unfinished wall spacing remains editable in JSON",
);
checks++;
for (let size of [0, 511, 513]) {
  assert.throws(() => C.decode(new Uint8Array(size)));
  checks++;
}
for (const mutate of [
  (b) => (b[0] = 0),
  (b) => new DataView(b.buffer).setUint32(20, 0, true),
  (b) => new DataView(b.buffer).setUint32(20, 0xffffffff, true),
  (b) => new DataView(b.buffer).setUint32(28, 1, true),
  (b) => new DataView(b.buffer).setUint16(8, 25, true),
  (b) => new DataView(b.buffer).setUint32(256, 0xffffffff, true),
]) {
  const bytes = new Uint8Array(original);
  mutate(bytes);
  assert.throws(() => C.decode(bytes));
  checks++;
}
const max = Array.from({ length: 24 }, (_, i) =>
  C.create(i === 23 ? 65535 : i + 1, "Max", i === 23 ? 65535 : 16),
);
assert.equal(C.decode(C.encode(max))[23].rows, 65535);
checks++;
const rich = C.create(65535, "All objects", 256);
for (let kind = 1; kind <= 7; kind++)
  rich.data[kind * 20 * 8 + kind] = 1 << kind % 5;
rich.gems = Array.from({ length: 32 }, (_, i) => [160 + i, (i % 5) + 1]);
rich.mechs = Array.from({ length: 32 }, (_, i) => [
  200 + i,
  (i % 5) + 1,
  (i % 5) + 1,
  [1024, 2048, 4096, 8192][i % 4],
  i * 3,
  i % 5 === 2 ? 256 : 1,
]);
const richBytes = C.encode([...seed, rich]);
assert.deepEqual(
  Buffer.from(C.encode(C.decode(richBytes))),
  Buffer.from(richBytes),
);
checks++;
let rng = 0x5124;
const random = () => {
  rng = (Math.imul(rng, 1664525) + 1013904223) >>> 0;
  return rng;
};
const randomized = C.create(42, "Randomized", 1024);
for (let r = 0; r < randomized.rows; r++) {
  randomized.data[r * 8] = random() % 32;
  for (let lane = 0; lane < 5; lane++) {
    const kind = [0, 1, 3, 4, 5, 6, 7][random() % 7];
    if (kind && (kind === 5 || randomized.data[r * 8] & (1 << lane)))
      randomized.data[r * 8 + kind] |= 1 << lane;
  }
}
const randomBytes = C.encode([randomized]);
assert.deepEqual(
  Buffer.from(C.encode(C.fromJSON(C.toJSON([randomized])))),
  Buffer.from(randomBytes),
);
checks++;
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "rs-editor-parity-"));
try {
  for (const [name, levels] of [
    ["seed", seed],
    ["rich", [...seed, rich]],
    ["random", [randomized]],
    ["maximum", max],
  ]) {
    const source = path.join(temporary, `${name}.json`),
      output = path.join(temporary, `${name}.bin`);
    fs.writeFileSync(source, JSON.stringify(C.toJSON(levels)));
    const result = spawnSync(
      process.env.PYTHON || "python",
      [path.join(repo, "tools/make_sd_levels.py"), source, output],
      { encoding: "utf8" },
    );
    assert.equal(result.status, 0, result.stderr);
    assert.deepEqual(
      fs.readFileSync(output),
      Buffer.from(C.encode(levels)),
      `Python compiler parity ${name}`,
    );
    checks++;
  }
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}
console.log(
  `PASS ${checks} codec checks, including exact card backup and authoritative Python compiler parity.`,
);
