const assert = require("node:assert/strict");
const { create, fromJSON } = require("../dist/codec.js");
const { analyze } = require("../dist/reachability.js");

function result(level, budget) {
  const simulation = analyze(level, budget);
  let update;
  do update = simulation.next(); while (!update.done);
  return update.value;
}

const map = () => create(6, "Test route", 32);
const floor = (level, row, mask) => { level.data[row * 8] = mask; };
const tile = (level, row, kind, mask) => { level.data[row * 8 + kind] = mask; };

assert.equal(result(create(6, "Maximum length", 65535)).status, "pass");

const spawnGap = map();
floor(spawnGap, 4, 27);
assert.deepEqual(result(spawnGap), { status: "blocked", row: 4 });

const gap = map();
for (let row = 11; row <= 13; row++) floor(gap, row, 0);
assert.deepEqual(result(gap), { status: "blocked", row: 11 });
tile(gap, 10, 1, 4);
assert.equal(result(gap).status, "pass");
tile(gap, 10, 1, 0);
tile(gap, 10, 6, 4);
assert.deepEqual(result(gap), { status: "blocked", row: 13 });

const chain = map();
for (let row = 11; row <= 17; row++) floor(chain, row, row === 14 ? 4 : 0);
tile(chain, 10, 1, 4);
tile(chain, 14, 1, 4);
assert.equal(result(chain).status, "pass");
tile(chain, 14, 1, 0);
assert.deepEqual(result(chain), { status: "blocked", row: 15 });

const bypass = map();
for (let row = 11; row <= 15; row++) floor(bypass, row, 1);
tile(bypass, 13, 3, 1);
assert.equal(result(bypass).status, "pass");
tile(bypass, 13, 3, 0);
tile(bypass, 13, 4, 1);
assert.equal(result(bypass).status, "pass"); // Narrow collision boxes leave an edge route.
bypass.mechs.push([14, 1, 4, 2048, 0, 1]);
assert.deepEqual(result(bypass), { status: "blocked", row: 14 });

const displacement = map();
floor(displacement, 10, 1);
floor(displacement, 11, 16);
assert.equal(result(displacement).status, "unknown");

const laser = map();
laser.mechs.push([6, 3, 1, 2048, 42, 384]);
assert.deepEqual(result(laser), { status: "blocked", row: 4 });
laser.mechs[0][1] = 1;
assert.equal(result(laser).status, "pass");

const seeded = fromJSON(require("../../../levels/levels.json"));
assert.deepEqual(seeded.map((l) => result(l).status), ["pass", "blocked", "pass", "pass", "pass"]);
assert.equal(result(gap, 1).status, "unknown");
console.log("PASS reachability: spawn, gaps, jumps, chain, glass, obstacles, steering, laser and bundled levels");
