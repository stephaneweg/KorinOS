/* termcap.h : ce que rtlib/unix attend de termcap. Le terminal d'ARMPI ne connait aucune sequence :
   tgetent echoue, fb_hInit laisse la console « non initialisee » et PRINT, INPUT passent par stdout
   et stdin ; CLS, LOCATE et COLOR sont sans effet (docs/GCC.md). */
#ifndef _ARMPI_TERMCAP_H
#define _ARMPI_TERMCAP_H

extern char PC, *UP, *BC;
extern short ospeed;

int tgetent (char *buf, const char *name);
int tgetflag (const char *id);
int tgetnum (const char *id);
char *tgetstr (const char *id, char **area);
char *tgoto (const char *cap, int col, int row);
int tputs (const char *str, int affcnt, int (*putc_fn) (int));

#endif
