//
// fmtracker -- FM Tracker : le tracker FM d'Onyx, porté en C++ sur les kits de Korin. 8 canaux
// d'instruments FM à 2 opérateurs (la synthèse FM d'AudioKit) sur une grille de tranches de temps ;
// lit et écrit les .FMS de FM Song et les instruments .FMI (fms.h).
//
// D'où vient le code : le modèle, les formats, les règles d'édition et de lecture sont ceux d'Onyx
// (user/Apps/fmtracker : fms.h, main.cpp) ; la fenêtre, ses dessins, ses touches et ce que Korin a
// ajouté (afficheur à deux lignes en une, barres d'outils de UIKit, panneau dessiné, éditeur
// d'instrument en entiers, dialogue des préréglages, langues) suivent l'asm de Korin
// (fmtracker.asm, fmt_song.inc, fmt_edit.inc, fmt_view.inc, fmt_ins.inc, fmt_tables.inc), qui fait
// foi pour ce qui se voit. Les écarts sont dans README.md. Un seul source pour le Pi et le PC.
//
//   * La fenêtre : barre de transport (lecture, depuis le début, stop, boucle du motif, position et
//     temps, annuler / rétablir, couper / copier / coller, suivre), à gauche les motifs du morceau
//     (nouveau, dupliquer, monter, descendre, supprimer ; lignes et vitesse du motif ; octave et pas),
//     la grille, un piano dessous.
//   * Une colonne par canal ; son en-tête : l'instrument (un clic ouvre son éditeur), M (muet dans ce
//     motif ; clic droit aussi), S (solo), un niveau. Une ligne = une tranche de temps (vitesse / 20 s).
//   * Une case : une note commence (« C#4 »), « --- » : la note continue, vide : silence. Une note
//     dure jusqu'à la note ou au silence suivant de son canal.
//   * Touches : C D E F G A B une note (Maj : dièse ; le curseur descend de « pas » lignes), 0-7
//     l'octave, Espace un silence, Suppr « --- » (un bloc choisi : effacé), Retour arrière efface la
//     tranche du dessus, # le dièse, Ctrl+haut / bas un demi-ton (Maj : une octave), flèches, Page,
//     Début, Fin, Maj + celles-ci un bloc, Tab le canal suivant. Ctrl+X C V le bloc, Ctrl+A tout le
//     motif, Ctrl+Z Y annuler / rétablir. Clic : une case, glisser : un bloc ; molette et barre de
//     défilement. Clic sur le piano : cette note.
//   * Ctrl+P joue depuis le curseur et suit la position ; Échap arrête. Un morceau est une liste de
//     motifs (chacun sa longueur, sa vitesse), joués dans l'ordre.
//   * Fichier > Exporter en WAV : le morceau tel qu'AudioKit le joue (ak_dec_open lit le FM Song).
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <armpi/syscall.h>
#include <uikit.h>
#include <audiokit_abi.h>
#include "fms.h"

#define SYS_TIME_MS	0x19

// --- le format, la lecture (fmtracker.asm) ---------------------------------------------------
#define UNDO_MAX	48
#define FILE_MAX	(140 * 1024)		// un .FMS au plus (64 x 256 lignes)
#define ROW_MS		50			// une ligne : vitesse * 50 ms
#define PREVIEW_MS	350
#define TEST_MS		700
#define TEST_VOICE	15
#define SYNTH_VOICES	16
#define PCM_FRAMES	4096
#define MAX_PRESETS	96
#define PRESET_SIZE	13

// --- la fenêtre -------------------------------------------------------------------------------
#define WIN_W0		980
#define WIN_H0		660
#define WIN_MIN_W	760
#define WIN_MIN_H	440
#define TOOL_H		50
#define SIDE_W		156
#define ST_H		22
#define PIANO_H		58
#define HEAD_H		46
#define GRID_Y		(TOOL_H + HEAD_H)
#define SB_W		14
#define NUM_W		40
#define ROW_H		17
#define LCD_X		146
#define LCD_W		226
#define LCD_H		34
#define T2_X		(LCD_X + LCD_W + 4)
#define LIST_Y		(TOOL_H + 30)
#define LIST_ROW	24
#define SIDE_BELOW	300			// le panneau sous la liste
#define PIANO_KEYS	(8 * 7)			// touches blanches
#define PIANO_KH	(PIANO_H - 16)
#define PIANO_BH	(PIANO_KH * 6 / 10)
#define PLAY_COL	0x0048B068
#define SD_ROWS		24			// décalages depuis g_side_y (panneau)
#define SD_SPEED	56
#define SD_HTYPE	98
#define SD_OCT		122
#define SD_STEP		154
#define SD_HSONG	196
#define SD_TITLE	216

#define MODS_CTRL	0x11
#define MODS_SHIFT	0x22

// --- ce qui est à repeindre (flush) -----------------------------------------------------------
enum { D_GRID = 1, D_HEAD = 2, D_LIST = 4, D_PIANO = 8, D_STATUS = 16, D_LCD = 32, D_BOXES = 64,
       D_SCROLL = 128, D_SIDE = 256, D_ALL = 512, D_TITLE = 1024 };
#define D_SONG		(D_GRID | D_HEAD | D_LIST | D_PIANO | D_LCD)

// --- les commandes (menus, boutons) -----------------------------------------------------------
enum {
	ID_NEW = 1, ID_OPEN = 2, ID_SAVE = 3, ID_SAVEAS = 4, ID_EXPORT = 5, ID_INFO = 6,
	ID_UNDO = 10, ID_REDO = 11, ID_CUT = 12, ID_COPY = 13, ID_PASTE = 14, ID_ALL = 15, ID_INSROW = 16,
	ID_DELROW = 17, ID_CLRCH = 18, ID_UP1 = 19, ID_DOWN1 = 20, ID_UP12 = 21, ID_DOWN12 = 22,
	ID_PREV = 30, ID_NEXT = 31, ID_NEWPAT = 32, ID_DUPPAT = 33, ID_PATUP = 34, ID_PATDOWN = 35, ID_DELPAT = 36,
	ID_INSTR = 40, ID_MUTE = 41, ID_SOLO = 42,
	ID_PLAY = 50, ID_START = 51, ID_LOOP = 52, ID_FOLLOW = 53, ID_STOP = 54,
	W_ROWS = 60, W_SPEED = 61, W_OCT = 62, W_STEP = 63		// les nombres du panneau
};

static struct ui_vars ui;
#define TR(s)	((const char *) ui_tr ((long) (s)))
static long ev (int off) { return ui_ev_field (&ui, off); }
static long now_ms (void) { return armpi_syscall (SYS_TIME_MS, 0, 0, 0, 0, 0, 0); }

// --- l'état -----------------------------------------------------------------------------------
static FmsSong g_song;
static long g_fm;
static long g_w = WIN_W0, g_h = WIN_H0;
static long g_col_w = 96, g_gw, g_gh, g_vis = 1, g_kw = 1, g_kx, g_lvis = 1, g_list_h, g_ltop, g_side_y;
static long g_redraw, g_in_dlg;
static long g_pat, g_row, g_ch, g_top, g_oct = 4, g_step = 1;
static long g_sel, g_ar, g_ac;				// un bloc choisi : de cette case au curseur
static long g_modified, g_playing, g_loop, g_follow = 1, g_solo = -1;
static long g_play_pat, g_play_row, g_shown_row = -1;
static unsigned long g_next_tick, g_pv_end, g_vu_tick;
static long g_pv_voice = -1;
static long g_clip_r, g_clip_c;
static long g_mx, g_my, g_drag, g_down_ch, g_hot_head = -1, g_hot_list = -1;	// g_drag 1 : la grille, 2 : un nom de canal
static long g_sb_rows, g_sb_vis, g_sb_top;
static long g_font_note, g_cell_tw = 24;
static long g_vu[FMS_CH];
static unsigned char g_snd[FMS_CH];			// la note qui sonne sur chaque canal
static long w_t1, w_t2, w_lcd, w_play, w_loop, w_follow, w_rows, w_speed, w_oct, w_step, w_sb, w_side[5];
static unsigned char g_akins[AKFI_SIZE + 8];
static char g_status[512], g_path[260], g_dlgpath[260], g_dirbuf[260], g_tmp[512], g_menu[WS_MENU_MAX + 16];
static unsigned char g_clip[FMS_CH * FMS_MAXROWS];	// g_clip[canal * g_clip_r + ligne]
static unsigned char g_file[FILE_MAX + 16];
static short g_pcm[PCM_FRAMES * 2];

static const unsigned ch_col[FMS_CH] = { 0x00C98A36, 0x00BC5069, 0x005E9F58, 0x00C85C40,
					 0x008E6CC4, 0x004A9FB0, 0x00B5A040, 0x007484C8 };

static const char s_app[] = "FM Tracker";
static const char s_lang_dir[] = "SD0:/apps/fmtracker.app/lang/";
static const char s_piano_fmi[] = "SD0:/apps/fmtracker.app/ins/PIANO.FMI";
static const char s_ins_dir[] = "SD0:/apps/fmtracker.app/ins";
static const char s_song_dir[] = "SD0:/home/music/fms";
static const char s_tmp_dir[] = "SD0:/tmp";
static const char s_tmp_fms[] = "SD0:/tmp/fmtracker-export.fms";
static const char s_help[] = "C D E F G A B: a note (Shift: #), 0-7: octave, Space: silence, Del: ---, Shift+arrows: a block, Ctrl+P: play";

static void mark (long bits) { g_redraw |= bits; }
static FmsPattern &pat_at (long p) { return g_song.pat[p]; }
static FmsPattern &cur_pat (void) { return g_song.pat[g_pat]; }
static unsigned char &cell (FmsPattern &p, long c, long r) { return p.n[c * p.rows + r]; }

// déclarés plus loin
static void flush (void);
static void edit_instrument (long ch);
static void op_info (void);
static void list_show (void);
static void op_save (void);

// ---- petites aides (AppKit de l'asm) ----------------------------------------------------------

// fmt de fmt_view.inc : les %d remplacés par a puis b, le reste copié
static void fmt (char *d, const char *f, long a, long b = 0)
{
	long v[2] = { a, b };
	int k = 0;
	for (; *f; f++)
		if (f[0] == '%' && f[1] == 'd') { d += sprintf (d, "%ld", v[k]); k = 1; f++; }
		else *d++ = *f;
	*d = 0;
}

// au moins deux chiffres -> la fin
static char *put_2d (char *d, long v)
{
	if (v < 10) *d++ = '0';
	return d + sprintf (d, "%ld", v);
}

// le dernier composant d'un chemin
static const char *base_name (const char *p)
{
	const char *r = p;
	for (; *p; p++) if (*p == '/') r = p + 1;
	return r;
}

// path_parent d'AppKit : coupé au dernier « / » (« VOL:/x » garde le « / » de la racine)
static void path_parent (char *p)
{
	char *s = 0;
	for (char *q = p; *q; q++) if (*q == '/') s = q;
	if (!s || s == p) return;
	if (s[-1] == ':') s++;
	*s = 0;
}

// str_copy de l'asm : copie bornée, terminée par NUL
static void str_copy (char *d, const char *s, long cap)
{
	long i = 0;
	if (s) for (; i < cap - 1 && s[i]; i++) d[i] = s[i];
	d[i] = 0;
}

// ---- le morceau et ses fichiers (fmt_song.inc) ----------------------------------------------

// -> octets lus (< 0 : erreur)
static long read_file (const char *path, void *buf, long max)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return -2;
	long n = 0;
	while (n < max)
	{
		long r = read (fd, (char *) buf + n, max - n);
		if (r <= 0) break;
		n += r;
	}
	close (fd);
	return n;
}

// -> 0 / < 0
static long write_file (const char *path, const void *buf, long n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return -2;
	long k = 0;
	while (k < n)
	{
		long r = write (fd, (const char *) buf + k, n - k);
		if (r <= 0) { close (fd); return -5; }
		k += r;
	}
	close (fd);
	return 0;
}

// -> 1 si lu
static bool read_fmi (const char *path, FmsIns *in)
{
	static char b[1028];
	long n = read_file (path, b, 1023);
	if (n <= 0) return false;
	b[n] = 0;
	return fmi_parse (b, in);
}

// morceau neuf (song_new de l'asm : fms_new d'Onyx, les anciens motifs libérés)
static void song_new (void)
{
	fms_clear (&g_song);
	fms_new (&g_song);
}

// ---- audio (fmt_edit.inc) --------------------------------------------------------------------

// le son de l'instrument d'un canal au synthé
static void upload (long ch)
{
	ak_fm_from_fms ((long) g_akins, (long) g_song.ins[ch].p);
	ak_fm_instrument (g_fm, ch, (long) g_akins);
}
static void upload_all (void) { for (long c = 0; c < FMS_CH; c++) upload (c); }

static void note_on (long voice, unsigned char v)
{
	if (voice < FMS_CH) { g_vu[voice] = 100; g_snd[voice] = v; mark (D_PIANO | D_HEAD); }
	ak_fm_start (g_fm, voice, fms_note_mhz (v), AKW_FM, 220);
}
// voix -1 : toutes
static void note_off (long voice)
{
	if (voice < 0) { memset (g_snd, 0, sizeof g_snd); mark (D_PIANO | D_HEAD); }
	else if (voice < FMS_CH && g_snd[voice]) { g_snd[voice] = 0; mark (D_PIANO | D_HEAD); }
	ak_fm_stop (g_fm, voice);
}
// réduit au silence : muet dans ce motif, ou un autre en solo
static bool silenced (long c) { return cur_pat ().mute[c] || (g_solo >= 0 && g_solo != c); }
// entendre la note saisie
static void preview (long ch, unsigned char v)
{
	if (g_playing || silenced (ch)) return;
	note_on (ch, v);
	g_pv_voice = ch;
	g_pv_end = now_ms () + PREVIEW_MS;
}

// ---- état ------------------------------------------------------------------------------------
static void set_status (const char *a, const char *b = 0)
{
	snprintf (g_status, sizeof g_status, "%s%s", a, b ? b : "");
	mark (D_STATUS);
}
static void status_tr (const char *s) { set_status (TR (s)); }
static void set_modified (void) { if (!g_modified) { g_modified = 1; mark (D_TITLE); } }

// ---- annulation : les notes du motif avant chaque changement (Onyx) --------------------------
struct Snap { long pat, rows, speed; unsigned char mute[FMS_CH]; unsigned char *n; };
static Snap g_undo[UNDO_MAX], g_redo[UNDO_MAX];
static long g_nundo, g_nredo;

static void snap_push (Snap *st, long &n)
{
	if (n >= UNDO_MAX) { delete [] st[0].n; memmove (st, st + 1, (UNDO_MAX - 1) * sizeof (Snap)); n--; }	// plein : le plus ancien s'en va
	FmsPattern &p = cur_pat ();
	Snap &s = st[n++];
	s.pat = g_pat; s.rows = p.rows; s.speed = p.speed;
	memcpy (s.mute, p.mute, FMS_CH);
	s.n = new unsigned char[FMS_CH * p.rows];
	memcpy (s.n, p.n, (size_t) FMS_CH * p.rows);
}
static void snap_clear (Snap *st, long &n) { for (long i = 0; i < n; i++) delete [] st[i].n; n = 0; }
static void undo_clear (void) { snap_clear (g_undo, g_nundo); snap_clear (g_redo, g_nredo); }	// (la liste des motifs a changé)
static void undo_push (void) { snap_clear (g_redo, g_nredo); snap_push (g_undo, g_nundo); set_modified (); }
static void refresh_pattern (void);
static void snap_back (Snap *from, long &nf, Snap *to, long &nt, const char *empty)
{
	if (!nf) { status_tr (empty); return; }
	Snap s = from[--nf];
	if (s.pat >= g_song.npat) { delete [] s.n; return; }
	g_pat = s.pat;
	snap_push (to, nt);
	FmsPattern &p = cur_pat ();
	delete [] p.n;
	p.n = s.n; p.rows = (int) s.rows; p.speed = (int) s.speed; memcpy (p.mute, s.mute, FMS_CH);
	set_modified ();
	g_sel = 0;
	refresh_pattern ();
}
static void op_undo (void) { snap_back (g_undo, g_nundo, g_redo, g_nredo, "Nothing to undo."); }
static void op_redo (void) { snap_back (g_redo, g_nredo, g_undo, g_nundo, "Nothing to redo."); }

// ---- la vue ----------------------------------------------------------------------------------

// g_top borné par la longueur du motif
static void sync_scroll (void)
{
	long mx = cur_pat ().rows - g_vis;
	if (mx < 0) mx = 0;
	if (g_top > mx) g_top = mx;
	if (g_top < 0) g_top = 0;
	mark (D_SCROLL | D_GRID);
}
static void ensure_visible (long row, bool center)
{
	if (center) g_top = row - (long) ((unsigned long) g_vis >> 1);
	else if (row < g_top) g_top = row;
	else if (row >= g_top + g_vis) g_top = row - g_vis + 1;
	sync_scroll ();
}
// le motif courant a changé : numéros, curseur, vue
static void refresh_pattern (void)
{
	long rows = cur_pat ().rows;
	if ((unsigned long) g_row >= (unsigned long) rows) g_row = rows - 1;
	if ((unsigned long) g_ar >= (unsigned long) rows) g_sel = 0;
	mark (D_BOXES | D_SONG);
	list_show ();
	sync_scroll ();
}
// le bloc (la case du curseur sans bloc)
static void sel_rect (long &r0, long &r1, long &c0, long &c1)
{
	r0 = r1 = g_row; c0 = c1 = g_ch;
	if (!g_sel) return;
	if (g_ar < r0) r0 = g_ar; else r1 = g_ar;
	if (g_ac < c0) c0 = g_ac; else c1 = g_ac;
}
static void move_to (long row, long ch)
{
	long rows = cur_pat ().rows;
	if (row < 0) row = 0;
	if (row >= rows) row = rows - 1;
	if (ch < 0) ch = FMS_CH - 1;
	if (ch >= FMS_CH) ch = 0;
	g_row = row; g_ch = ch;
	ensure_visible (row, false);
	mark (D_GRID | D_HEAD | D_LCD);
}
static void set_cell (unsigned char v, long advance)
{
	undo_push ();
	cell (cur_pat (), g_ch, g_row) = v;
	g_sel = 0;
	move_to (g_row + advance, g_ch);
}
// une note tapée ou cliquée sur le piano
static void enter_note (int note, int sharp, int oct)
{
	unsigned char v = fms_make_note (note, sharp, oct);
	preview (g_ch, v);
	set_cell (v, g_step);
}

// ---- le bloc : effacer, couper, copier, coller, transposer ----------------------------------

// efface le bloc (sans annulation : voir op_clear_block)
static void block_zero (void)
{
	long r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	FmsPattern &p = cur_pat ();
	for (long c = c0; c <= c1; c++) for (long r = r0; r <= r1; r++) cell (p, c, r) = FMS_CONT;
	mark (D_GRID);
}
static void op_clear_block (void) { undo_push (); block_zero (); }
static void op_copy (void)
{
	FmsPattern &p = cur_pat ();
	long r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	g_clip_r = r1 - r0 + 1; g_clip_c = c1 - c0 + 1;
	for (long c = 0; c < g_clip_c; c++) for (long r = 0; r < g_clip_r; r++) g_clip[c * g_clip_r + r] = cell (p, c0 + c, r0 + r);
	fmt (g_tmp, TR ("Copied: %d rows x %d channels."), g_clip_r, g_clip_c);
	set_status (g_tmp);
}
static void op_cut (void) { op_copy (); op_clear_block (); }
// au curseur (le coin du bloc s'il y en a un)
static void op_paste (void)
{
	if (!g_clip_r) { status_tr ("Nothing to paste: copy a block first."); return; }
	long r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	undo_push ();
	FmsPattern &p = cur_pat ();
	for (long c = 0; c < g_clip_c && c0 + c < FMS_CH; c++)
		for (long r = 0; r < g_clip_r && r0 + r < p.rows; r++) cell (p, c0 + c, r0 + r) = g_clip[c * g_clip_r + r];
	mark (D_GRID);
}
static void op_select_all (void)
{
	g_row = cur_pat ().rows - 1; g_ch = FMS_CH - 1; g_ar = 0; g_ac = 0; g_sel = 1;
	mark (D_GRID | D_HEAD | D_LCD);
}
// les notes du bloc (celle du curseur sans bloc)
static void transpose (int delta)
{
	FmsPattern &p = cur_pat ();
	long r0, r1, c0, c1; sel_rect (r0, r1, c0, c1);
	bool any = false;
	for (long c = c0; c <= c1 && !any; c++) for (long r = r0; r <= r1; r++) { unsigned char v = cell (p, c, r); if ((v & 7) && !(v & 128)) { any = true; break; } }
	if (!any) return;
	undo_push ();
	for (long c = c0; c <= c1; c++)
		for (long r = r0; r <= r1; r++) cell (p, c, r) = fms_transpose (cell (p, c, r), delta);
	if (!g_sel) preview (g_ch, cell (p, g_ch, g_row));
	mark (D_GRID);
}
static void op_insert_row (void)
{
	undo_push ();
	FmsPattern &p = cur_pat ();
	unsigned char *n = p.n + g_ch * p.rows;
	for (long r = p.rows - 1; r > g_row; r--) n[r] = n[r - 1];
	n[g_row] = FMS_CONT;
	mark (D_GRID);
}
static void op_delete_row (void)
{
	undo_push ();
	FmsPattern &p = cur_pat ();
	unsigned char *n = p.n + g_ch * p.rows;
	for (long r = g_row; r < p.rows - 1; r++) n[r] = n[r + 1];
	n[p.rows - 1] = FMS_CONT;
	mark (D_GRID);
}
static void op_clear_channel (void)
{
	undo_push ();
	FmsPattern &p = cur_pat ();
	for (long r = 0; r < p.rows; r++) cell (p, g_ch, r) = FMS_CONT;
	mark (D_GRID);
}

// ---- motifs ----------------------------------------------------------------------------------
static void op_stop (void);
// un motif après le courant (sa copie si duplicate)
static void add_pattern (bool duplicate)
{
	if (g_song.npat >= FMS_MAXPAT) { status_tr ("At most 64 patterns."); return; }
	op_stop (); undo_clear ();
	for (long i = g_song.npat; i > g_pat + 1; i--) g_song.pat[i] = g_song.pat[i - 1];	// les suivants reculent
	FmsPattern &src = g_song.pat[g_pat], &np = g_song.pat[g_pat + 1];
	fms_pattern_alloc (&np, src.rows, src.speed);
	if (duplicate) { memcpy (np.n, src.n, (size_t) FMS_CH * src.rows); memcpy (np.mute, src.mute, FMS_CH); }
	g_song.npat++; g_pat++; g_row = 0; g_top = 0; g_sel = 0;
	set_modified ();
	refresh_pattern ();
}
static void op_delete_pattern (void)
{
	if (g_song.npat <= 1) { status_tr ("A song keeps at least one pattern."); return; }
	op_stop ();
	if (!ui_message ((long) TR ("Pattern"), (long) TR ("Delete this pattern?"), UM_YESNO)) return;
	undo_clear ();
	delete [] g_song.pat[g_pat].n;
	for (long i = g_pat; i + 1 < g_song.npat; i++) g_song.pat[i] = g_song.pat[i + 1];
	g_song.pat[g_song.npat - 1].n = 0;
	g_song.npat--;
	if (g_pat >= g_song.npat) g_pat = g_song.npat - 1;
	g_sel = 0;
	set_modified ();
	refresh_pattern ();
}
// plus tôt / plus tard dans le morceau
static void move_pattern (long d)
{
	long to = g_pat + d;
	if (to < 0 || to >= g_song.npat) return;
	op_stop (); undo_clear ();
	FmsPattern t = g_song.pat[g_pat]; g_song.pat[g_pat] = g_song.pat[to]; g_song.pat[to] = t;
	g_pat = to;
	set_modified ();
	refresh_pattern ();
}
static void op_prev_pattern (void) { if (g_pat > 0) { g_pat--; g_sel = 0; refresh_pattern (); } }
static void op_next_pattern (void) { if (g_pat + 1 < g_song.npat) { g_pat++; g_sel = 0; refresh_pattern (); } }
// muet dans ce motif (ou plus)
static void mute_channel (long c)
{
	FmsPattern &p = cur_pat ();
	p.mute[c] ^= 1;
	set_modified ();
	if (p.mute[c]) note_off (c);
	mark (D_GRID | D_HEAD);
}
// solo (ou plus de solo) ; en lecture, ceux qui se taisent maintenant s'arrêtent (comme l'asm, aussi
// depuis le menu)
static void solo_channel (long c)
{
	g_solo = g_solo == c ? -1 : c;
	if (g_playing) for (long k = 0; k < FMS_CH; k++) if (silenced (k)) note_off (k);
	mark (D_GRID | D_HEAD);
}

// ---- lecture ---------------------------------------------------------------------------------
static void play_row (void)
{
	FmsPattern &p = g_song.pat[g_play_pat];
	for (long c = 0; c < FMS_CH; c++)
	{
		unsigned char v = cell (p, c, g_play_row);
		if (v & 128) note_off (c);
		else if ((v & 7) == 0) continue;
		else if (p.mute[c] || (g_solo >= 0 && g_solo != c)) note_off (c);
		else note_on (c, v);
	}
}
static void op_stop (void)
{
	if (!g_playing) return;
	g_playing = 0;
	note_off (-1);
	mark (D_SONG | D_BOXES);
	status_tr ("Stopped.");
}
static void start_play (long patn, long row)
{
	upload_all ();
	g_playing = 1; g_play_pat = patn; g_play_row = row; g_shown_row = row;
	g_next_tick = now_ms ();
	mark (D_BOXES);
	status_tr (g_loop ? "Playing this pattern in a loop -- Esc or Stop to stop." : "Playing -- Esc or Stop to stop.");
}
static void op_play (void) { if (g_playing) op_stop (); else start_play (g_pat, g_row); }
static void op_play_start (void) { op_stop (); start_play (g_loop ? g_pat : 0, 0); }
static void op_loop (void)
{
	g_loop ^= 1;
	ui_menu_state (ID_LOOP, g_loop ? UMS_CHECKED : 0);
	status_tr (g_loop ? "Loop: the pattern plays again and again." : "Loop off: the patterns play in order.");
	mark (D_BOXES);
}
static void op_follow (void)
{
	g_follow ^= 1;
	ui_menu_state (ID_FOLLOW, g_follow ? UMS_CHECKED : 0);
	mark (D_BOXES);
}

// à chaque tour de boucle : fin des notes d'essai, vumètres, lecture
static void tick (void)
{
	unsigned long now = (unsigned long) now_ms ();
	if (g_pv_voice >= 0 && now >= g_pv_end) { note_off (g_pv_voice); g_pv_voice = -1; }
	unsigned long dt = now - g_vu_tick;			// (ms) les vumètres : une note retombe à un plancher
	if (dt >= 30)						// tant qu'elle sonne, à rien une fois arrêtée
	{
		g_vu_tick = now;
		if (dt > 200) dt = 200;
		long cs = (long) (dt / 10);
		bool changed = false;
		for (int c = 0; c < FMS_CH; c++)
		{
			long v = g_vu[c], floor_ = g_snd[c] ? 35 : 0, rate = g_snd[c] ? 2 : 6;
			if (v > floor_) { v -= rate * cs; if (v < floor_) v = floor_; g_vu[c] = v; changed = true; }
		}
		if (changed) mark (D_HEAD);
	}
	if (!g_playing) return;
	int guard = 0;						// 8 lignes au plus d'un coup
	while (g_playing && now >= g_next_tick && guard < 8)
	{
		guard++;
		if (g_play_row >= g_song.pat[g_play_pat].rows)	// fin du motif
		{
			g_play_row = 0;
			if (!g_loop) { g_play_pat++; if (g_play_pat >= g_song.npat) g_play_pat = 0; }
		}
		play_row ();
		if (g_follow)					// suivre : le motif joué, sa ligne au centre
		{
			if (g_play_pat != g_pat) { g_pat = g_play_pat; g_sel = 0; refresh_pattern (); }
			ensure_visible (g_play_row, true);
		}
		g_next_tick += (unsigned long) g_song.pat[g_play_pat].speed * ROW_MS;
		g_shown_row = g_play_row++;
		mark (D_SONG);
	}
	long late = (long) (now - g_next_tick);			// en retard : on recale
	if (late > 500) g_next_tick = now;
}

// ---- commandes du morceau --------------------------------------------------------------------

// la barre d'état : le fichier, modifié ou non
static void title_status (void)
{
	set_status (g_path[0] ? g_path : TR ("Untitled"), g_modified ? TR ("  (modified)") : 0);
	mark (D_TITLE | D_SIDE);
}
// -> false si l'on renonce (le morceau modifié est gardé ou non : question)
static bool confirm_discard (void)
{
	if (!g_modified) return true;
	op_stop ();
	long r = ui_message ((long) TR (s_app), (long) TR ("The song has changed. Save it first?"), UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { op_save (); return !g_modified; }
	return true;
}
static void after_load (void)
{
	g_pat = 0; g_row = 0; g_ch = 0; g_top = 0; g_sel = 0; g_solo = -1;
	undo_clear ();
	upload_all ();
	g_modified = 0;
	refresh_pattern ();
	title_status ();
}
// le nouveau morceau du début : les instruments de PIANO.FMI si on les trouve
static void song_defaults (void)
{
	song_new ();
	FmsIns in;
	if (read_fmi (s_piano_fmi, &in)) for (int c = 0; c < FMS_CH; c++) g_song.ins[c] = in;
}
static void op_new (void)
{
	if (!confirm_discard ()) return;
	op_stop ();
	song_defaults ();
	g_path[0] = 0;
	after_load ();
}
// -> true si le morceau est chargé
static bool load_song (const char *path)
{
	long n = read_file (path, g_file, FILE_MAX);
	if (n <= 0) { snprintf (g_tmp, sizeof g_tmp, "%s %s", TR ("Cannot open"), path); set_status (g_tmp); return false; }
	op_stop ();
	static FmsSong s;
	s.npat = 0;
	if (!fms_parse (g_file, (int) n, &s))
	{
		fms_clear (&s);
		ui_message ((long) TR (s_app), (long) TR ("This is not an FM Song (.FMS) file."), UM_OK);
		return false;
	}
	fms_clear (&g_song);
	g_song = s;						// (les motifs passent à g_song)
	for (int i = 0; i < FMS_MAXPAT; i++) s.pat[i].n = 0;
	s.npat = 0;
	char p[260];
	str_copy (p, path, 256);				// (path peut être g_dlgpath ou g_path)
	strcpy (g_path, p);
	after_load ();
	return true;
}
// -> le dossier de départ des dialogues
static const char *song_dir (void)
{
	if (!g_path[0]) return s_song_dir;
	str_copy (g_dirbuf, g_path, 256);
	path_parent (g_dirbuf);
	return g_dirbuf;
}
static void op_open (void)
{
	if (!confirm_discard ()) return;
	op_stop ();
	if (ui_file_open ((long) song_dir (), (long) g_dlgpath, 256)) load_song (g_dlgpath);
}
static long write_song (const char *path)
{
	long n = fms_write (&g_song, g_file);
	return write_file (path, g_file, n);
}
// une extension ajoutée au chemin (tampon de 256 octets) s'il n'en a pas
static void ensure_ext (char *p, const char *ext)
{
	size_t n = strlen (p);
	if (n > 4 && p[n - 4] == '.') return;
	str_copy (p + n, ext, 5);
}
// « <message> <chemin> » en barre d'état
static void status_path (const char *msg, const char *path)
{
	snprintf (g_tmp, sizeof g_tmp, "%s %s", TR (msg), path);
	set_status (g_tmp);
}
static void op_save_as (void)
{
	op_stop ();
	const char *dir = song_dir ();
	const char *name = g_path[0] ? base_name (g_path) : "SONG.FMS";
	if (!ui_file_save ((long) dir, (long) name, (long) g_dlgpath, 256)) return;
	ensure_ext (g_dlgpath, ".FMS");
	if (write_song (g_dlgpath) < 0) { status_path ("Cannot save", g_dlgpath); return; }
	str_copy (g_path, g_dlgpath, 256);
	g_modified = 0;
	title_status ();
}
static void op_save (void)
{
	if (!g_path[0]) { op_save_as (); return; }
	if (write_song (g_path) < 0) { status_path ("Cannot save", g_path); return; }
	g_modified = 0;
	title_status ();
}
// Fichier > Exporter en WAV : le morceau tel qu'AudioKit le joue (ak_dec_open lit le FM Song,
// enregistré pour l'occasion dans un fichier du moment) ; la synthèse est celle du kit, pas de l'app
static void op_export_wav (void)
{
	op_stop ();
	char name[64];						// le nom proposé
	str_copy (name, g_path[0] ? base_name (g_path) : "SONG.FMS", 64);
	size_t n = strlen (name);
	if (n > 4 && name[n - 4] == '.') n -= 4;
	str_copy (name + n, ".WAV", 5);
	if (!ui_file_save ((long) song_dir (), (long) name, (long) g_dlgpath, 256)) return;
	ensure_ext (g_dlgpath, ".WAV");
	mkdir (s_tmp_dir, 0755);
	if (write_song (s_tmp_fms) < 0) { status_path ("Cannot export", g_dlgpath); return; }
	status_path ("Exporting", g_dlgpath);
	flush ();
	long d = ak_dec_open ((long) s_tmp_fms);
	if (d < 0) { unlink (s_tmp_fms); status_path ("Cannot export", g_dlgpath); return; }
	long info[AKI_SIZE / 8];
	ak_dec_info (d, (long) info);
	long e = ak_enc_open ((long) g_dlgpath, AKF_WAV, info[AKI_RATE / 8], info[AKI_CHANNELS / 8], 0);
	if (e < 0) { ak_dec_close (d); unlink (s_tmp_fms); status_path ("Cannot export", g_dlgpath); return; }
	long r = 0;
	for (;;)
	{
		long k = ak_dec_read (d, (long) g_pcm, PCM_FRAMES);
		if (k <= 0) break;
		r = ak_enc_write (e, (long) g_pcm, k);
		if (r < 0) break;
	}
	if (r < 0) { ak_enc_close (e); ak_dec_close (d); unlink (s_tmp_fms); status_path ("Cannot export", g_dlgpath); return; }
	r = ak_enc_close (e);
	ak_dec_close (d);
	unlink (s_tmp_fms);
	status_path (r < 0 ? "Cannot export" : "Exported", g_dlgpath);
}

// un id de commande (menu, raccourci, bouton)
static void on_command (long id)
{
	switch (id)
	{
	case ID_NEW: op_new (); break;
	case ID_OPEN: op_open (); break;
	case ID_SAVE: op_save (); break;
	case ID_SAVEAS: op_save_as (); break;
	case ID_EXPORT: op_export_wav (); break;
	case ID_INFO: op_info (); break;
	case ID_UNDO: op_undo (); break;
	case ID_REDO: op_redo (); break;
	case ID_CUT: op_cut (); break;
	case ID_COPY: op_copy (); break;
	case ID_PASTE: op_paste (); break;
	case ID_ALL: op_select_all (); break;
	case ID_INSROW: op_insert_row (); break;
	case ID_DELROW: op_delete_row (); break;
	case ID_CLRCH: op_clear_channel (); break;
	case ID_UP1: transpose (1); break;
	case ID_DOWN1: transpose (-1); break;
	case ID_UP12: transpose (12); break;
	case ID_DOWN12: transpose (-12); break;
	case ID_PREV: op_prev_pattern (); break;
	case ID_NEXT: op_next_pattern (); break;
	case ID_NEWPAT: add_pattern (false); break;
	case ID_DUPPAT: add_pattern (true); break;
	case ID_PATUP: move_pattern (-1); break;
	case ID_PATDOWN: move_pattern (1); break;
	case ID_DELPAT: op_delete_pattern (); break;
	case ID_INSTR: edit_instrument (g_ch); break;
	case ID_MUTE: mute_channel (g_ch); break;
	case ID_SOLO: solo_channel (g_ch); break;
	case ID_PLAY: op_play (); break;
	case ID_START: op_play_start (); break;
	case ID_LOOP: op_loop (); break;
	case ID_FOLLOW: op_follow (); break;
	case ID_STOP: op_stop (); break;
	}
}

#include "view.h"
#include "ins.h"

// ---- la fenêtre (fmtracker.asm) --------------------------------------------------------------

// le dictionnaire du programme : SD0:/apps/fmtracker.app/lang/<langue>.txt
static void dico_load (void)
{
	char p[96];
	snprintf (p, sizeof p, "%s%s.txt", s_lang_dir, (const char *) ui_lang ());
	ui_tr_load ((long) p);
}

// la taille de départ : celle d'Onyx, moins si l'écran est plus petit
static void win_size (void)
{
	long s[4] = { 0, 0, 0, 0 };
	ui_screen ((long) s);
	long w = WIN_W0, h = WIN_H0;
	if (s[0])
	{
		if (s[0] - 12 < w) w = s[0] - 12;
		long hh = s[1] - s[2] - s[3] - (FRAME_TITLE_H + 8);
		if (hh < h) h = hh;
		if (w < WIN_MIN_W) w = WIN_MIN_W;
		if (h < WIN_MIN_H) h = WIN_MIN_H;
	}
	g_w = w; g_h = h;
}

// le texte des menus, libellés traduits (« M<titre> », « I<id>\t<libellé>[\t<raccourci>] », « - »)
static const char menus_src[] =
	"MFile\n"
	"I1\tNew\tCtrl+N\n"
	"I2\tOpen...\tCtrl+O\n"
	"I3\tSave\tCtrl+S\n"
	"I4\tSave As...\n"
	"I5\tExport WAV...\n"
	"-\n"
	"I6\tSong Info...\n"
	"MEdit\n"
	"I10\tUndo\tCtrl+Z\n"
	"I11\tRedo\tCtrl+Y\n"
	"-\n"
	"I12\tCut\tCtrl+X\n"
	"I13\tCopy\tCtrl+C\n"
	"I14\tPaste\tCtrl+V\n"
	"I15\tSelect the Pattern\tCtrl+A\n"
	"-\n"
	"I16\tInsert Slice\tCtrl+E\n"
	"I17\tDelete Slice\tCtrl+D\n"
	"I18\tClear Channel\n"
	"-\n"
	"I19\tSemitone Up\n"
	"I20\tSemitone Down\n"
	"I21\tOctave Up\n"
	"I22\tOctave Down\n"
	"MPattern\n"
	"I30\tPrevious\tCtrl+B\n"
	"I31\tNext\tCtrl+F\n"
	"-\n"
	"I32\tNew Pattern\n"
	"I33\tDuplicate\n"
	"I34\tMove Earlier\n"
	"I35\tMove Later\n"
	"I36\tDelete...\n"
	"MChannel\n"
	"I40\tInstrument...\n"
	"I41\tMute in this Pattern\n"
	"I42\tSolo\n"
	"MPlay\n"
	"I50\tPlay / Stop\tCtrl+P\n"
	"I51\tFrom the Start\n"
	"I52\tLoop the Pattern\tCtrl+L\n"
	"I53\tFollow\n"
	"I54\tStop\n";

static void menu_build (void)
{
	const char *s = menus_src;
	char *d = g_menu;
	while (*s)
	{
		if (*s == 'M' || *s == 'I')
		{
			if (*s == 'M') *d++ = *s++;
			else while ((*d++ = *s++) != '\t') {}	// id, tabulation
			char lab[128]; int k = 0;
			while (*s && *s != '\t' && *s != '\n' && k < 127) lab[k++] = *s++;
			lab[k] = 0;
			for (const char *t = TR (lab); *t; t++) *d++ = *t;
		}
		while (*s && (*d++ = *s++) != '\n') {}		// le reste de la ligne tel quel
	}
	*d = 0;
}

// les boutons des barres d'outils : glyphe, libellé, drapeaux, id, infobulle, widget à garder
struct Tool { const char *glyph, *label; long flags, id; const char *tip; long *var; };
static const Tool tools_1[] = {
	{ "\xE2\x96\xB6", 0, UTB_TOGGLE, ID_PLAY, "Play from the cursor / stop (Ctrl+P)", &w_play },
	{ "\xE2\x87\xA4", 0, 0, ID_START, "Play from the start", 0 },
	{ "\xE2\x96\xA0", 0, 0, ID_STOP, "Stop (Esc)", 0 },
	{ "\xE2\x86\xBB", 0, UTB_TOGGLE, ID_LOOP, "Loop: play this pattern again and again (Ctrl+L)", &w_loop },
};
static const Tool tools_2[] = {
	{ "\xE2\x86\xB6", 0, 0, ID_UNDO, "Undo (Ctrl+Z)", 0 },
	{ "\xE2\x86\xB7", 0, 0, ID_REDO, "Redo (Ctrl+Y)", 0 },
	{ 0, "Cut", UTB_SEP, ID_CUT, "Cut the block (Ctrl+X)", 0 },
	{ 0, "Copy", 0, ID_COPY, "Copy the block (Ctrl+C)", 0 },
	{ 0, "Paste", 0, ID_PASTE, "Paste at the cursor (Ctrl+V)", 0 },
	{ 0, "Follow", UTB_SEP | UTB_TOGGLE, ID_FOLLOW, "Follow the position while it plays", &w_follow },
};

static long on_widget (long w, long code);

static void tools_add (long bar, const Tool *t, int n)
{
	for (int i = 0; i < n; i++, t++)
	{
		long b = ui_toolbar_add (bar, (long) t->glyph, t->label ? (long) TR (t->label) : 0, t->flags);
		if (t->var) *t->var = b;
		ui_w_on (b, (long) on_widget, t->id);
		ui_w_set_range (b, 0, 1, 0, 0);			// (valeur 0 / 1 des boutons à bascule)
		ui_w_tip (b, (long) TR (t->tip));
	}
}

// petit bouton du panneau
static void side_button (const char *glyph, long id, const char *tip, int rank)
{
	long b = ui_pushbutton (0, 0, 27, 26, (long) glyph, UB_GHOST);
	w_side[rank] = b;
	ui_w_on (b, (long) on_widget, id);
	ui_w_tip (b, (long) TR (tip));
}

// un nombre du panneau
static long side_numeric (long lo, long hi, long v, long id, const char *tip)
{
	long w = ui_numeric (0, 0, 80, 28, lo, hi, v);
	ui_w_on (w, (long) on_widget, id);
	if (tip) ui_w_tip (w, (long) TR (tip));
	return w;
}

static long on_scrollbar (long w, long code);

static void make_widgets (void)
{
	w_t1 = ui_toolbar (0, 0, LCD_X - 4, TOOL_H);			// transport
	tools_add (w_t1, tools_1, 4);
	w_lcd = ui_lcd (LCD_X, (TOOL_H - LCD_H) / 2, LCD_W, LCD_H, (long) "01:000 0:00.0");	// position, temps
	ui_w_tip (w_lcd, (long) TR ("PATTERN : ROW"));
	w_t2 = ui_toolbar (T2_X, 0, WIN_W0 - T2_X, TOOL_H);		// édition
	tools_add (w_t2, tools_2, 6);
	side_button ("+", ID_NEWPAT, "A new pattern after this one", 0);	// panneau : les motifs
	side_button ("\xE2\x8A\x9E", ID_DUPPAT, "Duplicate this pattern", 1);
	side_button ("\xE2\x96\xB2", ID_PATUP, "Move it earlier in the song", 2);
	side_button ("\xE2\x96\xBC", ID_PATDOWN, "Move it later in the song", 3);
	side_button ("\xE2\x9C\x95", ID_DELPAT, "Delete this pattern", 4);
	w_rows = side_numeric (1, FMS_MAXROWS, 64, W_ROWS, 0);		// ce motif
	w_speed = side_numeric (1, 40, 3, W_SPEED, "A row lasts speed / 20 s");
	w_oct = side_numeric (0, 7, g_oct, W_OCT, 0);			// saisie
	w_step = side_numeric (0, 16, g_step, W_STEP, "The rows the cursor goes down after a note");
	w_sb = ui_scrollbar (0, 0, SB_W, 100, 0);			// barre de défilement
	ui_w_on (w_sb, (long) on_scrollbar, 0);
}

// la fenêtre change de taille : tout se replace
static void layout (void)
{
	g_col_w = (long) ((unsigned long) (g_w - (SIDE_W + SB_W + NUM_W)) >> 3);	// colonnes
	g_gw = g_col_w * 8 + NUM_W;
	g_gh = g_h - (ST_H + PIANO_H + GRID_Y);
	g_vis = (long) ((unsigned long) g_gh / ROW_H);
	long pw = g_w - SIDE_W;						// piano
	g_kw = (long) ((unsigned long) (pw - 16) / PIANO_KEYS);
	g_kx = (long) ((unsigned long) (pw - g_kw * PIANO_KEYS) >> 1) + SIDE_W;
	long lh = g_h - (TOOL_H + ST_H + 30 + SIDE_BELOW);		// liste des motifs
	if (lh < LIST_ROW * 2) lh = LIST_ROW * 2;
	g_lvis = (long) ((unsigned long) lh / LIST_ROW);
	g_list_h = g_lvis * LIST_ROW;
	long y = g_list_h + LIST_Y + 8;					// y des petits boutons
	for (int i = 0; i < 5; i++) ui_w_move (w_side[i], 29 * i + 6, y, 27, 26);
	g_side_y = y + 40;
	ui_w_move (w_rows, 60, g_side_y + SD_ROWS + 2, 90, 24);
	ui_w_move (w_speed, 60, g_side_y + SD_SPEED + 2, 90, 24);
	ui_w_move (w_oct, 60, g_side_y + SD_OCT + 2, 90, 24);
	ui_w_move (w_step, 60, g_side_y + SD_STEP + 2, 90, 24);
	ui_w_move (w_t2, T2_X, 0, g_w - T2_X, TOOL_H);
	ui_w_move (w_sb, g_gw + SIDE_W, GRID_Y, SB_W, g_gh);
	g_sb_rows = 0;
	ensure_visible (g_row, false);
	list_show ();
}

// fonction de ui_on_resize
static long on_resize (long w, long h)
{
	g_w = w; g_h = h;
	layout ();
	mark (D_ALL);
	return 0;
}

// fonction des widgets de la fenêtre
static long on_widget (long w, long code)
{
	long id = ui_w_user (w);
	if ((unsigned long) id < W_ROWS)
	{
		if (code != UN_ACTIVATE) return 0;
		on_command (id);
		ui_w_focus (0);
		return 0;
	}
	if (code != UN_CHANGE) return 0;
	long v = ui_w_get_value (w);
	if (id == W_ROWS) { undo_push (); fms_pattern_resize (&cur_pat (), (int) v); refresh_pattern (); }
	else if (id == W_SPEED) { undo_push (); cur_pat ().speed = (int) v; mark (D_LCD); }
	else if (id == W_OCT) { g_oct = v; mark (D_PIANO); }
	else g_step = v;
	if (ev (EV_TYPE) == WEV_MOUSE) ui_w_focus (0);		// à la souris : le clavier revient à la grille
	return 0;
}

// -> true si quelque chose bouge (lecture, note d'essai, niveaux)
static bool busy (void)
{
	if (g_playing || g_pv_voice >= 0) return true;
	for (int c = 0; c < FMS_CH; c++) if (g_vu[c]) return true;
	return false;
}

int main (int argc, char **argv)
{
	char args[260] = "";					// arguments : le fichier à ouvrir (la ligne de ui_arg)
	for (int i = 1; i < argc; i++)
	{
		if (i > 1) strncat (args, " ", sizeof args - strlen (args) - 1);
		strncat (args, argv[i], sizeof args - strlen (args) - 1);
	}
	if (!uikit_open (&ui) || !audiokit_kit_open ()) return 1;
	dico_load ();
	sin_init ();
	colors_init ();
	win_size ();
	if (ui_window_ex (-1, -1, g_w, g_h, (long) s_app, WF_RESIZABLE) < 0) return 0;
	g_fm = ak_fm_new (SYNTH_VOICES);				// la synthèse, jouée en direct
	if (g_fm < 0) return 0;
	ak_fm_live (g_fm, 0);
	ui_min_size (WIN_MIN_W, WIN_MIN_H);
	ui_on_resize ((long) on_resize, 0);
	menu_build ();
	ui_menu ((long) g_menu);
	ui_menu_state (ID_FOLLOW, UMS_CHECKED);
	g_font_note = ui_font_get (UFN_MONOBOLD);			// la grille : mono, notes en gras
	g_cell_tw = ui_text_width ((long) "C#4", 13, (long) ui.font_m);
	make_widgets ();
	layout ();
	song_defaults ();
	after_load ();
	status_tr (s_help);
	if (args[0])
	{
		str_copy (g_dlgpath, args, 256);
		load_song (g_dlgpath);
	}
	mark (D_ALL);
	for (;;)
	{
		tick ();
		flush ();
		long t = ui_event (busy () ? 8 : 200);		// (attente courte quand ça bouge)
		if (!t) { ui_w_idle (); continue; }
		long code, taken;
		if (t == WEV_CLOSE)
		{
			if (confirm_discard ()) break;
		}
		else if (t == WEV_MENU) on_command (ev (EV_CODE));
		else if (t == WEV_KEY)
		{
			if (!ev (EV_KIND)) continue;
			long id = ui_menu_key (0);
			if (id >= 0) { on_command (id); continue; }
			if (ui_w_get_focus ())
			{
				uikit_w_event (&code, &taken);
				if (taken) continue;
			}
			grid_key ();
		}
		else if (t == WEV_MOUSE)
		{
			g_mx = ev (EV_X); g_my = ev (EV_Y);
			if (!g_drag)
			{
				uikit_w_event (&code, &taken);
				if (taken) continue;
			}
			on_mouse ();
		}
		else if (t == WEV_WHEEL)
		{
			uikit_w_event (&code, &taken);
			if (taken) continue;
			on_wheel (ev (EV_Y));
		}
		else if (t == WEV_DROP)
		{
			uikit_w_event (&code, &taken);
			long kind = 0, n = uikit_drop_get (g_dlgpath, 255, &kind);	// un fichier déposé : ouvert
			if (kind != UDT_FILES || n <= 0) continue;
			g_dlgpath[n] = 0;
			char *nl = strchr (g_dlgpath, '\n');
			if (nl) *nl = 0;
			if (confirm_discard ()) load_song (g_dlgpath);
		}
		else uikit_w_event (&code, &taken);
	}
	ak_fm_live (g_fm, -1);
	return 0;
}
