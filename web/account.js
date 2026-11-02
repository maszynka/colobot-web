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
  otherTab: 'Gra jest otwarta w innej karcie — w tej karcie synchronizacja jest wstrzymana. Zamknij jedną z kart.',
  newerCloud: 'W chmurze są nowsze zapisy.', reload: 'Przeładuj',
  importPending: 'Zaimportowano. Wysyłka do chmury się nie udała — ponowię próbę.',
  ownerT: 'Zapisy z innego konta',
  ownerB: 'Ta przeglądarka ma zapisy ostatnio synchronizowane z innym kontem. Wysłać je na to konto, czy zacząć to konto od nowa? (Zostaną usunięte z tej przeglądarki; tamto konto ma swoją kopię w chmurze.)',
  ownerUpload: 'Wyślij na to konto', ownerFresh: 'Zacznij od nowa',
  signOutB: 'Zostawić zapisy w tej przeglądarce? Na wspólnym komputerze lepiej je usunąć — kopia w chmurze zostaje.',
  keepSaves: 'Wyloguj, zostaw zapisy', removeSaves: 'Wyloguj i usuń zapisy z tej przeglądarki', cancel: 'Anuluj',
  removeUnsynced: 'Najnowszych zapisów nie udało się wysłać. Usunięcie skasuje je na zawsze. Kontynuować?',
  delPass: 'hasło (potwierdzenie)', delBtn: 'Usuń konto',
  errors: {
    reauth_required: 'Dla bezpieczeństwa zaloguj się ponownie przez Google i usuń konto w ciągu 10 minut.',
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
  otherTab: 'The game is open in another tab — cloud sync is paused in this one. Close one of the tabs.',
  newerCloud: 'Newer saves are in the cloud.', reload: 'Reload',
  importPending: 'Imported. The cloud upload failed — it will retry.',
  ownerT: 'Saves from another account',
  ownerB: 'This browser has saves last synced with a different account. Upload them to this account, or start this account fresh? (They are removed from this browser; the other account keeps its cloud copy.)',
  ownerUpload: 'Upload to this account', ownerFresh: 'Start fresh',
  signOutB: 'Keep the saves in this browser? On a shared computer, remove them — your cloud copy stays.',
  keepSaves: 'Sign out, keep saves here', removeSaves: 'Sign out and remove saves from this browser', cancel: 'Cancel',
  removeUnsynced: 'The latest saves could not be uploaded. Removing them now deletes them for good. Continue?',
  delPass: 'password (to confirm)', delBtn: 'Delete account',
  errors: {
    reauth_required: 'For safety, sign in with Google again, then delete within 10 minutes.',
    bad_credentials: 'Wrong email or password.', email_taken: 'That account already exists — sign in instead.',
    bad_email: 'That email address looks wrong.', bad_password: 'The password needs 8–200 characters.',
    too_many_attempts: 'Too many attempts. Try again in a few minutes.',
    google_not_configured: 'Google sign-in is not configured yet.',
    google_denied: 'Google sign-in was cancelled.', google_state: 'The sign-in expired — please try again.',
    google_token: 'Google did not confirm the sign-in — please try again.', too_large: 'Saves are too large (20 MB limit).',
    network: 'Cannot reach the server.', bad_zip: 'That is not a valid saves .zip file.',
  },
};
const errText = (code) => (Object.hasOwn(L.errors, code) ? L.errors[code] : code);

const MODE_KEY = 'colobot.mode';
const SYNC_KEY = (uid) => 'colobot.sync.' + uid;
const USER_KEY = 'colobot.user';      // last signed-in user {id,email,provider}: cloud mode while offline
const OWNER_KEY = 'colobot.owner';    // user id whose saves /save holds (absent: local play / unknown)
// Per-device settings (resolution, key bindings, audio) are not part of the synced saves.
const SETTINGS = 'colobot.json';
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
// Entries under /save, sorted; without colobot.json unless all (downloads keep it).
function listSave(FS, { all = false } = {}) {
  const out = [];
  const walk = (dir) => {
    let names;
    try { names = FS.readdir(dir); } catch { return; }
    for (const n of names.sort()) {
      if (n === '.' || n === '..') continue;
      if (!all && dir === '/save' && n === SETTINGS) continue;
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

// Replaces /save with entries, keeping this device's colobot.json unless entries carry one.
function replaceSave(FS, entries) {
  const rm = (dir) => {
    for (const n of FS.readdir(dir)) {
      if (n === '.' || n === '..') continue;
      if (dir === '/save' && n === SETTINGS) continue;
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
    offline: false,      // cloud mode, but the API is unreachable (retrying)
    pendingReconcile: false, // start-up reconcile with the cloud has not happened yet
    secondary: false,    // another tab holds the save lock: no sync from this tab
    mounted: false,
    Module: null,
    sync: null,          // {base, hash} for the signed-in user
    uploading: null,
    again: false,
    timer: 0,
    retryTimer: 0,
    retryDelay: 0,
    ownFlush: 0,         // flushes started by syncUp itself (their hook call is ignored)
    lastToastHash: null,
    lastToast: 0,
    status: '',
  };
  window.__colobotAccount = s;
  const loadSync = () => { try { return JSON.parse(ls.get(SYNC_KEY(s.user.id))) || {}; } catch { return {}; } };
  const storeSync = (v) => { s.sync = v; ls.set(SYNC_KEY(s.user.id), JSON.stringify(v)); };
  const setOwner = () => ls.set(OWNER_KEY, s.user.id);
  const flush = () => s.Module.__wfsFlushNow();

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
    if (persist) {
      ls.set(MODE_KEY, mode);
      ls.set(USER_KEY, user ? JSON.stringify({ id: user.id, email: user.email, provider: user.provider }) : null);
    }
    if (user) s.sync = loadSync();
    renderBadge();
    decide();
  };
  const dropSession = () => {
    ls.set(MODE_KEY, null); ls.set(USER_KEY, null);
    s.mode = 'local'; s.offline = false; s.pendingReconcile = false;
    clearTimeout(s.retryTimer); setStatus('');
    toast(L.expired, { label: L.signIn, fn: () => chooser() });
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
              // Signed in after /save was mounted (from the menu): reconcile now.
              if (s.mounted) reconcile(true).catch((err) => console.warn('reconcile', err));
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
    const fileIn = el('input', { type: 'file', accept: '.zip,application/zip', hidden: true, onchange: () => importZip(fileIn.files[0]).catch((e) => { console.warn('import', e); toast(L.syncFail); }) });
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
    saveBlob(await writeZip(listSave(s.Module.FS, { all: true })), `colobot-saves-${stamp()}.zip`);
  }

  async function importZip(file) {
    if (!file || !s.Module) return;
    let entries;
    try { entries = await readZip(new Uint8Array(await file.arrayBuffer())); } catch { toast(errText('bad_zip')); return; }
    if (!confirm(L.importConfirm)) return;
    while (s.uploading) await s.uploading;          // an older upload must not race the import
    replaceSave(s.Module.FS, entries);
    await flush();
    if (s.mode === 'cloud') {
      setOwner();                                   // imported on purpose into this account
      const r = await syncUp(true);
      if (r === 'reloading') return;                // a conflict chose "Use cloud": it reloads
      if (r !== 'synced' && r !== 'unchanged') {
        toast(L.importPending, { label: L.reload, fn: () => location.reload() });
        return;
      }
    }
    location.reload();
  }

  // Empties /save (keeps colobot.json) and flushes it to IndexedDB.
  async function clearLocal() {
    replaceSave(s.Module.FS, []);
    await flush();
    ls.set(OWNER_KEY, null);
  }

  function signOut() {
    modal((p, close) => {
      const go = async (remove) => {
        close();
        const r = await syncUp(true).catch(() => 'failed');
        if (remove && s.Module) {
          if (r !== 'synced' && r !== 'unchanged' && !confirm(L.removeUnsynced)) return;
          try { await clearLocal(); } catch (e) { console.warn('clear saves', e); }
        }
        try { await api('/api/auth/logout', { method: 'POST', body: {} }); } catch { /* cookie may be gone */ }
        ls.set(MODE_KEY, null); ls.set(USER_KEY, null);
        location.reload();
      };
      p.append(
        el('h2', {}, L.signOut), el('p', {}, L.signOutB),
        el('button', { class: 'acct-btn primary', onclick: () => go(false) }, L.keepSaves),
        el('button', { class: 'acct-btn', onclick: () => go(true) }, L.removeSaves),
        el('button', { class: 'acct-link', onclick: close }, L.cancel),
      );
    });
  }

  function deleteAccount() {
    modal((p, close) => {
      const msg = el('p', { class: 'acct-err', role: 'alert' });
      const pass = s.user.provider === 'password'
        ? el('input', { type: 'password', placeholder: L.delPass, autocomplete: 'current-password', name: 'password' }) : null;
      const submit = el('button', { class: 'acct-btn danger', type: 'submit' }, L.delBtn);
      const f = el('form', {
        onsubmit: async (e) => {
          e.preventDefault(); submit.disabled = true; msg.textContent = '';
          const body = { confirm: 'delete' };
          if (pass) body.password = pass.value;
          try { await api('/api/me', { method: 'DELETE', body }); } catch (err) {
            submit.disabled = false;
            msg.replaceChildren(errText(err.code));
            if (err.code === 'reauth_required')
              msg.append(' ', el('button', { class: 'acct-link', type: 'button', onclick: () => { location.href = '/api/auth/google/start'; } }, L.google));
            return;
          }
          close();
          ls.set(SYNC_KEY(s.user.id), null); ls.set(MODE_KEY, null); ls.set(USER_KEY, null);
          location.reload();
        },
      }, pass, submit);
      p.append(el('h2', {}, L.del), el('p', {}, L.delConfirm), msg, f, el('button', { class: 'acct-link', onclick: close }, L.cancel));
    });
  }

  // ---- dialogs ----
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

  // /save holds saves last synced with a different account (shared browser): ask before they
  // go into this one. Resolves true to upload; false after starting this account fresh.
  // live: the game already runs on /save, so "start fresh" reloads after clearing it.
  let ownerAsk = null;
  function confirmOwner(entries, live) {
    const owner = ls.get(OWNER_KEY);
    if (!owner || owner === s.user.id || !entries.length) return Promise.resolve(true);
    if (ownerAsk) return ownerAsk;
    ownerAsk = new Promise((resolve) => {
      modal((p, close) => {
        p.append(
          el('h2', {}, L.ownerT), el('p', {}, L.ownerB),
          el('div', { class: 'acct-row' },
            el('button', { class: 'acct-link', onclick: async () => saveBlob(await writeZip(entries), `colobot-saves-device-${stamp()}.zip`) }, L.dlLocal)),
          el('button', { class: 'acct-btn primary', onclick: () => { close(); setOwner(); resolve(true); } }, L.ownerUpload),
          el('button', {
            class: 'acct-btn',
            onclick: async () => {
              close();
              try { await clearLocal(); setOwner(); } catch (e) { console.warn('clear saves', e); }
              if (live) location.reload();
              resolve(false);
            },
          }, L.ownerFresh),
        );
      });
    }).finally(() => { ownerAsk = null; });
    return ownerAsk;
  }

  // Order matters: the files reach IndexedDB first, and only then does the sync state say
  // "this device has version N". A failed or interrupted flush leaves the old state, so the
  // next start sees a conflict instead of uploading stale saves over the newer cloud version.
  async function restoreFromCloud(latest) {
    const r = await api('/api/saves/' + latest.version, { raw: true });
    const entries = (await readZip(new Uint8Array(await r.arrayBuffer()))).filter((e) => e.path !== SETTINGS);
    replaceSave(s.Module.FS, entries);
    await flush();
    storeSync({ base: latest.version, hash: await hashEntries(listSave(s.Module.FS)) });
    setOwner();
  }

  // ---- offline / retry ----
  function markOffline() {
    s.offline = true; renderBadge();
    clearTimeout(s.retryTimer);
    s.retryDelay = Math.min(Math.max(s.retryDelay * 2, 15000), 300000);
    s.retryTimer = setTimeout(() => reconnect().catch((e) => console.warn('reconnect', e)), s.retryDelay);
  }
  async function reconnect() {
    if (s.mode !== 'cloud') return;
    let me;
    try { me = await api('/api/me'); } catch { markOffline(); return; }
    if (!me.user || me.user.id !== s.user.id) { dropSession(); return; }
    s.offline = false; s.retryDelay = 0; clearTimeout(s.retryTimer); renderBadge();
    if (!s.mounted) return;                           // onSaveMounted reconciles
    if (s.pendingReconcile) await reconcile(true); else await syncUp(false);
  }

  // ---- reconcile this device with the cloud ----
  // live = the game is already running on /save (reconnect, sign-in from the menu).
  async function reconcile(live) {
    if (s.mode !== 'cloud' || s.secondary) return;
    let entries = listSave(s.Module.FS);
    const localHash = await hashEntries(entries);
    let latest;
    try { latest = (await api('/api/saves')).latest; } catch (e) {
      if (e.status === 401) { dropSession(); return; }
      s.pendingReconcile = true; markOffline(); return;
    }
    s.pendingReconcile = false; s.offline = false; renderBadge();
    s.sync = loadSync();
    const st = s.sync;
    if (!latest) { if (entries.length && await confirmOwner(entries, live)) schedule(500); return; }
    if (latest.hash === localHash) { storeSync({ base: latest.version, hash: localHash }); setOwner(); return; }
    if (st.base === latest.version) {                                    // only this device changed
      if (await confirmOwner(entries, live)) { schedule(500); return; }
      entries = [];                                                      // started fresh: take the cloud
    }
    const auto = entries.length === 0 || (st.hash ? st.hash === localHash : false);
    if (auto && live) {                                                  // newer cloud, game running: offer a reload
      toast(L.newerCloud, { label: L.reload, fn: () => restoreAndReload(latest) });
      return;
    }
    const choice = auto ? 'cloud' : await conflict(latest, entries);
    if (choice === 'cloud') {
      setStatus(L.restoring);
      try { await restoreFromCloud(latest); setStatus(''); if (live) location.reload(); }
      catch (e) { console.warn('restore', e); setStatus(''); toast(L.syncFail); }
    } else {
      storeSync({ base: latest.version, hash: st.hash || null });
      setOwner();
      schedule(500);
    }
  }
  async function restoreAndReload(latest) {
    setStatus(L.restoring);
    try { await restoreFromCloud(latest); location.reload(); }
    catch (e) { console.warn('restore', e); setStatus(''); toast(L.syncFail); }
  }

  // ---- upload ----
  async function upload(entries, hash, base) {
    const zip = await writeZip(entries);
    const r = await api('/api/saves', {
      method: 'PUT', body: zip,
      headers: { 'Content-Type': 'application/zip', 'X-Base-Version': String(base || 0), 'X-Save-Hash': hash, 'X-Device': deviceLabel() },
    });
    storeSync({ base: r.saved.version, hash });
    setOwner();
    return r.saved;
  }

  // Resolves to 'synced' | 'unchanged' | 'skipped' | 'failed' | 'expired' | 'reloading'; never rejects.
  async function syncUp(force) {
    if (s.mode !== 'cloud' || !s.Module || !s.mounted || s.secondary) return 'skipped';
    if (s.pendingReconcile) { await reconnect().catch(() => {}); return s.pendingReconcile ? 'failed' : 'skipped'; }
    if (s.uploading) { s.again = true; return s.uploading; }
    s.uploading = (async () => {
      // Flush first, so IndexedDB never lags behind what the cloud gets: otherwise a tab killed
      // right after the upload restarts with older files and base = the new version.
      s.ownFlush++;
      try { await flush(); } finally { s.ownFlush--; }
      const entries = listSave(s.Module.FS), hash = await hashEntries(entries);
      s.sync = loadSync();                           // another tab or a restore may have moved it
      if (hash === s.sync.hash) { if (force) setStatus(L.synced); return 'unchanged'; }
      if (!(await confirmOwner(entries, true))) return 'skipped';
      setStatus(L.syncing);
      try {
        await upload(entries, hash, s.sync.base);
        s.offline = false; s.retryDelay = 0; setStatus(L.synced);
        setTimeout(() => s.status === L.synced && setStatus(''), 3000);
        return 'synced';
      } catch (e) {
        if (e.status === 409) {
          const choice = await conflict(e.data.latest, entries);
          if (choice === 'local') { await upload(entries, hash, e.data.latest.version); setStatus(L.synced); return 'synced'; }
          setStatus(L.restoring); await restoreFromCloud(e.data.latest); location.reload(); return 'reloading';
        }
        if (e.status === 401) { dropSession(); return 'expired'; }
        setStatus('');
        if (e.code === 'network') markOffline();
        toast(e.code === 'too_large' ? errText('too_large') : L.syncFail);
        return 'failed';
      }
    })().catch((e) => { console.warn('sync', e); setStatus(''); toast(L.syncFail); return 'failed'; }).finally(() => {
      s.uploading = null;
      if (s.again) { s.again = false; schedule(1000); }
    });
    return s.uploading;
  }
  s.syncNow = () => syncUp(true);                    // for tests and the menu
  const schedule = (ms) => { clearTimeout(s.timer); s.timer = setTimeout(() => { s.timer = 0; syncUp(false); }, ms); };
  addEventListener('online', () => { if (s.mode === 'cloud') reconnect().catch((e) => console.warn('reconnect', e)); });
  document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden' && s.mode === 'cloud' && s.timer) syncUp(false); });

  // One tab owns /save: each tab has its own MEMFS copy and would overwrite the other's files and
  // sync state. The first tab holds a Web Lock until it closes; later tabs do not sync.
  function acquireLock() {
    if (!navigator.locks) return Promise.resolve();
    return new Promise((resolve) => {
      navigator.locks.request('colobot-save', { ifAvailable: true }, (lock) => {
        if (!lock) { s.secondary = true; resolve(); return undefined; }
        resolve();
        return new Promise(() => {});                // held for the life of the tab
      }).catch(() => resolve());
    });
  }

  // ---- hooks ----
  async function onSaveMounted(Module) {
    s.Module = Module;
    await decided;
    await acquireLock();
    s.mounted = true;
    if (s.secondary) { toast(L.otherTab); return; }
    if (s.mode !== 'cloud') return;
    if (s.offline) { s.pendingReconcile = true; return; }   // reconnect() reconciles later
    await reconcile(false);
  }

  async function onSaveFlushed() {
    if (s.ownFlush) return;
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
    let lastUser = null;
    try { lastUser = JSON.parse(ls.get(USER_KEY)); } catch { /* none */ }
    if (me && me.user) return setMode('cloud', me.user);
    if (!me && stored === 'cloud' && lastUser && lastUser.id) {
      // Offline start of a signed-in player: stay in cloud mode (badge "offline"), keep local
      // changes pending and retry; nothing is persisted, so the next visit starts the same way.
      s.offline = true;
      setMode('cloud', lastUser, false);
      markOffline();
      return;
    }
    if (!me && stored === 'cloud') return setMode('local', null, false);  // no known user: local this time
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
