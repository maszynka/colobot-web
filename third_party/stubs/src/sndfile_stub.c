/* colobot-web TEMPORARY libsndfile stub. TODO(WP2): delete. */
#include <sndfile.h>
#include <stddef.h>
SNDFILE *sf_open_virtual(SF_VIRTUAL_IO *v, int mode, SF_INFO *info, void *u)
{ (void)v; (void)mode; (void)info; (void)u; return NULL; }
int sf_close(SNDFILE *f) { (void)f; return 0; }
sf_count_t sf_read_short(SNDFILE *f, short *p, sf_count_t n) { (void)f; (void)p; (void)n; return 0; }
const char *sf_strerror(SNDFILE *f) { (void)f; return "libsndfile stubbed in web build (TODO WP2)"; }
