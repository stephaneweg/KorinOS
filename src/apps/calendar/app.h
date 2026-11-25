//
// app.h -- what the Calendar's files share: UIKit, the theme's tokens, the clock, the files, the text helpers.
// The port of Onyx's user/Apps/calendar: model.h is Onyx's, the views and the dialogs are UIKit's (ui_view, ui_dialog_*).
//
#ifndef _calendar_app_h
#define _calendar_app_h

#include <armpi/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <uikit.h>

// les données de l'utilisateur : HOME:/.settings/calendar/ (plus /apps/calendar.app, écrit par le système ; les
// fichiers de l'ancienne place sont repris à la première lecture : armpi/appdata.h)
#include <armpi/appdata.h>
static inline const char *cal_file (const char *f)
{
	static char b[4][200];
	static int i;
	char *p = b[i++ & 3];
	return korin_app_data ("calendar", f, p, 200, 1);
}
#define CAL_ICS		cal_file ("calendar.ics")
#define AGENDA_TXT	cal_file ("agenda.txt")
#define REMIND_TXT	cal_file ("reminders.txt")

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (data/lang-en.txt : le source est en francais)

// ---- system calls (src/inc/syscalls.inc) ---------------------------------------------------------------------
static inline long sys_call (long n, long a = 0, long b = 0, long c = 0)
{
	return armpi_syscall (n, a, b, c, 0, 0, 0);	// (Pi : svc 0 ; PC : syscall)
}
enum { SYS_TIME_MS_ = 0x19, SYS_CLOCK_GET_ = 0x1B, SYS_SVC_OPEN_ = 0x31, SYS_SVC_CALL_ = 0x32 };
static unsigned ticks_ms () { return (unsigned) sys_call (SYS_TIME_MS_); }

// ---- the theme: UIKit's tokens, never theme.txt ---------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
#define C_BG	jet (J_FACE)
#define C_FIELD	jet (J_SURFACE)
#define C_TEXT	jet (J_TEXTE1)
#define C_ACCENT jet (J_ACCENT)
static unsigned mix (unsigned a, unsigned b, int n) { return (unsigned) ui_mix (a, b, n); }
static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)

// ---- text in a view (style 2: bold) ---------------------------------------------------------------------------
static const int PX = 13;
static long font_of (int style) { return style == 2 ? (long) ui.font_b : (long) ui.font; }
static int text_w (const char *s, int style = 0) { return (int) ui_text_width ((long) s, PX, font_of (style)); }
static int text_wn (const char *s, int n, int style = 0) { return (int) ui_text_prefix ((long) s, n, PX, font_of (style)); }
// a line in a box of height h, centred on it (ui_text takes the baseline)
static void text_l (int x, int y, int h, const char *s, unsigned c, int style = 0) { ui_text ((long) s, x, y + h / 2 + PX * 37 / 100, c, PX, font_of (style), 0); }
static void text_c (int x, int y, int w, int h, const char *s, unsigned c, int style = 0) { text_l (x + (w - text_w (s, style)) / 2, y, h, s, c, style); }
static void text_fit (int x, int y, int w, int h, const char *s, unsigned c, int style = 0)
{
	if (w < 8) return;
	ui_text_fit ((long) s, x, y + h / 2 + PX * 37 / 100, c, PX, font_of (style), w);
}
static void disc (int x, int y, int s, unsigned c) { ui_vp_disc ((x * 2 + s) * 8, (y * 2 + s) * 8, s * 8, c); }
static void rbox (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r > h / 2 ? h / 2 : r, c, c); }

// ---- files ----------------------------------------------------------------------------------------------------
// A whole file, ended by a 0 (new[]; 0: no such file); len: its size
static char *cal_read_file (const char *path, int *len)
{
	int f = open (path, O_RDONLY);
	if (f < 0) return 0;
	off_t sz = lseek (f, 0, SEEK_END);
	lseek (f, 0, SEEK_SET);
	char *b = new char[sz + 1];
	int got = 0;
	while (got < sz) { int r = read (f, b + got, sz - got); if (r <= 0) break; got += r; }
	close (f);
	b[got] = '\0';
	if (len) *len = got;
	return b;
}
static bool save_file (const char *path, const char *b, int n)
{
	int f = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f < 0) return false;
	int put = 0;
	while (put < n) { int r = write (f, b + put, n - put); if (r <= 0) break; put += r; }
	close (f);
	return put == n;
}

#endif
