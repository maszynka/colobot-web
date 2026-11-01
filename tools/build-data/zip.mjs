// Minimal store-only zip writer. Returns data offsets so manifest can address raw bytes.
import { createHash } from 'node:crypto';
import fs from 'node:fs';

const T = new Uint32Array(256);
for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
export function crc32(buf) { let c = 0xffffffff; for (let i = 0; i < buf.length; i++) c = T[(c ^ buf[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
export const sha256 = (b) => createHash('sha256').update(b).digest('hex');

// entries: [{path, data:Buffer}] -> {size, sha256, files:{path:{offset,size,sha256}}}
export function writeZip(entries, outFile) {
  const parts = []; const central = []; const files = {}; let pos = 0;
  for (const { path: p, data } of entries) {
    const name = Buffer.from(p, 'utf8'); const crc = crc32(data);
    const lh = Buffer.alloc(30);
    lh.writeUInt32LE(0x04034b50, 0); lh.writeUInt16LE(20, 4); lh.writeUInt16LE(0x0800, 6); lh.writeUInt16LE(0, 8);
    lh.writeUInt32LE(0x00210000, 10); lh.writeUInt32LE(crc, 14); lh.writeUInt32LE(data.length, 18); lh.writeUInt32LE(data.length, 22);
    lh.writeUInt16LE(name.length, 26); lh.writeUInt16LE(0, 28);
    const off = pos; parts.push(lh, name, data); pos += 30 + name.length;
    files[p] = { offset: pos, size: data.length, sha256: sha256(data) };
    pos += data.length;
    const ch = Buffer.alloc(46);
    ch.writeUInt32LE(0x02014b50, 0); ch.writeUInt16LE(20, 4); ch.writeUInt16LE(20, 6); ch.writeUInt16LE(0x0800, 8); ch.writeUInt16LE(0, 10);
    ch.writeUInt32LE(0x00210000, 12); ch.writeUInt32LE(crc, 16); ch.writeUInt32LE(data.length, 20); ch.writeUInt32LE(data.length, 24);
    ch.writeUInt16LE(name.length, 28); ch.writeUInt32LE(off, 42);
    central.push(ch, name);
  }
  const cd = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(pos, 16);
  const buffer = Buffer.concat([...parts, cd, end]);
  fs.writeFileSync(outFile, buffer);
  return { size: buffer.length, sha256: sha256(buffer), files };
}
