#!/usr/bin/env node
// WP5 asset tool: colobot-data -> dist/data/{manifest.json, *.pak, loose files}
// Usage: node tools/build-data/build.mjs [--src DIR] [--out DIR] [--music] [--music-cache DIR]
//        [--sounds-wav] [--no-oxipng] [--loose-min BYTES]
// Layout (WP13): boot.pak (+ boot.pak.gz, inflated by loader.js) holds what the menu needs;
// sounds, intro, icons, music and every other file >= --loose-min (64 KiB) are loose files
// (fetched per file, cached by sha256 in sw.js); small files stay grouped in packs.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { execFileSync, spawnSync } from 'node:child_process';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { writeZip, sha256 } from './zip.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i < 0 ? d : args[i + 1]; };
const flag = (n) => args.includes('--' + n);
const SRC = path.resolve(opt('src', path.join(root, 'upstream/colobot-data')));
const src = SRC;
if (!fs.existsSync(src)) { console.error(`colobot-data not found at ${src}; clone https://github.com/colobot/colobot-data there or pass --src`); process.exit(1); }
const OUT = path.resolve(opt('out', path.join(root, 'dist/data')));
const CACHE = path.resolve(opt('music-cache', path.join(root, '.cache/build-data')));
const PACK_MAX = 25 * 1024 * 1024 - 64 * 1024, BOOT_MAX = 15 * 1000 * 1000;

const INCLUDE = ['ai', 'fonts', 'icons', 'models', 'sounds', 'textures', 'levels', 'help'];
// mods/AltTextures deliberately excluded (only mod dir upstream).
// English only: engine falls back to help/%lng%->E (parser.cpp:336) and level txt are English sources; po/pot dropped.
const EXCL_DIR = /(^|\/)(textures-src|models-new|po|i18n-tools|tools)(\/|$)/;
const EXCL_EXT = /\.(xcf|max|blend|blend1|po|pot|psd|md|svg)$|(^|\/)(CMakeLists\.txt|Jenkinsfile|LICENSE\.txt)$/i;
const log = (...a) => console.log('[build-data]', ...a);
const warn = (...a) => console.warn('[build-data] WARN', ...a);
const has = (cmd) => { const r = spawnSync(cmd, ['--version']); return !r.error && r.status === 0; };

function walk(dir, rel = '', out = []) {
  for (const e of fs.readdirSync(path.join(dir, rel), { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const r = rel ? rel + '/' + e.name : e.name;
    if (EXCL_DIR.test(r)) continue;
    if (e.isDirectory()) walk(dir, r, out);
    else if (e.isFile() && !EXCL_EXT.test(r)) out.push(r);
  }
  return out;
}

const useOxi = !flag('no-oxipng') && has('oxipng');
if (!useOxi) warn('oxipng not available/disabled: PNGs copied as-is');
const useFF = has('ffmpeg');
const useOggenc = has('oggenc');
// vorbis_wav_reader sniffs RIFF/OggS magic, so sounds/*.wav may hold Ogg Vorbis (on by default).
const oggSounds = !flag('sounds-wav');
if (oggSounds && !useFF && !useOggenc) warn('oggenc/ffmpeg missing: sounds kept as wav');
const LOOSE_MIN = +opt('loose-min', String(64 * 1024));

function load(rel) {
  const p = path.join(src, rel);
  if (useOxi && rel.endsWith('.png')) {
    const tmp = path.join(os.tmpdir(), 'bd-' + process.pid + '.png');
    try { execFileSync('oxipng', ['-q', '-o', '3', '--strip', 'safe', '--out', tmp, '--force', p]); const b = fs.readFileSync(tmp); fs.unlinkSync(tmp); return b; }
    catch { return fs.readFileSync(p); }
  }
  if (oggSounds && (useOggenc || useFF) && rel.endsWith('.wav')) {
    // Engine hardcodes sounds/soundNNN.wav (sound.cpp:50): content is ogg, name stays .wav.
    const r = useOggenc
      ? spawnSync('oggenc', ['-Q', '-q', '3', '-o', '-', p], { maxBuffer: 1 << 28 })
      : spawnSync('ffmpeg', ['-v', 'error', '-i', p, '-c:a', 'libvorbis', '-q:a', '3', '-f', 'ogg', '-'], { maxBuffer: 1 << 28 });
    const raw = fs.readFileSync(p);
    if (r.status === 0 && r.stdout.length && r.stdout.length < raw.length) return r.stdout;
    return raw;
  }
  return fs.readFileSync(p);
}

const all = [];
for (const d of INCLUDE) if (fs.existsSync(path.join(src, d))) all.push(...walk(src, d));
log('source:', src, '- files selected:', all.length);

const isBoot = (f) =>
  f.startsWith('fonts/') ||
  // intro*.png: welcome screens (skipped on web); generico.png: credits/quit screen only (2.5 MB)
  (f.startsWith('textures/interface/') && !/\/(intro\d+|generico)\.png$/.test(f)) ||
  /^textures\/(effect\d+|shadow\d+|lens\d+)\.png$/.test(f) ||
  /^levels\/[^/]+\/[^/]+\.(txt|png)$/.test(f) || f.startsWith('levels/other/') ||
  /^levels\/[^/]+\/chapter\d+\/chaptertitle\.txt$/.test(f);
const groupOf = (f) => {
  if (isBoot(f)) return 'boot';
  if (/\/(intro\d+|generico)\.png$/.test(f) || f.startsWith('icons/') || f.startsWith('sounds/')) return 'loose';
  const m = f.match(/^levels\/([^/]+)\/(chapter\d+)\//);
  if (m) return `lvl-${m[1]}-${m[2]}`;
  if (f.startsWith('levels/')) return 'levels-misc';
  if (f.startsWith('help/')) return 'help';
  if (f.startsWith('models/')) return 'models';
  return 'common'; // ai, remaining textures
};
const groups = new Map();
for (const f of all) { const g = groupOf(f); if (!groups.has(g)) groups.set(g, []); groups.get(g).push(f); }

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });
const manifest = { version: 1, packs: {}, files: {} };

// Content-addressed URLs (name.<sha256[:12]>.ext): a redeploy can never pair a new manifest with a
// stale pack from the HTTP/CDN cache, and /data/* is safely `immutable`.
const hashed = (rel, h) => {
  const dir = path.posix.dirname(rel), base = path.posix.basename(rel);
  const dot = base.indexOf('.');
  const name = dot < 0 ? `${base}.${h.slice(0, 12)}` : `${base.slice(0, dot)}.${h.slice(0, 12)}${base.slice(dot)}`;
  return dir === '.' ? name : `${dir}/${name}`;
};

function emitPack(name, rels, limit) {
  let part = 1, cur = [], size = 0;
  const flush = () => {
    if (!cur.length) return;
    const pn = part === 1 ? name : `${name}-${part}`;
    const tmp = path.join(OUT, pn + '.pak');
    const r = writeZip(cur, tmp);
    const url = hashed(pn + '.pak', r.sha256);
    fs.renameSync(tmp, path.join(OUT, url));
    manifest.packs[pn] = { url, size: r.size, sha256: r.sha256 };
    for (const [p, v] of Object.entries(r.files)) manifest.files[p] = { pack: pn, ...v };
    log(pn.padEnd(28), (r.size / 1048576).toFixed(2).padStart(7), 'MiB', cur.length, 'files');
    if (r.size > limit) warn(pn, 'exceeds limit', limit);
    cur = []; size = 0; part++;
  };
  for (const rel of rels) {
    const data = load(rel);
    if (size + data.length + 200 > limit && cur.length) flush();
    cur.push({ path: rel, data }); size += data.length + 100 + rel.length * 2;
  }
  flush();
}
function emitLoose(rel, data) {
  const h = sha256(data), url = hashed(rel, h);
  fs.mkdirSync(path.dirname(path.join(OUT, url)), { recursive: true });
  fs.writeFileSync(path.join(OUT, url), data);
  manifest.files[rel] = { pack: null, url, size: data.length, sha256: h };
}
emitPack('boot', groups.get('boot') || [], BOOT_MAX); groups.delete('boot');
// boot.pak is fetched before the first frame and served as application/octet-stream, which
// Cloudflare does not compress: ship a gzip twin that loader.js inflates (DecompressionStream).
{
  const raw = fs.readFileSync(path.join(OUT, manifest.packs.boot.url));
  const gz = zlib.gzipSync(raw, { level: 9 });
  const gzUrl = hashed('boot.pak.gz', sha256(gz));
  fs.writeFileSync(path.join(OUT, gzUrl), gz);
  manifest.packs.boot.gz = { url: gzUrl, size: gz.length, sha256: sha256(gz) };
  log(gzUrl.padEnd(28), (gz.length / 1048576).toFixed(2).padStart(7), 'MiB');
}
let looseN = 0, looseB = 0;
for (const [g, rels] of groups) {
  const small = [];
  for (const rel of rels) {
    if (g !== 'loose' && fs.statSync(path.join(src, rel)).size < LOOSE_MIN) { small.push(rel); continue; }
    const data = load(rel); emitLoose(rel, data); looseN++; looseB += data.length;
  }
  if (small.length) emitPack(g, small, PACK_MAX);
}
log('loose files:', looseN, (looseB / 1048576).toFixed(2), 'MiB');

const MUSIC = ['Intro1', 'Intro2', 'music002', 'music003', 'music004', 'music005', 'music006', 'music007', 'music008', 'music009', 'music010', 'music011', 'music012', 'music013', 'Constructive', 'Humanitarian', 'Hv2', 'Quite', 'Infinite', 'Proton', 'Prototype'];
if (flag('music')) {
  fs.mkdirSync(path.join(CACHE, 'music'), { recursive: true });
  for (const n of MUSIC) {
    const f = path.join(CACHE, 'music', n + '.ogg');
    if (!fs.existsSync(f)) {
      try {
        const res = await fetch(`https://colobot.info/files/music/${n}.ogg`);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        fs.writeFileSync(f, Buffer.from(await res.arrayBuffer()));
      } catch (e) { warn('music', n, 'download failed:', e.message); continue; }
    }
    const data = fs.readFileSync(f);
    if (data.length > 25 * 1024 * 1024) warn(n, 'exceeds 25MiB');
    emitLoose(`music/${n}.ogg`, data);
  }
  log('music files:', Object.keys(manifest.files).filter((k) => k.startsWith('music/')).length);
}

fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(manifest));
const total = Object.values(manifest.packs).reduce((a, p) => a + p.size, 0);
log(`done: ${Object.keys(manifest.packs).length} packs, ${Object.keys(manifest.files).length} files, ${(total / 1048576).toFixed(1)} MiB -> ${OUT}`);
