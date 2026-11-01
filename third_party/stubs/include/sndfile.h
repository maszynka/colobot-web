/* colobot-web TEMPORARY stub of libsndfile API surface used by sndfile_wrapper.
 * TODO(WP2): delete; replaced by vorbis_wav_reader (stb_vorbis + WAV parser).
 * Every open fails, so sounds are silently not loaded. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int64_t sf_count_t;
typedef struct SNDFILE_tag SNDFILE;
typedef struct SF_INFO {
    sf_count_t frames;
    int samplerate, channels, format, sections, seekable;
} SF_INFO;
typedef sf_count_t (*sf_vio_get_filelen)(void *user_data);
typedef sf_count_t (*sf_vio_seek)(sf_count_t offset, int whence, void *user_data);
typedef sf_count_t (*sf_vio_read)(void *ptr, sf_count_t count, void *user_data);
typedef sf_count_t (*sf_vio_write)(const void *ptr, sf_count_t count, void *user_data);
typedef sf_count_t (*sf_vio_tell)(void *user_data);
typedef struct SF_VIRTUAL_IO {
    sf_vio_get_filelen get_filelen;
    sf_vio_seek seek;
    sf_vio_read read;
    sf_vio_write write;
    sf_vio_tell tell;
} SF_VIRTUAL_IO;
enum { SFM_READ = 0x10, SFM_WRITE = 0x20, SFM_RDWR = 0x30 };
SNDFILE *sf_open_virtual(SF_VIRTUAL_IO *sfvirtual, int mode, SF_INFO *sfinfo, void *user_data);
int sf_close(SNDFILE *sndfile);
sf_count_t sf_read_short(SNDFILE *sndfile, short *ptr, sf_count_t items);
const char *sf_strerror(SNDFILE *sndfile);
#ifdef __cplusplus
}
#endif
