import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const WEB_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SOURCE = path.join(WEB_ROOT, "public", "brand", "drop7-icon.svg");
const BRAND_DIR = path.dirname(SOURCE);

const png = (source, size) =>
  sharp(source, { density: 144 })
    .resize(size, size, { fit: "fill" })
    .png({ compressionLevel: 9, palette: false })
    .toBuffer();

function createIco(images) {
  const headerSize = 6;
  const entrySize = 16;
  let imageOffset = headerSize + entrySize * images.length;
  const header = Buffer.alloc(imageOffset);

  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(images.length, 4);

  images.forEach(({ size, data }, index) => {
    const offset = headerSize + index * entrySize;
    header.writeUInt8(size >= 256 ? 0 : size, offset);
    header.writeUInt8(size >= 256 ? 0 : size, offset + 1);
    header.writeUInt8(0, offset + 2);
    header.writeUInt8(0, offset + 3);
    header.writeUInt16LE(1, offset + 4);
    header.writeUInt16LE(32, offset + 6);
    header.writeUInt32LE(data.length, offset + 8);
    header.writeUInt32LE(imageOffset, offset + 12);
    imageOffset += data.length;
  });

  return Buffer.concat([header, ...images.map(({ data }) => data)]);
}

await mkdir(BRAND_DIR, { recursive: true });
const source = await readFile(SOURCE);

for (const size of [128, 256, 512, 1024]) {
  await writeFile(path.join(BRAND_DIR, `drop7-icon-${size}.png`), await png(source, size));
}

await writeFile(path.join(WEB_ROOT, "app", "icon.png"), await png(source, 512));
await writeFile(path.join(WEB_ROOT, "app", "apple-icon.png"), await png(source, 180));

const faviconImages = await Promise.all(
  [16, 32, 48].map(async (size) => ({ size, data: await png(source, size) })),
);
await writeFile(path.join(WEB_ROOT, "app", "favicon.ico"), createIco(faviconImages));
