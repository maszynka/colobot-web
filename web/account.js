// Accounts + save sync for the web shell (docs/ACCOUNTS.md).
//   Modes: 'cloud' (signed in: /save is mirrored to /api/saves) or 'local' (this browser only).
//   Hooks (called by web_archiver.c): Module.onSaveMounted() before the game reads /save,
//   Module.onSaveFlushed() after every IDBFS flush.
import { writeZip, readZip } from './zip.js';

const PL = /^pl\b/i.test(navigator.language || '');
const L = PL ? {
  title: 'Twoje zapisy gry',
  intro: 'Wybierz, gdzie przechowywać postępy.',
  google: 'Zaloguj przez Google',
  googleOff: 'Google — jeszcze nieskonfigurowane',
  email: 'E-mail i hasło',
  local: 'Graj lokalnie (tylko ta przeglądarka)',
  cloudNote: 'Po zalogowaniu zapisy synchronizują się automatycznie — możesz kontynuować grę na dowolnym urządzeniu (PC, laptop, inna przeglądarka).',
  localNote: 'Lokalnie zapisy zostają tylko w tej przeglądarce. Użyj „Pobierz zapisy”, aby zrobić kopię.',
  signIn: 'Zaloguj', register: 'Załóż konto', back: 'Wstecz',
  emailPh: 'e-mail', passPh: 'hasło (min. 8 znaków)',
  haveAccount: 'Mam konto', noAccount: 'Nie mam konta',
  localBadge: 'Lokalnie', offline: 'offline',
  syncNow: 'Synchronizuj teraz', download: 'Pobierz zapisy (.zip)', import: 'Importuj zapisy (.zip)',
  signOut: 'Wyloguj', signInMenu: 'Zaloguj, aby grać na wielu urządzeniach', del: 'Usuń konto i zapisy w chmurze',
  privacy: 'Prywatność',
  synced: 'Zsynchronizowano', syncing: 'Synchronizacja…', syncFail: 'Synchronizacja nieudana — spróbuję ponownie',
  saved: 'Zapisano w tej przeglądarce', dl: 'Pobierz',
  conflictT: 'Dwie różne wersje zapisów',
  conflictB: 'Zapisy w chmurze zmieniły się na innym urządzeniu, a ta przeglądarka ma inne. Którą wersję zachować? Druga zostanie zastąpiona — najpierw możesz ją pobrać.',
  useCloud: 'Użyj chmury', useLocal: 'Użyj tego urządzenia', dlCloud: 'Pobierz wersję z chmury', dlLocal: 'Pobierz wersję z tego urządzenia',
  importConfirm: 'Zastąpić zapisy na tym urządzeniu zawartością pliku? Gra zostanie przeładowana.',
  delConfirm: 'Usunąć konto oraz wszystkie zapisy w chmurze? Tej operacji nie można cofnąć. Zapisy w tej przeglądarce zostaną.',
  restoring: 'Przywracanie zapisów z chmury…',
  expired: 'Sesja wygasła — zapisy zostają w tej przeglądarce.',
  errors: {
    bad_credentials: 'Nieprawidłowy e-mail lub hasło.', email_taken: 'To konto już istnieje — zaloguj się.',
    bad_email: 'Nieprawidłowy adres e-mail.', bad_password: 'Hasło musi mieć 8–200 znaków.',
    too_many_attempts: 'Zbyt wiele prób. Spróbuj za kilkanaście minut.',
    google_not_configured: 'Logowanie przez Google nie jest jeszcze skonfigurowane.',
    google_denied: 'Anulowano logowanie przez Google.', google_state: 'Sesja logowania wygasła — spróbuj ponownie.',
    google_token: 'Google nie potwierdził logowania — spróbuj ponownie.', too_large: 'Zapisy są za duże (limit 20 MB).',
    network: 'Brak połączenia z serwerem.', bad_zip: 'To nie jest poprawny plik .zip z zapisami.',
  },
} : {
  title: 'Your saved games',
  intro: 'Choose where to keep your progress.',
  google: 'Sign in with Google',
  googleOff: 'Google — not configured yet',
  email: 'Email and password',
  local: 'Play locally (this browser only)',
  cloudNote: 'Signed in, your saves sync automatically — continue on any device (PC, laptop, another browser).',
  localNote: 'Locally, saves stay only in this browser. Use “Download saves” to keep a backup.',
  signIn: 'Sign in', register: 'Create account', back: 'Back',
  emailPh: 'email', passPh: 'password (min. 8 characters)',
  haveAccount: 'I have an account', noAccount: 'I need an account',
  localBadge: 'Local', offline: 'offline',
  syncNow: 'Sync now', download: 'Download saves (.zip)', import: 'Import saves (.zip)',
  signOut: 'Sign out', signInMenu: 'Sign in to play on many devices', del: 'Delete account and cloud saves',
  privacy: 'Privacy',
  synced: 'Synced', syncing: 'Syncing…', syncFail: 'Sync failed — will retry',
  saved: 'Saved in this browser', dl: 'Download',
  conflictT: 'Two different versions of your saves',
  conflictB: 'Your cloud saves changed on another device, and this browser has different ones. Which version do you want to keep? The other one is replaced — you can download it first.',
  useCloud: 'Use cloud', useLocal: 'Use this device', dlCloud: 'Download cloud version', dlLocal: 'Download this device’s version',
  importConfirm: 'Replace the saves on this device with this file? The game will reload.',
  delConfirm: 'Delete your account and all cloud saves? This cannot be undone. Saves in this browser are kept.',
  restoring: 'Restoring saves from the cloud…',
  expired: 'Session expired — saves stay in this browser.',
  errors: {
    bad_credentials: 'Wrong email or password.', email_taken: 'That account already exists — sign in instead.',
    bad_email: 'That email address looks wrong.', bad_password: 'The password needs 8–200 characters.',
    too_many_attempts: 'Too many attempts. Try again in a few minutes.',
    google_not_configured: 'Google sign-in is not configured yet.',
    google_denied: 'Google sign-in was cancelled.', google_state: 'The sign-in expired — please try again.',
    google_token: 'Google did not confirm the sign-in — please try again.', too_large: 'Saves are too large (20 MB limit).',
    network: 'Cannot reach the server.', bad_zip: 'That is not a valid saves .zip file.',
  },
};
const errText = (code) => L.errors[code] || code;

const MODE_KEY = 'colobot.mode';
const SYNC_KEY = (uid) => 'colobot.sync.' + uid;
const ls = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { v == null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch { /* private mode */ } },
};

// ---------------- API ----------------
async function api(path, { method = 'GET', body, headers = {}, raw = false } = {}) {
  const h = { ...headers };
  if (method !== 'GET') h['X-Colobot'] = '1';
  if (body !== undefined && !(body instanceof Uint8Array)) { h['Content-Type'] = 'application/json'; body = JSON.stringify(body); }
  let r;
  try { r = await fetch(path, { method, body, headers: h, credentials: 'same-origin', cache: 'no-store' }); }
  catch { const e = new Error('network'); e.code = 'network'; throw e; }
  if (raw && r.ok) return r;
  const data = r.headers.get('Content-Type')?.includes('json') ? await r.json() : {};
  if (!r.ok) { const e = new Error(data.error || 'http_' + r.status); e.code = data.error || 'http_' + r.status; e.status = r.status; e.data = data; throw e; }
  return data;
}

// ---------------- /save <-> entries ----------------
function listSave(FS) {
  const out = [];
  const walk = (dir) => {
    let names;
    try { names = FS.readdir(dir); } catch { return; }
    for (const n of names.sort()) {
      if (n === '.' || n === '..') continue;
      const p = dir + '/' + n, st = FS.stat(p);
      if (FS.isDir(st.mode)) { out.push({ path: p.slice('/save/'.length) + '/', data: new Uint8Array(0), mtime: new Date(st.mtime) }); walk(p); }
      else out.push({ path: p.slice('/save/'.length), data: FS.readFile(p), mtime: new Date(st.mtime) });
    }
  };
  walk('/save');
  return out;
}

async function hashEntries(entries) {
  const enc = new TextEncoder(), parts = [];
  for (const e of entries) parts.push(enc.encode(e.path + '\0' + e.data.length + '\0'), e.data);
  const d = await crypto.subtle.digest('SHA-256', await new Blob(parts).arrayBuffer());
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

function replaceSave(FS, entries) {
  const rm = (dir) => {
    for (const n of FS.readdir(dir)) {
      if (n === '.' || n === '..') continue;
      const p = dir + '/' + n;
      if (FS.isDir(FS.stat(p).mode)) { rm(p); FS.rmdir(p); } else FS.unlink(p);
    }
  };
  rm('/save');
  for (const e of entries) {
    const clean = e.path.split('/').filter((s) => s && s !== '.' && s !== '..');
    if (!clean.length) continue;
    const isDir = e.path.endsWith('/');
    let d = '/save';
    for (const s of isDir ? clean : clean.slice(0, -1)) { d += '/' + s; try { FS.mkdir(d); } catch { /* exists */ } }
    if (!isDir) FS.writeFile(d + '/' + clean[clean.length - 1], e.data);
  }
}

function deviceLabel() {
  const ua = navigator.userAgent;
  const os = /Windows/.test(ua) ? 'Windows' : /Mac OS X/.test(ua) ? 'macOS' : /Android/.test(ua) ? 'Android'
    : /iPhone|iPad/.test(ua) ? 'iOS' : /CrOS/.test(ua) ? 'ChromeOS' : /Linux/.test(ua) ? 'Linux' : 'Web';
  const br = /Edg\//.test(ua) ? 'Edge' : /OPR\//.test(ua) ? 'Opera' : /Firefox\//.test(ua) ? 'Firefox' : /Chrome\//.test(ua) ? 'Chrome' : /Safari\//.test(ua) ? 'Safari' : '';
  return (os + ' ' + br).trim();
}

function saveBlob(bytes, name) {
  const url = URL.createObjectURL(new Blob([bytes], { type: 'application/zip' }));
  const a = document.createElement('a');
  a.href = url; a.download = name; document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}
const stamp = () => new Date().toISOString().slice(0, 16).replace(/[T:]/g, '-');

// ---------------- UI helpers ----------------
function el(tag, attrs = {}, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'class') e.className = v;
    else if (v !== false && v != null) e.setAttribute(k, v === true ? '' : v);
  }
  for (const k of kids.flat()) if (k != null) e.append(k);
  return e;
}

let toastTimer = 0;
function toast(text, action) {
  let t = document.getElementById('acct-toast');
  if (!t) { t = el('div', { id: 'acct-toast', role: 'status' }); document.body.append(t); }
  t.replaceChildren(el('span', {}, text), action ? el('button', { class: 'acct-link', onclick: action.fn }, action.label) : null);
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), action ? 7000 : 3000);
}

function modal(build) {
  const wrap = el('div', { class: 'acct-modal', role: 'dialog', 'aria-modal': 'true' });
  const panel = el('div', { class: 'acct-panel' });
  wrap.append(panel);
  document.body.append(wrap);
  const close = () => wrap.remove();
  build(panel, close);
  // Keep keys typed into the dialog away from the game's canvas handlers.
  for (const ev of ['keydown', 'keyup', 'keypress']) wrap.addEventListener(ev, (e) => e.stopPropagation());
  setTimeout(() => panel.querySelector('input,button')?.focus(), 0);
  return close;
}

// ---------------- the account controller ----------------
export function startAccount() {
  const s = {
    mode: null,          // 'cloud' | 'local'
    user: null,
    google: false,
    offline: false,
    Module: null,
    sync: null,          // {base, hash} for the signed-in user
    uploading: null,
    again: false,
    timer: 0,
    lastToastHash: null,
    lastToast: 0,
    status: '',
  };
  window.__colobotAccount = s;
  const loadSync = () => { try { return JSON.parse(ls.get(SYNC_KEY(s.user.id))) || {}; } catch { return {}; } };
  const storeSync = (v) => { s.sync = v; ls.set(SYNC_KEY(s.user.id), JSON.stringify(v)); };

  const params = new URLSearchParams(location.search);
  const authError = params.get('auth_error');
  if (authError || params.has('signed_in')) {
    params.delete('auth_error'); params.delete('signed_in');
    history.replaceState(null, '', location.pathname + (params.toString() ? '?' + params : '') + location.hash);
  }

  let decide;
  const decided = new Promise((r) => { decide = r; });
  const setMode = (mode, user, persist = true) => {
    s.mode = mode; s.user = user || null;
    if (persist) ls.set(MODE_KEY, mode);
    if (user) s.sync = loadSync();
    renderBadge();
    decide();
  };

  // ---- chooser ----
  function chooser(error) {
    modal((p, close) => {
      const msg = el('p', { class: 'acct-err', role: 'alert' }, error ? errText(error) : '');
      const home = () => {
        p.replaceChildren(
          el('h2', {}, L.title), el('p', {}, L.intro), msg,
          el('button', { class: 'acct-btn' + (s.google ? ' primary' : ''), disabled: !s.google, onclick: () => { location.href = '/api/auth/google/start'; } },
            s.google ? L.google : L.googleOff),
          el('button', { class: 'acct-btn' + (s.google ? '' : ' primary'), onclick: () => form(true) }, L.email),
          el('p', { class: 'acct-note' }, '☁ ' + L.cloudNote),
          el('button', { class: 'acct-btn ghost', onclick: () => { close(); setMode('local'); } }, L.local),
          el('p', { class: 'acct-note' }, L.localNote),
          el('a', { class: 'acct-small', href: '/privacy.html', target: '_blank' }, L.privacy),
        );
      };
      const form = (isLogin) => {
        const email = el('input', { type: 'email', placeholder: L.emailPh, autocomplete: 'email', required: true, name: 'email' });
        const pass = el('input', { type: 'password', placeholder: L.passPh, autocomplete: isLogin ? 'current-password' : 'new-password', required: true, minlength: '8', name: 'password' });
        const submit = el('button', { class: 'acct-btn primary', type: 'submit' }, isLogin ? L.signIn : L.register);
        const f = el('form', {
          onsubmit: async (e) => {
            e.preventDefault(); submit.disabled = true; msg.textContent = '';
            try {
              const r = await api(isLogin ? '/api/auth/login' : '/api/auth/register', { method: 'POST', body: { email: email.value, password: pass.value } });
              close(); setMode('cloud', r.user);
            } catch (err) { msg.textContent = errText(err.code); submit.disabled = false; }
          },
        }, email, pass, submit);
        p.replaceChildren(
          el('h2', {}, isLogin ? L.signIn : L.register), msg, f,
          el('button', { class: 'acct-link', onclick: () => form(!isLogin) }, isLogin ? L.noAccount : L.haveAccount),
          el('button', { class: 'acct-link', onclick: home }, '‹ ' + L.back),
        );
        email.focus();
      };
      home();
    });
  }

  // ---- badge + menu ----
  let badge;
  function renderBadge() {
    if (!badge) {
      badge = el('button', { id: 'acct-badge', 'aria-haspopup': 'menu', onclick: (e) => { e.stopPropagation(); toggleMenu(); } });
      document.body.append(badge);
    }
    const name = s.mode === 'cloud' ? '☁ ' + (s.user.email || 'Google') + (s.offline ? ' · ' + L.offline : '') : '⌂ ' + L.localBadge;
    badge.textContent = name + (s.status ? ' · ' + s.status : '');
    badge.dataset.mode = s.mode || '';
  }
  function setStatus(t) { s.status = t; if (badge) renderBadge(); }

  let menu;
  function toggleMenu() {
    if (menu) { menu.remove(); menu = null; return; }
    const item = (label, fn, cls) => el('button', { role: 'menuitem', class: cls || '', onclick: () => { menu.remove(); menu = null; fn(); } }, label);
    const fileIn = el('input', { type: 'file', accept: '.zip,application/zip', hidden: true, onchange: () => importZip(fileIn.files[0]) });
    menu = el('div', { id: 'acct-menu', role: 'menu' },
      el('p', { class: 'acct-note' }, s.mode === 'cloud' ? L.cloudNote : L.localNote),
      s.mode === 'cloud' ? item(L.syncNow, () => syncUp(true)) : null,
      item(L.download, downloadLocal),
      item(L.import, () => fileIn.click()),
      fileIn,
      s.mode === 'cloud' ? item(L.signOut, signOut) : item(L.signInMenu, () => chooser()),
      s.mode === 'cloud' ? item(L.del, deleteAccount, 'danger') : null,
      el('a', { href: '/privacy.html', target: '_blank', role: 'menuitem' }, L.privacy),
    );
    document.body.append(menu);
    const off = (e) => { if (menu && !menu.contains(e.target)) { menu.remove(); menu = null; } removeEventListener('pointerdown', off, true); };
    addEventListener('pointerdown', off, true);
  }

  async function downloadLocal() {
    if (!s.Module) return;
    saveBlob(await writeZip(listSave(s.Module.FS)), `colobot-saves-${stamp()}.zip`);
  }

  async function importZip(file) {
    if (!file || !s.Module) return;
    let entries;
    try { entries = await readZip(new Uint8Array(await file.arrayBuffer())); } catch { toast(errText('bad_zip')); return; }
    if (!confirm(L.importConfirm)) return;
    replaceSave(s.Module.FS, entries);
    await s.Module.__wfsFlushNow();
    if (s.mode === 'cloud') await syncUp(true);
    location.reload();
  }

  async function signOut() {
    try { await syncUp(true); } catch { /* best effort */ }
    try { await api('/api/auth/logout', { method: 'POST', body: {} }); } catch { /* cookie may be gone */ }
    ls.set(MODE_KEY, null);
    location.reload();
  }

  async function deleteAccount() {
    if (!confirm(L.delConfirm)) return;
    try { await api('/api/me', { method: 'DELETE' }); } catch (e) { toast(errText(e.code)); return; }
    ls.set(SYNC_KEY(s.user.id), null); ls.set(MODE_KEY, null);
    location.reload();
  }

  // ---- conflict ----
  function conflict(latest, localEntries) {
    return new Promise((resolve) => {
      modal((p, close) => {
        p.append(
          el('h2', {}, L.conflictT), el('p', {}, L.conflictB),
          el('p', { class: 'acct-note' }, `☁ v${latest.version} · ${latest.device || '?'} · ${new Date(latest.createdAt * 1000).toLocaleString()}`),
          el('div', { class: 'acct-row' },
            el('button', { class: 'acct-link', onclick: async () => saveBlob(new Uint8Array(await (await api('/api/saves/' + latest.version, { raw: true })).arrayBuffer()), `colobot-saves-cloud-v${latest.version}.zip`) }, L.dlCloud),
            el('button', { class: 'acct-link', onclick: async () => saveBlob(await writeZip(localEntries), `colobot-saves-device-${stamp()}.zip`) }, L.dlLocal)),
          el('button', { class: 'acct-btn primary', onclick: () => { close(); resolve('cloud'); } }, L.useCloud),
          el('button', { class: 'acct-btn', onclick: () => { close(); resolve('local'); } }, L.useLocal),
        );
      });
    });
  }

  async function restoreFromCloud(latest) {
    const r = await api('/api/saves/' + latest.version, { raw: true });
    const entries = await readZip(new Uint8Array(await r.arrayBuffer()));
    replaceSave(s.Module.FS, entries);
    storeSync({ base: latest.version, hash: await hashEntries(listSave(s.Module.FS)) });
    await s.Module.__wfsFlushNow();
  }

  // ---- upload ----
  async function upload(entries, hash, base) {
    const zip = await writeZip(entries);
    const r = await api('/api/saves', {
      method: 'PUT', body: zip,
      headers: { 'Content-Type': 'application/zip', 'X-Base-Version': String(base || 0), 'X-Save-Hash': hash, 'X-Device': deviceLabel() },
    });
    storeSync({ base: r.saved.version, hash });
    return r.saved;
  }

  async function syncUp(force) {
    if (s.mode !== 'cloud' || !s.Module) return;
    if (s.uploading) { s.again = true; return s.uploading; }
    s.uploading = (async () => {
      const entries = listSave(s.Module.FS), hash = await hashEntries(entries);
      if (hash === s.sync.hash) { if (force) setStatus(L.synced); return; }
      setStatus(L.syncing);
      try {
        await upload(entries, hash, s.sync.base);
        s.offline = false; setStatus(L.synced);
        setTimeout(() => s.status === L.synced && setStatus(''), 3000);
      } catch (e) {
        if (e.status === 409) {
          const choice = await conflict(e.data.latest, entries);
          if (choice === 'local') { await upload(entries, hash, e.data.latest.version); setStatus(L.synced); }
          else { setStatus(L.restoring); await restoreFromCloud(e.data.latest); location.reload(); }
        } else if (e.status === 401) {
          ls.set(MODE_KEY, null); s.mode = 'local'; setStatus(''); toast(L.expired, { label: L.signIn, fn: () => chooser() });
        } else {
          s.offline = e.code === 'network'; setStatus(''); toast(e.code === 'too_large' ? errText('too_large') : L.syncFail);
        }
      }
    })().finally(() => {
      s.uploading = null;
      if (s.again) { s.again = false; schedule(1000); }
    });
    return s.uploading;
  }
  s.syncNow = () => syncUp(true);                    // for tests and the menu
  const schedule = (ms) => { clearTimeout(s.timer); s.timer = setTimeout(() => syncUp(false), ms); };
  addEventListener('online', () => s.mode === 'cloud' && schedule(1000));
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden' && s.mode === 'cloud' && s.timer) syncUp(false); });

  // ---- hooks ----
  async function onSaveMounted(Module) {
    s.Module = Module;
    await decided;
    if (s.mode !== 'cloud' || s.offline) return;
    const entries = listSave(Module.FS), localHash = await hashEntries(entries);
    let latest;
    try { latest = (await api('/api/saves')).latest; } catch (e) { s.offline = true; renderBadge(); return; }
    const st = s.sync;
    if (!latest) { if (entries.length) schedule(500); return; }
    if (latest.hash === localHash) { storeSync({ base: latest.version, hash: localHash }); return; }
    if (st.base === latest.version) { schedule(500); return; }           // only this device changed
    const unchangedHere = st.hash ? st.hash === localHash : false;
    let choice = entries.length === 0 || unchangedHere ? 'cloud' : await conflict(latest, entries);
    if (choice === 'cloud') {
      setStatus(L.restoring);
      try { await restoreFromCloud(latest); setStatus(''); } catch (e) { setStatus(''); toast(L.syncFail); }
    } else {
      storeSync({ base: latest.version, hash: st.hash || null });
      schedule(500);
    }
  }

  async function onSaveFlushed() {
    if (s.mode === 'cloud') { schedule(5000); return; }
    // Local players: after a real game save (data.sav changed), offer a download, at most every 2 min.
    if (!s.Module || Date.now() - s.lastToast < 120000) return;
    const games = listSave(s.Module.FS).filter((e) => /(^|\/)data\.sav$/.test(e.path));
    if (!games.length) return;
    const h = await hashEntries(games);
    if (s.lastToastHash === null) { s.lastToastHash = h; return; }   // first flush: baseline only
    if (h === s.lastToastHash) return;
    s.lastToastHash = h; s.lastToast = Date.now();
    toast(L.saved, { label: L.dl, fn: downloadLocal });
  }

  // ---- start ----
  (async () => {
    let me = null;
    try { me = await api('/api/me'); } catch { /* offline or no API (static hosting) */ }
    s.google = !!(me && me.google);
    const stored = ls.get(MODE_KEY);
    if (me && me.user) return setMode('cloud', me.user);
    if (!me && stored === 'cloud') { s.offline = true; return setMode('local', null, false); } // retry next visit
    if (stored === 'local' && !authError) return setMode('local');
    if (!me && !authError) return setMode('local', null, false); // API unreachable: play locally, no prompt
    chooser(authError);
  })();

  return {
    decided,
    hooks: { onSaveMounted, onSaveFlushed },
    attach(Module) { s.Module = Module; },
  };
}
