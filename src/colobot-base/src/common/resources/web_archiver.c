/*
 * WP11 Lazy FS: PhysFS archiver backed by manifest.json + HTTP fetch.
 *
 * JS contract (loader, before callMain): Module.colobotFS = {
 *   baseUrl: string,                 // prefix for pack/loose URLs ("" = page-relative)
 *   manifest: object,                // parsed manifest.json (docs/manifest-format.md)
 *   packs: { boot: Uint8Array }      // boot.pak bytes, pre-fetched
 * }
 * stat/exists/enumerate answer synchronously from the manifest; only openRead suspends (JSPI).
 */
#include "common/resources/web_archiver.h"

#include <physfs.h>
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- JS side ---------------- */

EM_JS(int, wfs_js_init, (void), {
    var c = Module.colobotFS;
    if (!c || !c.manifest || !c.manifest.files) return 0;
    c.packs = c.packs || {};
    c.cache = new Map(); c.cacheBytes = 0; c.cacheLimit = 128 * 1024 * 1024;
    var dirs = new Map(); dirs.set("", new Set());
    for (var p in c.manifest.files) {
        var parts = p.split('/');
        var cur = "";
        for (var i = 0; i < parts.length; i++) {
            var next = cur ? cur + '/' + parts[i] : parts[i];
            if (!dirs.has(cur)) dirs.set(cur, new Set());
            dirs.get(cur).add(parts[i]);
            if (i < parts.length - 1 && !dirs.has(next)) dirs.set(next, new Set());
            cur = next;
        }
    }
    c.dirs = dirs;
    c.touch = function(key, val) {
        if (c.cache.has(key)) c.cacheBytes -= c.cache.get(key).byteLength;
        c.cache.delete(key);
        c.cache.set(key, val); c.cacheBytes += val.byteLength;
        while (c.cacheBytes > c.cacheLimit && c.cache.size > 1) {
            var k = c.cache.keys().next().value;
            c.cacheBytes -= c.cache.get(k).byteLength; c.cache.delete(k);
        }
        return val;
    };
    /* WP13: one in-flight fetch per key; get(key) resolves the bytes of 'pack:<name>' or
     * 'file:<path>' (loose). prefetch() warms the cache without blocking the frame loop. */
    c.inflight = new Map();
    c.get = function(key) {
        var hit = c.cache.get(key);
        if (hit) return Promise.resolve(c.touch(key, hit));
        if (c.inflight.has(key)) return c.inflight.get(key);
        var base = c.baseUrl || "";
        var isPack = key.lastIndexOf('pack:', 0) === 0;
        var name = key.slice(5);
        var url = isPack ? c.manifest.packs[name].url : c.manifest.files[name].url;
        var pr = fetch(base + url, c.lowPriority && c.lowPriority.has(key) ? { priority: 'low' } : undefined)
            .then(function(r) { if (!r.ok) throw new Error('HTTP ' + r.status + ' ' + url); return r.arrayBuffer(); })
            .then(function(b) { c.inflight.delete(key); return c.touch(key, new Uint8Array(b)); },
                  function(e) { c.inflight.delete(key); throw e; });
        c.inflight.set(key, pr);
        return pr;
    };
    c.keyOf = function(p) {
        var f = c.manifest.files[p];
        if (!f) return null;
        if (f.pack) return c.packs[f.pack] ? null : 'pack:' + f.pack;
        return 'file:' + p;
    };
    c.prefetch = function(key, low) {
        if (!key || c.cache.has(key)) return;
        if (low) { c.lowPriority = c.lowPriority || new Set(); c.lowPriority.add(key); }
        c.get(key).catch(function(e) { err('web_archiver: prefetch ' + key + ': ' + e); });
    };
    return 1;
})

/* WP13: 1 if path's bytes are resident (no fetch needed), else starts a background fetch and
 * returns 0. -1 if the path is not in the manifest. Never suspends. */
EM_JS(int, wfs_js_ready, (const char* path), {
    var c = Module.colobotFS; var p = UTF8ToString(path);
    if (!c || !c.manifest || !c.manifest.files[p]) return -1;
    var key = c.keyOf(p);
    if (!key || c.cache.has(key)) return 1;
    c.prefetch(key, false);
    return 0;
})

/* 0 = missing, 1 = file, 2 = dir; *size set for files */
EM_JS(int, wfs_js_stat, (const char* path, double* size), {
    var c = Module.colobotFS; var p = UTF8ToString(path);
    var f = c.manifest.files[p];
    if (f) { HEAPF64[size >> 3] = f.size; return 1; }
    return c.dirs.has(p) ? 2 : 0;
})

/* malloc'd '\n'-joined child names, or 0 if not a dir */
EM_JS(char*, wfs_js_list, (const char* path), {
    var d = Module.colobotFS.dirs.get(UTF8ToString(path));
    if (!d || d.size === 0) return 0;
    return stringToNewUTF8(Array.from(d).join('\n'));
})

EM_JS(int, wfs_js_boot, (void** out), {
    var b = Module.colobotFS.packs.boot;
    if (!b) return -1;
    var ptr = _malloc(b.byteLength); HEAPU8.set(b, ptr);
    HEAPU32[out >> 2] = ptr;
    return b.byteLength;
})

/* fetch file bytes into malloc'd buffer; returns size or -1 */
EM_ASYNC_JS(int, wfs_js_fetch, (const char* path, void** out), {
    var c = Module.colobotFS; var p = UTF8ToString(path);
    var f = c.manifest.files[p];
    if (!f) return -1;
    try {
        var bytes;
        if (f.pack && c.packs[f.pack]) {
            bytes = c.packs[f.pack].subarray(f.offset, f.offset + f.size);
        } else if (f.pack) {
            var pk = await c.get('pack:' + f.pack);
            bytes = pk.subarray(f.offset, f.offset + f.size);
        } else {
            bytes = await c.get('file:' + p);
        }
        var ptr = _malloc(Math.max(bytes.byteLength, 1)); HEAPU8.set(bytes, ptr);
        HEAPU32[out >> 2] = ptr;
        return bytes.byteLength;
    } catch (e) {
        err('web_archiver: fetch failed for ' + p + ': ' + e);
        return -1;
    }
})

EM_ASYNC_JS(void, wfs_js_mount_save, (void), {
    if (Module.__wfsSaveMounted) return;
    Module.__wfsSaveMounted = true;
    try { FS.mkdir('/save'); } catch (e) {}
    FS.mount(IDBFS, {}, '/save');
    await new Promise(function(res) { FS.syncfs(true, function(e) { if (e) err('idbfs load: ' + e); res(); }); });
    var flush = function() { FS.syncfs(false, function() {}); };
    addEventListener('pagehide', flush);
    document.addEventListener('visibilitychange', function() { if (document.visibilityState === 'hidden') flush(); });
})

EM_ASYNC_JS(void, wfs_js_sync, (void), {
    await new Promise(function(res) { FS.syncfs(false, function(e) { if (e) err('idbfs sync: ' + e); res(); }); });
})

/* ---------------- memory PHYSFS_Io (refcounted buffer) ---------------- */

typedef struct { unsigned char* buf; PHYSFS_uint64 len, pos; int* refs; } MemIo;

static PHYSFS_Io* mio_make(unsigned char* buf, PHYSFS_uint64 len, int* refs);

static PHYSFS_sint64 mio_read(PHYSFS_Io* io, void* b, PHYSFS_uint64 len)
{
    MemIo* m = (MemIo*)io->opaque;
    PHYSFS_uint64 avail = m->len - m->pos;
    if (len > avail) len = avail;
    memcpy(b, m->buf + m->pos, (size_t)len);
    m->pos += len;
    return (PHYSFS_sint64)len;
}
static PHYSFS_sint64 mio_write(PHYSFS_Io* io, const void* b, PHYSFS_uint64 l)
{
    (void)io; (void)b; (void)l;
    PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
    return -1;
}
static int mio_seek(PHYSFS_Io* io, PHYSFS_uint64 off)
{
    MemIo* m = (MemIo*)io->opaque;
    if (off > m->len) { PHYSFS_setErrorCode(PHYSFS_ERR_PAST_EOF); return 0; }
    m->pos = off;
    return 1;
}
static PHYSFS_sint64 mio_tell(PHYSFS_Io* io) { return (PHYSFS_sint64)((MemIo*)io->opaque)->pos; }
static PHYSFS_sint64 mio_length(PHYSFS_Io* io) { return (PHYSFS_sint64)((MemIo*)io->opaque)->len; }
static int mio_flush(PHYSFS_Io* io) { (void)io; return 1; }
static void mio_destroy(PHYSFS_Io* io)
{
    MemIo* m = (MemIo*)io->opaque;
    if (--(*m->refs) == 0) { free(m->buf); free(m->refs); }
    free(m);
    free(io);
}
static PHYSFS_Io* mio_duplicate(PHYSFS_Io* io)
{
    MemIo* m = (MemIo*)io->opaque;
    (*m->refs)++;
    return mio_make(m->buf, m->len, m->refs);
}
static PHYSFS_Io* mio_make(unsigned char* buf, PHYSFS_uint64 len, int* refs)
{
    PHYSFS_Io* io = (PHYSFS_Io*)calloc(1, sizeof(PHYSFS_Io));
    MemIo* m = (MemIo*)calloc(1, sizeof(MemIo));
    m->buf = buf; m->len = len; m->refs = refs;
    io->version = 0; io->opaque = m;
    io->read = mio_read; io->write = mio_write; io->seek = mio_seek; io->tell = mio_tell;
    io->length = mio_length; io->duplicate = mio_duplicate; io->flush = mio_flush;
    io->destroy = mio_destroy;
    return io;
}

/* ---------------- archiver ---------------- */

static int g_archiveOpaque;

static void* wa_open(PHYSFS_Io* io, const char* name, int forWrite, int* claimed)
{
    (void)io;
    size_t n = strlen(name);
    if (forWrite || n < 13 || strcmp(name + n - 13, "manifest.json") != 0) return NULL;
    *claimed = 1;
    return &g_archiveOpaque;
}

static PHYSFS_EnumerateCallbackResult wa_enumerate(void* opaque, const char* dirname,
    PHYSFS_EnumerateCallback cb, const char* origdir, void* data)
{
    (void)opaque;
    char* list = wfs_js_list(dirname);
    if (!list) return PHYSFS_ENUM_OK;
    PHYSFS_EnumerateCallbackResult r = PHYSFS_ENUM_OK;
    char* s = list;
    while (*s && r == PHYSFS_ENUM_OK)
    {
        char* e = strchr(s, '\n');
        if (e) *e = '\0';
        r = cb(data, origdir, s);
        if (r == PHYSFS_ENUM_ERROR) PHYSFS_setErrorCode(PHYSFS_ERR_APP_CALLBACK);
        if (!e) break;
        s = e + 1;
    }
    free(list);
    return r;
}

static PHYSFS_Io* wa_openRead(void* opaque, const char* fnm)
{
    (void)opaque;
    double size = 0;
    int t = wfs_js_stat(fnm, &size);
    if (t == 0) { PHYSFS_setErrorCode(PHYSFS_ERR_NOT_FOUND); return NULL; }
    if (t == 2) { PHYSFS_setErrorCode(PHYSFS_ERR_NOT_A_FILE); return NULL; }
    void* buf = NULL;
    int len = wfs_js_fetch(fnm, &buf);
    if (len < 0) { PHYSFS_setErrorCode(PHYSFS_ERR_IO); return NULL; }
    int* refs = (int*)malloc(sizeof(int));
    *refs = 1;
    return mio_make((unsigned char*)buf, (PHYSFS_uint64)len, refs);
}

static PHYSFS_Io* wa_openWrite(void* o, const char* f)
{
    (void)o; (void)f;
    PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
    return NULL;
}
static int wa_remove(void* o, const char* f)
{
    (void)o; (void)f;
    PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
    return 0;
}

static int wa_stat(void* opaque, const char* fn, PHYSFS_Stat* st)
{
    (void)opaque;
    double size = 0;
    int t = wfs_js_stat(fn, &size);
    if (t == 0) { PHYSFS_setErrorCode(PHYSFS_ERR_NOT_FOUND); return 0; }
    st->filesize = t == 1 ? (PHYSFS_sint64)size : 0;
    st->modtime = st->createtime = st->accesstime = -1;
    st->filetype = t == 1 ? PHYSFS_FILETYPE_REGULAR : PHYSFS_FILETYPE_DIRECTORY;
    st->readonly = 1;
    return 1;
}

static void wa_close(void* opaque) { (void)opaque; }

static const PHYSFS_Archiver g_webArchiver = {
    0,
    { "JSON", "Colobot web lazy manifest archive", "colobot-web", "", 0 },
    wa_open, wa_enumerate, wa_openRead, wa_openWrite, wa_openWrite,
    wa_remove, wa_remove, wa_stat, wa_close
};

/* ---------------- public ---------------- */

void web_fs_mount_save(void) { wfs_js_mount_save(); }

void web_save_sync(void) { wfs_js_sync(); }

static void free_cb(void* p) { free(p); }

int web_fs_mount_data(void)
{
    if (!wfs_js_init()) return 0;

    /* boot.pak first so its (already resident) entries win over the lazy archiver. */
    void* boot = NULL;
    int bootLen = wfs_js_boot(&boot);
    if (bootLen > 0 && !PHYSFS_mountMemory(boot, (PHYSFS_uint64)bootLen, free_cb, "boot.pak", NULL, 1))
        free(boot);

    static int registered = 0;
    if (!registered)
    {
        if (!PHYSFS_registerArchiver(&g_webArchiver)) return 0;
        registered = 1;
    }
    /* PhysFS opens the path natively before probing archivers, so it must exist in MEMFS. */
    FILE* f = fopen("/manifest.json", "a");
    if (f) fclose(f);
    return PHYSFS_mount("/manifest.json", NULL, 1) != 0;
}

int web_fs_ready(const char* path)
{
    return wfs_js_ready(path);
}
