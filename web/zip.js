// Tiny zip writer/reader for save archives (browser). Deflate via CompressionStream('deflate-raw')
// when it helps, otherwise store. Reader handles store + deflate, no zip64, no encryption.
// Layout follows tools/build-data/zip.mjs (UTF-8 names, flag 0x0800).
const T = new Uint32Array(256);
for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
export function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = T[(c ^ buf[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

const pipe = async (data, stream) => new Uint8Array(await new Response(new Blob([data]).stream().pipeThrough(stream)).arrayBuffer());
const deflate = (d) => pipe(d, new CompressionStream('deflate-raw'));
const inflate = (d) => pipe(d, new DecompressionStream('deflate-raw'));

function dosTime(date) {
  const t = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
  const d = ((Math.max(1980, date.getFullYear()) - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
  return [t, d];
}

// entries: [{path, data: Uint8Array, mtime?: Date}] -> Uint8Array (zip); a path ending in '/' with
// empty data is a directory entry (Colobot keeps empty player directories).
export async function writeZip(entries) {
  const enc = new TextEncoder(), parts = [], central = [];
  let pos = 0;
  for (const e of entries) {
    const name = enc.encode(e.path), data = e.data, crc = crc32(data);
    let method = 0, body = data;
    if (data.length > 64 && typeof CompressionStream !== 'undefined') {
      const z = await deflate(data);
      if (z.length < data.length) { method = 8; body = z; }
    }
    const [t, d] = dosTime(e.mtime || new Date());
    const lh = new DataView(new ArrayBuffer(30));
    lh.setUint32(0, 0x04034b50, true); lh.setUint16(4, 20, true); lh.setUint16(6, 0x0800, true);
    lh.setUint16(8, method, true); lh.setUint16(10, t, true); lh.setUint16(12, d, true);
    lh.setUint32(14, crc, true); lh.setUint32(18, body.length, true); lh.setUint32(22, data.length, true);
    lh.setUint16(26, name.length, true);
    const ch = new DataView(new ArrayBuffer(46));
    ch.setUint32(0, 0x02014b50, true); ch.setUint16(4, 20, true); ch.setUint16(6, 20, true); ch.setUint16(8, 0x0800, true);
    ch.setUint16(10, method, true); ch.setUint16(12, t, true); ch.setUint16(14, d, true);
    ch.setUint32(16, crc, true); ch.setUint32(20, body.length, true); ch.setUint32(24, data.length, true);
    ch.setUint16(28, name.length, true); ch.setUint32(42, pos, true);
    parts.push(new Uint8Array(lh.buffer), name, body);
    central.push(new Uint8Array(ch.buffer), name);
    pos += 30 + name.length + body.length;
  }
  const cdSize = central.reduce((s, p) => s + p.length, 0);
  const end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true); end.setUint16(8, entries.length, true); end.setUint16(10, entries.length, true);
  end.setUint32(12, cdSize, true); end.setUint32(16, pos, true);
  const all = [...parts, ...central, new Uint8Array(end.buffer)];
  const out = new Uint8Array(all.reduce((s, p) => s + p.length, 0));
  let o = 0;
  for (const p of all) { out.set(p, o); o += p.length; }
  return out;
}

// Uint8Array (zip) -> [{path, data: Uint8Array, dir?: true}]; throws on malformed input.
export async function readZip(buf) {
  const u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  let eocd = -1;
  for (let i = u8.length - 22; i >= Math.max(0, u8.length - 22 - 65535); i--)
    if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('not a zip file');
  const count = dv.getUint16(eocd + 10, true);
  let p = dv.getUint32(eocd + 16, true);
  const dec = new TextDecoder(), out = [];
  for (let n = 0; n < count; n++) {
    if (dv.getUint32(p, true) !== 0x02014b50) throw new Error('bad zip central directory');
    const method = dv.getUint16(p + 10, true), crc = dv.getUint32(p + 16, true);
    const csize = dv.getUint32(p + 20, true), usize = dv.getUint32(p + 24, true);
    const nlen = dv.getUint16(p + 28, true), xlen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true);
    const off = dv.getUint32(p + 42, true);
    const path = dec.decode(u8.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + xlen + clen;
    if (path.endsWith('/')) { out.push({ path, data: new Uint8Array(0), dir: true }); continue; }
    if (dv.getUint32(off, true) !== 0x04034b50) throw new Error('bad zip local header');
    const start = off + 30 + dv.getUint16(off + 26, true) + dv.getUint16(off + 28, true);
    const raw = u8.subarray(start, start + csize);
    let data;
    if (method === 0) data = raw.slice();
    else if (method === 8) data = await inflate(raw);
    else throw new Error('unsupported zip method ' + method);
    if (data.length !== usize || crc32(data) !== crc) throw new Error('zip CRC mismatch: ' + path);
    out.push({ path, data });
  }
  return out;
}
