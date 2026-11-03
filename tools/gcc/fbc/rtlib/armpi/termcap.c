/* termcap : pas de base de terminaux sous ARMPI (termcap.h) */

#include "termcap.h"

char PC, *UP, *BC;
short ospeed;

int tgetent (char *buf, const char *name) { (void) buf; (void) name; return 0; }
int tgetflag (const char *id) { (void) id; return 0; }
int tgetnum (const char *id) { (void) id; return -1; }
char *tgetstr (const char *id, char **area) { (void) id; (void) area; return 0; }
char *tgoto (const char *cap, int col, int row) { (void) cap; (void) col; (void) row; return 0; }
int tputs (const char *str, int affcnt, int (*putc_fn) (int)) { (void) str; (void) affcnt; (void) putc_fn; return 0; }
