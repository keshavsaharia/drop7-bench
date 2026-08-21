const BITS_PER_COLUMN = 3;

/** Packs column choices 0-6 into a dense, most-significant-bit-first stream. */
export function packColumns(columns: readonly number[]): Uint8Array {
  const packed = new Uint8Array(Math.ceil((columns.length * BITS_PER_COLUMN) / 8));
  columns.forEach((column, index) => {
    if (!Number.isInteger(column) || column < 0 || column > 6) {
      throw new RangeError(`Column ${column} at move ${index + 1} is outside 0-6`);
    }
    for (let bit = 0; bit < BITS_PER_COLUMN; bit += 1) {
      const value = (column >> (BITS_PER_COLUMN - bit - 1)) & 1;
      const absoluteBit = index * BITS_PER_COLUMN + bit;
      packed[Math.floor(absoluteBit / 8)] |=
        value << (7 - (absoluteBit % 8));
    }
  });
  return packed;
}

export function unpackColumns(packed: Uint8Array, moveCount: number): number[] {
  if (!Number.isInteger(moveCount) || moveCount < 0) {
    throw new RangeError("Move count must be a non-negative integer");
  }
  const expectedBytes = Math.ceil((moveCount * BITS_PER_COLUMN) / 8);
  if (packed.byteLength !== expectedBytes) {
    throw new RangeError(
      `Packed move stream has ${packed.byteLength} bytes; expected ${expectedBytes}`,
    );
  }

  const columns: number[] = [];
  for (let index = 0; index < moveCount; index += 1) {
    let column = 0;
    for (let bit = 0; bit < BITS_PER_COLUMN; bit += 1) {
      const absoluteBit = index * BITS_PER_COLUMN + bit;
      const value =
        (packed[Math.floor(absoluteBit / 8)] >>
          (7 - (absoluteBit % 8))) &
        1;
      column = (column << 1) | value;
    }
    if (column > 6) throw new RangeError("Packed move stream contains column 7");
    columns.push(column);
  }

  const usedBits = moveCount * BITS_PER_COLUMN;
  for (let absoluteBit = usedBits; absoluteBit < packed.byteLength * 8; absoluteBit += 1) {
    const value =
      (packed[Math.floor(absoluteBit / 8)] >>
        (7 - (absoluteBit % 8))) &
      1;
    if (value !== 0) throw new RangeError("Packed move stream has non-zero padding");
  }
  return columns;
}
