import assert from "node:assert/strict";
import test from "node:test";
import { KEY_WORDS, TranspositionTable } from "./fast-cache.ts";

/** The reference's cache discipline, verbatim from solver.ts. */
class MapModel {
  readonly map = new Map<string, number>();
  private readonly maxEntries: number;
  constructor(maxEntries: number) {
    this.maxEntries = maxEntries;
  }
  get(key: string) {
    const cached = this.map.get(key);
    if (cached === undefined) return undefined;
    this.map.delete(key);
    this.map.set(key, cached);
    return cached;
  }
  set(key: string, value: number) {
    if (this.map.has(key)) this.map.delete(key);
    while (this.map.size >= this.maxEntries) {
      const oldest = this.map.keys().next().value;
      if (oldest === undefined) break;
      this.map.delete(oldest);
    }
    this.map.set(key, value);
  }
}

function lcg(seed: number) {
  let state = seed >>> 0;
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 4294967296;
  };
}

test("typed-array LRU table matches the reference Map discipline under random traffic", () => {
  for (const [maxEntries, slots, keySpace, operations] of [
    [8, 16, 20, 20_000],
    [50, 64, 120, 60_000],
    [1_000, 2_048, 3_000, 200_000],
  ] as const) {
    const table = new TranspositionTable(maxEntries, slots);
    const model = new MapModel(maxEntries);
    const random = lcg(maxEntries * 7919 + slots);
    let hits = 0;
    for (let step = 0; step < operations; step += 1) {
      const id = Math.floor(random() * keySpace);
      // Spread ids across the words so collisions exercise probing and shifts.
      for (let index = 0; index < KEY_WORDS; index += 1) {
        table.key[index] = index === id % KEY_WORDS ? id + 1 : (id * (index + 3)) & 7;
      }
      const key = Array.from(table.key).join(",");
      if (random() < 0.6) {
        const expected = model.get(key);
        const actual = table.get();
        assert.equal(actual, expected, `step ${step} get ${key}`);
        if (expected !== undefined) hits += 1;
      } else {
        const value = Math.floor(random() * 1000) - 500;
        model.set(key, value);
        table.set(value);
      }
      assert.equal(table.size, model.map.size, `step ${step} size`);
    }
    assert.ok(hits > operations / 20, `too few hits (${hits}) to exercise refresh`);
  }
});

test("clear empties the table and later traffic still matches", () => {
  const table = new TranspositionTable(4, 8);
  const model = new MapModel(4);
  const random = lcg(99);
  for (let round = 0; round < 5; round += 1) {
    table.clear();
    model.map.clear();
    for (let step = 0; step < 500; step += 1) {
      const id = Math.floor(random() * 9);
      table.key.fill(0);
      table.key[0] = id;
      const key = String(id);
      if (random() < 0.5) assert.equal(table.get(), model.get(key));
      else {
        model.set(key, id * 10);
        table.set(id * 10);
      }
      assert.equal(table.size, model.map.size);
    }
  }
});
