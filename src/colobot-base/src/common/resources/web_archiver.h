/* WP11 Lazy FS for the web build (see docs/manifest-format.md). */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Mount IDBFS at /save and pull persisted data (JSPI-suspending). */
void web_fs_mount_save(void);

/* Mount boot.pak (PHYSFS_mountMemory) and the lazy manifest archiver at "/" in the
 * PhysFS search path. Requires Module.colobotFS from the JS loader. Nonzero on success. */
int web_fs_mount_data(void);

/* Flush /save (IDBFS) to IndexedDB (JSPI-suspending). Call after a save/config write. */
void web_save_sync(void);

/* Nonblocking: 1 if the data file at path (PhysFS path) is resident, 0 if not yet (a background
 * fetch is started), -1 if it is not in the manifest. */
int web_fs_ready(const char* path);

#ifdef __cplusplus
}
#endif
