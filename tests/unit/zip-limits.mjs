// readZip limits: total/entry inflated size caps, a central directory that lies about sizes, truncation.
// Usage: node tests/unit/zip-limits.mjs
let failed = false;
const fail = (m) => { failed = true; console.log('FAIL', m); };
import { writeZip, readZip } from '../../web/zip.js';
const big = new Uint8Array(40 << 20);
const z = await writeZip([{ path: 'a/', data: new Uint8Array(0) }, { path: 'a/b.txt', data: new TextEncoder().encode('hello'.repeat(50)) }, { path: 'big', data: big }]);
console.log('zip bytes', z.length);
try { await readZip(z); fail('no throw'); } catch (e) { console.log('ok total cap:', e.message); }
const r = await readZip(z, { entries: 10, entryBytes: 64 << 20, totalBytes: 128 << 20 }); console.log('ok roundtrip', r.map(e => e.path + ':' + e.data.length));
// lie about usize: patch central dir usize of 'big' to 1000
const dv = new DataView(z.buffer); let p = dv.getUint32(z.length - 22 + 16, true);
for (let n = 0; n < 3; n++) { const nl = dv.getUint16(p + 28, true); if (n === 2) { dv.setUint32(p + 24, 1000, true); } p += 46 + nl; }
try { await readZip(z); fail('no throw'); } catch (e) { console.log('ok lying usize:', e.message); }
try { await readZip(z.slice(0, 100)); fail('no throw'); } catch (e) { console.log('ok truncated:', e.message); }
process.exit(failed ? 1 : 0);
