#!/usr/bin/env node
// WP5 asset tool: colobot-data -> dist/data/{manifest.json, *.pak, loose files}
// Usage: node tools/build-data/build.mjs [--src DIR] [--out DIR] [--music] [--music-cache DIR]
//        [--sounds-ogg] [--no-oxipng]
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { writeZip, sha256 } from './zip.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf('--' + n); return i < 0 ? d : args[i + 1]; };
const flag = (n) => args.includes('--' + n);
const SRC = path.resolve(opt('src', path.join(root, 'upstream/colobot-data')));
const SRC_FALLBACK = '/Users/danielbogusz/projects/colobot-web/upstream/colobot-data';
const src = fs.existsSync(SRC) ? SRC : SRC_FALLBACK;
const OUT = path.resolve(opt('out', path.join(root, 'dist/data')));
const CACHE = path.resolve(opt('music-cache', path.join(root, '.cache/build-data')));
const PACK_MAX = 25 * 1024 * 1024 - 64 * 1024, BOOT_MAX = 15 * 1000 * 1000;

const INCLUDE = ['ai', 'fonts', 'icons', 'models', 'sounds', 'textures', 'levels', 'help'];
// mods/AltTextures deliberately excluded (only mod dir upstream).
// English only: engine falls back to help/%lng%->E (parser.cpp:336) and level txt are English sources; po/pot dropped.
const EXCL_DIR = /(^|\/)(textures-src|models-new|po|i18n-tools|tools)(\/|$)/;
const EXCL_EXT = /\.(xcf|max|blend|blend1|po|pot|psd|md)$|(^|\/)(CMakeLists\.txt|Jenkinsfile|LICENSE\.txt)$/i;
const log = (...a) => console.log('[build-data]', ...a);
const warn = (...a) => console.warn('[build-data] WARN', ...a);
const has = (cmd) => !spawnSync(cmd, ['--version']).error;

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
const oggSounds = flag('sounds-ogg');
if (oggSounds && !useFF) warn('ffmpeg missing: --sounds-ogg ignored');

function load(rel) {
  const p = path.join(src, rel);
  if (useOxi && rel.endsWith('.png')) {
    const tmp = path.join(os.tmpdir(), 'bd-' + process.pid + '.png');
    try { execFileSync('oxipng', ['-q', '-o', '3', '--strip', 'safe', '--out', tmp, '--force', p]); const b = fs.readFileSync(tmp); fs.unlinkSync(tmp); return b; }
    catch { return fs.readFileSync(p); }
  }
  if (oggSounds && useFF && rel.endsWith('.wav')) {
    // Engine hardcodes sounds/soundNNN.wav (sound.cpp:50): content is ogg, name stays .wav.
    // Only usable if the web audio decoder sniffs content; off by default.
    const r = spawnSync('ffmpeg', ['-v', 'error', '-i', p, '-c:a', 'libvorbis', '-q:a', '4', '-f', 'ogg', '-'], { maxBuffer: 1 << 28 });
    if (r.status === 0 && r.stdout.length) return r.stdout;
  }
  return fs.readFileSync(p);
}

const all = [];
for (const d of INCLUDE) if (fs.existsSync(path.join(src, d))) all.push(...walk(src, d));
log('source:', src, '- files selected:', all.length);

const isBoot = (f) =>
  f.startsWith('fonts/') || f.startsWith('icons/') ||
  (f.startsWith('textures/interface/') && !/\/intro\d+\.png$/.test(f)) ||
  /^textures\/(effect\d+|shadow\d+|lens\d+)\.png$/.test(f) ||
  /^levels\/[^/]+\/[^/]+\.(txt|png)$/.test(f) || f.startsWith('levels/other/') ||
  /^levels\/[^/]+\/chapter\d+\/chaptertitle\.txt$/.test(f);
const groupOf = (f) => {
  if (isBoot(f)) return 'boot';
  if (/\/intro\d+\.png$/.test(f)) return 'intro';
  const m = f.match(/^levels\/([^/]+)\/(chapter\d+)\//);
  if (m) return `lvl-${m[1]}-${m[2]}`;
  if (f.startsWith('levels/')) return 'levels-misc';
  if (f.startsWith('help/')) return 'help';
  if (f.startsWith('sounds/')) return 'sounds';
  if (f.startsWith('models/')) return 'models';
  return 'common'; // ai, remaining textures
};
const groups = new Map();
for (const f of all) { const g = groupOf(f); if (!groups.has(g)) groups.set(g, []); groups.get(g).push(f); }

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });
const manifest = { version: 1, packs: {}, files: {} };

function emitPack(name, rels, limit) {
  let part = 1, cur = [], size = 0;
  const flush = () => {
    if (!cur.length) return;
    const pn = part === 1 ? name : `${name}-${part}`;
    const r = writeZip(cur, path.join(OUT, pn + '.pak'));
    manifest.packs[pn] = { url: pn + '.pak', size: r.size, sha256: r.sha256 };
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
emitPack('boot', groups.get('boot') || [], BOOT_MAX); groups.delete('boot');
for (const [g, rels] of groups) emitPack(g, rels, PACK_MAX);

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
    const rel = `music/${n}.ogg`;
    fs.mkdirSync(path.join(OUT, 'music'), { recursive: true });
    fs.writeFileSync(path.join(OUT, rel), data);
    manifest.files[rel] = { pack: null, url: rel, size: data.length, sha256: sha256(data) };
  }
  log('music files:', Object.keys(manifest.files).filter((k) => k.startsWith('music/')).length);
}

fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(manifest));
const total = Object.values(manifest.packs).reduce((a, p) => a + p.size, 0);
log(`done: ${Object.keys(manifest.packs).length} packs, ${Object.keys(manifest.files).length} files, ${(total / 1048576).toFixed(1)} MiB -> ${OUT}`);
