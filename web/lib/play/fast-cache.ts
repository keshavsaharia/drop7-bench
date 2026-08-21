/**
 * Transposition table for the browser solver (finding-13, O1, in TypeScript).
 *
 * The reference keeps a `Map<string, number>` keyed by a ~56-character string
 * built twice per node (forward and mirrored) and refreshed with delete+set to
 * get LRU order. Profiled in V8, building and hashing those strings plus the
 * Map churn was ~85% of a depth-3 decision. This table keys on the position
 * packed into seven 32-bit words (4 bits per cell, then next disc, moves
 * remaining, terminal flag and depth), probes linearly in typed arrays, and
 * keeps LRU order as an intrusive doubly linked list over slot indexes.
 *
 * The observable behaviour is the reference's exactly: the same keys are
 * equal (packing is injective on the reachable domain), a hit refreshes the
 * entry, an insert first removes an existing entry, then evicts least-recently
 * used entries while the table holds `maxEntries`, then inserts as most recent.
 * `fast-cache.test.ts` drives both against a Map model with random operations.
 */

export const KEY_WORDS = 7;

function hashWords(words: Uint32Array): number {
  let hash = 0x9e3779b9;
  for (let index = 0; index < KEY_WORDS; index += 1) {
    hash = Math.imul(hash ^ words[index], 0x85ebca6b);
    hash ^= hash >>> 13;
  }
  hash = Math.imul(hash, 0xc2b2ae35);
  hash ^= hash >>> 16;
  return hash >>> 0;
}

export class TranspositionTable {
  /** Fill this, then call `get` or `set`. */
  readonly key = new Uint32Array(KEY_WORDS);

  private readonly maxEntries: number;
  private readonly mask: number;
  private readonly words: Uint32Array;
  private readonly hashes: Uint32Array;
  private readonly values: Float64Array;
  private readonly occupied: Uint8Array;
  private readonly prev: Int32Array;
  private readonly next: Int32Array;
  private head = -1;
  private tail = -1;
  private count = 0;

  /**
   * `maxEntries` matches the reference's cap; `slots` must be a power of two
   * larger than `maxEntries` so a probe always finds an empty slot.
   */
  constructor(maxEntries: number, slots: number) {
    if (slots <= maxEntries || (slots & (slots - 1)) !== 0) {
      throw new Error("slots must be a power of two larger than maxEntries");
    }
    this.maxEntries = maxEntries;
    this.mask = slots - 1;
    this.words = new Uint32Array(slots * KEY_WORDS);
    this.hashes = new Uint32Array(slots);
    this.values = new Float64Array(slots);
    this.occupied = new Uint8Array(slots);
    this.prev = new Int32Array(slots);
    this.next = new Int32Array(slots);
  }

  get size() {
    return this.count;
  }

  /** Forget everything; O(slots) but only touches one byte per slot. */
  clear() {
    this.occupied.fill(0);
    this.head = -1;
    this.tail = -1;
    this.count = 0;
  }

  /** Value stored under `key`, refreshed to most recent; `undefined` on a miss. */
  get(): number | undefined {
    const slot = this.find(hashWords(this.key));
    if (slot < 0) return undefined;
    this.unlink(slot);
    this.linkFront(slot);
    return this.values[slot];
  }

  /** Store `value` under `key` as the most recent entry, evicting as the reference does. */
  set(value: number) {
    const hash = hashWords(this.key);
    const existing = this.find(hash);
    if (existing >= 0) this.remove(existing);
    while (this.count >= this.maxEntries) this.remove(this.tail);

    let slot = hash & this.mask;
    while (this.occupied[slot]) slot = (slot + 1) & this.mask;
    this.occupied[slot] = 1;
    this.hashes[slot] = hash;
    this.values[slot] = value;
    const base = slot * KEY_WORDS;
    for (let index = 0; index < KEY_WORDS; index += 1) {
      this.words[base + index] = this.key[index];
    }
    this.count += 1;
    this.linkFront(slot);
  }

  private find(hash: number) {
    const { key, words, hashes, occupied, mask } = this;
    let slot = hash & mask;
    while (occupied[slot]) {
      if (hashes[slot] === hash) {
        const base = slot * KEY_WORDS;
        let equal = true;
        for (let index = 0; index < KEY_WORDS; index += 1) {
          if (words[base + index] !== key[index]) {
            equal = false;
            break;
          }
        }
        if (equal) return slot;
      }
      slot = (slot + 1) & mask;
    }
    return -1;
  }

  private linkFront(slot: number) {
    this.prev[slot] = -1;
    this.next[slot] = this.head;
    if (this.head >= 0) this.prev[this.head] = slot;
    this.head = slot;
    if (this.tail < 0) this.tail = slot;
  }

  private unlink(slot: number) {
    const previous = this.prev[slot];
    const following = this.next[slot];
    if (previous >= 0) this.next[previous] = following;
    else this.head = following;
    if (following >= 0) this.prev[following] = previous;
    else this.tail = previous;
  }

  /** Delete a slot with backward-shift so probe chains stay unbroken. */
  private remove(slot: number) {
    this.unlink(slot);
    this.occupied[slot] = 0;
    this.count -= 1;

    const { mask, hashes, occupied } = this;
    let hole = slot;
    let cursor = slot;
    for (;;) {
      cursor = (cursor + 1) & mask;
      if (!occupied[cursor]) return;
      const home = hashes[cursor] & mask;
      // An entry may stay where it is only if its home lies cyclically in
      // (hole, cursor]; otherwise its probe chain passes through the hole.
      const stays = hole <= cursor ? home > hole && home <= cursor : home > hole || home <= cursor;
      if (stays) continue;
      this.move(cursor, hole);
      hole = cursor;
    }
  }

  private move(from: number, to: number) {
    const { words, hashes, values, occupied, prev, next } = this;
    const fromBase = from * KEY_WORDS;
    const toBase = to * KEY_WORDS;
    for (let index = 0; index < KEY_WORDS; index += 1) {
      words[toBase + index] = words[fromBase + index];
    }
    hashes[to] = hashes[from];
    values[to] = values[from];
    occupied[to] = 1;
    occupied[from] = 0;
    const previous = prev[from];
    const following = next[from];
    prev[to] = previous;
    next[to] = following;
    if (previous >= 0) next[previous] = to;
    else this.head = to;
    if (following >= 0) prev[following] = to;
    else this.tail = to;
  }
}
