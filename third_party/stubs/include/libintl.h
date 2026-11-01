/* colobot-web gettext stub: English only for M1/M2 (identity translation).
 * Owner: WP1 (may keep); real i18n deferred to WP13/M3. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
static inline char *gettext(const char *msgid) { return (char *)msgid; }
static inline char *dgettext(const char *d, const char *msgid) { (void)d; return (char *)msgid; }
static inline char *ngettext(const char *s, const char *p, unsigned long n) { return (char *)(n == 1 ? s : p); }
static inline char *textdomain(const char *d) { return (char *)d; }
static inline char *bindtextdomain(const char *d, const char *dir) { (void)d; return (char *)dir; }
static inline char *bind_textdomain_codeset(const char *d, const char *c) { (void)d; return (char *)c; }
#ifdef __cplusplus
}
#endif
