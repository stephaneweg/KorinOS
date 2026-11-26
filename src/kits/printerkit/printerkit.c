/* PrinterKit : l'impression (docs/IMPRESSION.md ; printerkit d'Onyx), en C
 * commun au Pi et au PC ; remplace printerkit.asm, même table
 * (printerkit.abi), mêmes octets dans le fichier des pages, mêmes messages au
 * service printd, même dialogue.
 *
 * Une application imprime en quatre temps : le dialogue, un travail, ses
 * pages, la fin. Les pages sont ENREGISTRÉES dans la file de printd
 * (HOME:/.cache/print/<id>.opj, E72) ; le service en fait un PDF, ou ce que
 * l'imprimante réseau prend. Tout le savoir (imprimantes, papiers, polices,
 * IPP) est chez printd : ici, le fichier des pages, les appels au service et
 * le dialogue.
 *
 * Le fichier : « OPJ2 », puis des enregistrements : genre et longueur de la
 * charge (32 bits chacun), la charge ; petit-boutiste, longueurs en millièmes
 * de point :
 *   PJR_FONT     police, octets d'un TrueType
 *   PJR_FONTREF  police, style à fabriquer, chemin du fichier, 0
 *   PJR_PAGE     l, h   ... le dessin de la page ... PJR_ENDPAGE
 *   PJR_RECT     x, y, l, h, couleur
 *   PJR_GLYPH    police, style, taille, x, base, glyphe, caractère, couleur
 *   PJR_TEXT     police, style, taille, x, base, couleur, octets, UTF-8
 *   PJR_IMAGE    x, y, l, h, largeur, hauteur, genre, données
 *   PJR_PATH     couleur, épaisseur (0 : rempli), n, n x (verbe, x, y)
 *   PJR_END      nombre de pages
 *
 * Un seul travail à la fois par process : son état est dans les variables du
 * kit (privées au process). Le « travail » rendu par print_begin est
 * l'adresse de la copie du réglage dans le kit ; les fonctions ne vérifient
 * que qu'il n'est pas nul, comme l'asm.
 *
 * Toutes les variables de l'asm faisaient 64 bits (dd de FASMARM) ; les
 * tampons du dialogue et du message sont dans une seule structure, dans
 * l'ordre de l'asm : un débordement (titre du dialogue, voir print_dialog)
 * tombe sur le même voisin. */
#include "korin_kit.h"

#include "appkit_import.h"
#include "archivekit_import.h"
#include "uikit_import.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

#define EXPORT __attribute__((visibility("hidden"), noinline))

/* ---- constantes (armpi.inc, uikit.inc, archivekit.inc, printerkit.inc, printd.inc) */

#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define PROT_READ 1
#define PROT_WRITE 2
#define E_NOENT (-2)

#define UN_CHANGE 1
#define UB_DEFAULT 1
#define ULB_BOLD 1
#define ULB_DIM 2
#define AR_ZLIB 1

#define PRINT_NORMAL 4
#define PRINT_PORTRAIT 0
#define PRINT_IMG_ALPHA 1
#define PRINT_DLG_OWN_PAPER 1

#define PS_SIZE 0
#define PS_PRINTER 4
#define PS_MEDIA 68
#define PS_PAPER_W 132
#define PS_PAPER_H 136
#define PS_ORIENTATION 156
#define PS_COPIES 160
#define PS_MONO 164
#define PS_QUALITY 168
#define PS_FROM 172
#define PS_TO 176
#define PS_OUTPUT 180
#define PS_BYTES 500

#define PDI_SIZE 0
#define PDI_TITLE 8
#define PDI_PAGES 16
#define PDI_CURRENT 20
#define PDI_FLAGS 24
#define PDI_BYTES 40

#define PR_KIND 64
#define PR_MODEL 232
#define PR_COLOR 296
#define PR_COPIES 300
#define PR_QUALITY 312
#define PR_MEDIA_DEFAULT 316
#define PR_BYTES 380
#define PF_BYTES 164
#define PJI_BYTES 256

#define WM_OP 0
#define WM_A 8
#define WM_B 16
#define WM_C 24
#define WM_D 32
#define WM_DATA 48

#define PD_NEW 1
#define PD_SUBMIT 2
#define PD_ABORT 3
#define PD_SETUP 4
#define PD_PRINTER 5
#define PD_MEDIA 6
#define PD_ADD 7
#define PD_REMOVE 8
#define PD_DEFAULT 9
#define PD_REFRESH 10
#define PD_SCAN 11
#define PD_FOUND 12
#define PD_JOB 13
#define PD_CANCEL 14
#define PD_FORGET 15
#define PD_FONT 16
#define PD_MEASURE 17
#define PD_METRICS 18

#define PJR_FONT 1
#define PJR_PAGE 2
#define PJR_RECT 3
#define PJR_GLYPH 4
#define PJR_TEXT 5
#define PJR_IMAGE 6
#define PJR_PATH 7
#define PJR_ENDPAGE 8
#define PJR_END 9
#define PJR_FONTREF 10

#define IMG_DEFLATE 0 /* genres d'une image : 0xAARRGGBB compressés (zlib) */
#define IMG_JPEG 1

#define V_MOVE 0 /* verbes d'un tracé */
#define V_LINE 1
#define V_CURVE 2
#define V_CLOSE 3

#define PJ_BUF 0x10000 /* tampon du fichier */
#define PJ_NPTS 8192   /* points d'un tracé */
#define PJ_NFONT 32
#define PT_SIZE 12
#define MSG_TEXT 456 /* texte par message (mesure) */

/* le dialogue */
#define PD_DW 430
#define PD_LX 18
#define PD_CX 132
#define PD_CW 278
#define PD_ROW 36
#define PD_NPR 12
#define PD_NMEDIA 40
#define RD_CANCEL 0
#define RD_PRINT 1
#define RD_PRINTER 2

/* ---- chaînes (comme l'asm) */

static const char s_svc[] = "print";
static const char s_bin[] = "/sys/printd";
static const char s_magic[4] = {'O', 'P', 'J', '2'};
static const char s_doc[] = "Document";
static const char s_sans[] = "DejaVu Sans";
static const char s_pdf[] = "pdf";
static const char s_nosvc[] = "le service d'impression ne repond pas";
static const char s_printer[] = "Imprimante";
static const char s_pages[] = "Pages";
static const char s_all[] = "Toutes";
static const char s_current[] = "Page courante";
static const char s_from[] = "De";
static const char s_to[] = "à";
static const char s_copies[] = "Exemplaires";
static const char s_colour[] = "Couleur";
static const char s_colours[] = "Couleur|Noir et blanc";
static const char s_quality[] = "Qualité";
static const char s_paper[] = "Papier";
static const char s_orient[] = "Orientation";
static const char s_orients[] = "Portrait|Paysage";
static const char s_print[] = "Imprimer";
static const char s_cancel[] = "Annuler";
static const char s_dash[] = " - ";
static const char s_ext[] = ".pdf";
static const char s_docs[] = "HOME:/documents";
static const char q_names[3][16] = {"Brouillon", "Normale", "Haute"};

static const char kits_msg[] = "kit absent ou trop ancien : /lib/";

/* ---- variables (segment inscriptible de l'asm) */

struct pk_font {
	long key; /* clé de printd, -1 : données de l'application */
	long synth;
	long data;
};

static long pk_kits_ok;
static long pj_open, pj_fd, pj_id, pj_n, pj_on, pj_seen, pj_pages, pj_nfont, pj_npts, pj_imgf;
static long pd_npr, pd_nmedia, pd_most, pd_tmp, pd_new_printer;
static long pd_st_printer, pd_st_mode, pd_st_from, pd_st_to, pd_st_copies, pd_st_mono, pd_st_quality, pd_st_ori;
/* les widgets : 11 mots, remis à 0 ensemble */
static struct {
	long printer, cur, range, from, to, copies, color, qual, media, ori, free;
} pd_w;
static long pd_qval[4];

static struct {
	u8 info[PDI_BYTES];
	u8 s[PS_BYTES];
	char title[80];
	char st_media[64];
	char dtitle[160];
	char def[96];
	char lbl[40];
	char qlist[48];
	u8 msg[512];
} pk;
#define pd_info pk.info
#define pj_s pk.s
#define pj_title pk.title
#define pd_st_media pk.st_media
#define pd_title pk.dtitle
#define pd_def pk.def
#define pd_lbl pk.lbl
#define pd_qlist pk.qlist
#define pk_msg pk.msg

static struct pk_font pj_fonts[PJ_NFONT];
static char pd_names[PD_NPR * 64];
static char pd_mlist[PD_NMEDIA * 40];
static char pd_mname[PD_NMEDIA * 64];
static u8 pd_pr[PD_NPR * PR_BYTES];
static u8 pj_pts[PJ_NPTS * PT_SIZE];
static u8 pj_buf[PJ_BUF];

/* ---- accès aux octets */

static inline u32 ld32(const void *p)
{
	u32 v;
	__builtin_memcpy(&v, p, 4);
	return v;
}
static inline long lds32(const void *p)
{
	return (s32)ld32(p);
}
static inline long ld64(const void *p)
{
	long v;
	__builtin_memcpy(&v, p, 8);
	return v;
}
static inline void st32(void *p, u64 v)
{
	u32 w = (u32)v;
	__builtin_memcpy(p, &w, 4);
}
static inline void st64(void *p, long v)
{
	__builtin_memcpy(p, &v, 8);
}
#define MSG64(o) ld64(pk_msg + (o))
#define SETMSG(o, v) st64(pk_msg + (o), (long)(v))

/* udiv d'AArch64 : une division par 0 rend 0 */
static inline u64 udiv(u64 a, u64 b)
{
	return b ? a / b : 0;
}

/* ---- kits ouverts au premier besoin (kits_open de kit.inc) : un kit pas
 * encore en mémoire est lu par AppKit (f_kit_load) ; absent ou trop ancien :
 * message et fin du programme (127), comme l'asm. Les relais d'import se
 * remplissent ensuite d'eux-mêmes. */

static void kit_need(const char *name, long count)
{
	long r = korin_sys2(SYS_LIB_OPEN, name, count);
	if (r >= 0)
		return;
	if (r == E_NOENT) {
		f_kit_load((long)name);
		if (korin_sys2(SYS_LIB_OPEN, name, count) >= 0)
			return;
	}
	korin_sys2(SYS_PRINT, kits_msg, sizeof kits_msg - 1);
	korin_sys2(SYS_PRINT, name, strlen(name));
	korin_sys2(SYS_PRINT, "\n", 1);
	korin_sys1(SYS_EXIT, 127);
}

static void kits_open(void)
{
	kit_need("appkit", KIT_APPKIT_FUNCTIONS);
	kit_need("uikit", KIT_UIKIT_FUNCTIONS);
	kit_need("archivekit", KIT_ARCHIVEKIT_FUNCTIONS);
}

static void pk_kits(void)
{
	if (pk_kits_ok)
		return;
	kits_open();
	pk_kits_ok = 1;
}

/* ---- petites aides */

static void pk_zero(void *d, u64 n)
{
	u8 *p = d;
	while (n--)
		*p++ = 0;
}

static void pk_copy(void *d, const void *s, u64 n)
{
	u8 *a = d;
	const u8 *b = s;
	while (n--)
		*a++ = *b++;
}

/* destination, taille, source (0 : vide) : copie coupée, 0 final */
static void pk_scpy(char *d, u64 size, const char *s)
{
	if (!size)
		return;
	size--;
	if (s)
		while (size && *s) {
			*d++ = *s++;
			size--;
		}
	*d = 0;
}

static u64 pk_slen(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	return n;
}

static long pk_seq(const char *a, const char *b)
{
	for (;;) {
		u8 c = (u8)*a++;
		if (c != (u8)*b++)
			return 0;
		if (!c)
			return 1;
	}
}

/* ---- le service */

/* message -> résultat (< 0 : pas de service). printd absent : lancé. */
static long pk_call(u8 *msg)
{
	pk_kits();
	long ch = korin_sys2(SYS_SVC_OPEN, s_svc, 0);
	if (ch < 0) {
		f_spawn((long)s_bin, 0);
		ch = korin_sys2(SYS_SVC_OPEN, s_svc, 4000);
		if (ch < 0)
			return ch;
	}
	long r = korin_sys2(SYS_SVC_CALL, ch, msg);
	if (r < 0)
		return r;
	return ld64(msg + WM_OP);
}

/* op, a, texte (0 : aucun) dans les données -> résultat (réponse dans pk_msg) */
static long pk_simple(long op, long a, const char *text)
{
	pk_zero(pk_msg, WM_DATA);
	SETMSG(WM_OP, op);
	SETMSG(WM_A, a);
	pk_scpy((char *)pk_msg + WM_DATA, 512 - WM_DATA, text);
	return pk_call(pk_msg);
}

/* ---- le fichier des pages */

static void pj_flush(void)
{
	if (pj_n) {
		f_write(pj_fd, (long)pj_buf, pj_n);
		pj_n = 0;
	}
}

static void pj_put(const void *src, u64 n)
{
	if ((u64)pj_n + n > PJ_BUF) {
		pj_flush();
		if (n >= PJ_BUF) { /* (gros bloc : sans passer par le tampon) */
			f_write(pj_fd, (long)src, (long)n);
			return;
		}
	}
	u64 at = (u64)pj_n;
	pj_n = (long)(at + n);
	pk_copy(pj_buf + at, src, n);
}

static void pj_head(u64 kind, u64 len)
{
	u8 h[8];
	st32(h, kind);
	st32(h + 4, len);
	pj_put(h, 8);
}

static void pj_rec(u64 kind, const void *data, u64 n)
{
	pj_head(kind, n);
	pj_put(data, n);
}

/* (entrée d'une fonction de dessin : hors page ou sans travail : rien) */
#define PJ_ON(job) ((job) != 0 && pj_on != 0)

/* ---- un travail */

/* PrintSetup, titre -> travail (0 : échec) */
EXPORT long print_begin(long s, long title)
{
	if (!s)
		return 0;
	pk_kits();
	if (pj_open)
		return 0; /* (un travail est ouvert) */
	pk_zero(pj_s, PS_BYTES); /* le dialogue d'une version plus ancienne : plus court */
	u64 n = ld32((u8 *)s + PS_SIZE);
	if (n >= PS_BYTES)
		n = PS_BYTES;
	pk_copy(pj_s, (const void *)s, n);
	pk_scpy(pj_title, 80, (const char *)title);
	if (!pj_title[0])
		pk_scpy(pj_title, 80, s_doc);
	long id = pk_simple(PD_NEW, 0, 0);
	if (id <= 0)
		return 0;
	pj_id = id;
	long fd = f_open((long)(pk_msg + WM_DATA), O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return 0;
	pj_fd = fd;
	pj_n = pj_seen = pj_on = pj_pages = pj_nfont = pj_npts = pj_imgf = 0;
	pj_open = 1;
	pj_put(s_magic, 4);
	return (long)pj_s;
}

/* fin de la page en cours */
static void pj_end_page(void)
{
	if (!pj_on)
		return;
	pj_on = 0;
	pj_head(PJR_ENDPAGE, 0);
	pj_pages++;
}

/* travail, l, h -> 1, 0 : hors des pages choisies */
EXPORT long print_page(long job, long w, long h)
{
	if (!job)
		return 0;
	pj_end_page();
	pj_npts = 0;
	long seen = ++pj_seen;
	long from = lds32(pj_s + PS_FROM), to = lds32(pj_s + PS_TO);
	if (from > 0) {
		if (seen < from)
			return 0;
		if (to > 0 && seen > to)
			return 0;
	}
	if (w <= 0)
		w = ld32(pj_s + PS_PAPER_W);
	if (h <= 0)
		h = ld32(pj_s + PS_PAPER_H);
	pj_on = 1;
	u8 r[8];
	st32(r, w);
	st32(r + 4, h);
	pj_rec(PJR_PAGE, r, 8);
	return 1;
}

/* travail, x, y, l, h, couleur */
EXPORT long print_rect(long job, long x, long y, long w, long h, long color)
{
	if (!PJ_ON(job))
		return 0;
	if (w <= 0 || h <= 0)
		return 0;
	u8 r[20];
	st32(r, x);
	st32(r + 4, y);
	st32(r + 8, w);
	st32(r + 12, h);
	st32(r + 16, color & 0xFFFFFF);
	pj_rec(PJR_RECT, r, 20);
	return 0;
}

/* travail, x0, y0, x1, y1, épaisseur, couleur */
EXPORT long print_line(long job, long x0, long y0, long x1, long y1, long th, long color)
{
	if (!PJ_ON(job))
		return 0;
	if (x0 == x1) {
		/* (droite et de niveau : un rectangle, nets sur la grille de l'imprimante) */
		long top = y0 < y1 ? y0 : y1, bot = y0 < y1 ? y1 : y0;
		return print_rect(job, x0 - (th >> 1), top, th, bot - top, color);
	}
	if (y0 == y1) {
		long l = x0 < x1 ? x0 : x1, r = x0 < x1 ? x1 : x0;
		return print_rect(job, l, y0 - (th >> 1), r - l, th, color);
	}
	if (th <= 0)
		th = 250;
	u8 p[36];
	st32(p, color & 0xFFFFFF);
	st32(p + 4, th);
	st32(p + 8, 2);
	st32(p + 12, V_MOVE);
	st32(p + 16, x0);
	st32(p + 20, y0);
	st32(p + 24, V_LINE);
	st32(p + 28, x1);
	st32(p + 32, y1);
	pj_rec(PJR_PATH, p, 36);
	return 0;
}

/* travail, x, y, l, h, épaisseur, couleur */
EXPORT long print_frame(long job, long x, long y, long w, long h, long th, long color)
{
	if (!PJ_ON(job))
		return 0;
	print_rect(job, x, y, w, th, color);                     /* haut */
	print_rect((long)pj_s, x, y + h - th, w, th, color);      /* bas */
	print_rect((long)pj_s, x, y, th, h, color);               /* gauche */
	print_rect((long)pj_s, x + w - th, y, th, h, color);      /* droite */
	return 0;
}

/* verbe, x, y */
static void pj_pt(u64 verb, long x, long y)
{
	if ((u64)pj_npts >= PJ_NPTS)
		return;
	u8 *p = pj_pts + pj_npts * PT_SIZE;
	st32(p, verb);
	st32(p + 4, x);
	st32(p + 8, y);
	pj_npts++;
}

EXPORT long print_path_move(long job, long x, long y)
{
	if (PJ_ON(job))
		pj_pt(V_MOVE, x, y);
	return 0;
}

EXPORT long print_path_line(long job, long x, long y)
{
	if (PJ_ON(job))
		pj_pt(V_LINE, x, y);
	return 0;
}

EXPORT long print_path_close(long job)
{
	if (PJ_ON(job))
		pj_pt(V_CLOSE, 0, 0);
	return 0;
}

EXPORT long print_path_curve(long job, long x1, long y1, long x2, long y2, long x3, long y3)
{
	if (!PJ_ON(job))
		return 0;
	pj_pt(V_CURVE, x1, y1);
	pj_pt(V_CURVE, x2, y2);
	pj_pt(V_CURVE, x3, y3);
	return 0;
}

/* couleur, épaisseur (0 : rempli) */
static void pj_path(long color, long th)
{
	u64 n = (u64)pj_npts;
	if (n >= 2) {
		pj_head(PJR_PATH, n * PT_SIZE + 12);
		u8 h[12];
		st32(h, color & 0xFFFFFF);
		st32(h + 4, th);
		st32(h + 8, n);
		pj_put(h, 12);
		pj_put(pj_pts, (u64)pj_npts * PT_SIZE);
	}
	pj_npts = 0;
}

EXPORT long print_path_fill(long job, long color)
{
	if (!job)
		return 0;
	if (pj_on)
		pj_path(color, 0);
	else
		pj_npts = 0;
	return 0;
}

EXPORT long print_path_stroke(long job, long th, long color)
{
	if (!job)
		return 0;
	if (th <= 0)
		th = 250;
	if (pj_on)
		pj_path(color, th);
	else
		pj_npts = 0;
	return 0;
}

/* (le travail n'est pas regardé, comme l'asm) */
EXPORT long print_image_mode(long job, long mode)
{
	(void)job;
	pj_imgf = mode;
	return 0;
}

/* travail, pixels, largeur, hauteur, x, y, l, h. Plus de pixels que
 * l'imprimante n'en montre (300 au pouce) : moyennes d'abord (k x k), puis
 * compressés. Comme l'asm, une image de moins de 240 millièmes de point dans
 * les deux sens ne finit jamais de chercher son k (aucun k ne la rend trop
 * petite). */
EXPORT long print_image(long job, long px, long pw, long ph, long x, long y, long w, long h)
{
	if (!PJ_ON(job))
		return 0;
	if (!px || pw <= 0 || ph <= 0 || w <= 0 || h <= 0)
		return 0;
	u8 hd[28];
	st32(hd, x);
	st32(hd + 4, y);
	st32(hd + 8, w);
	st32(hd + 12, h);
	u64 ww = (u64)w / 240, wh = (u64)h / 240; /* (millièmes de point par pixel à 300 dpi) */
	u64 k = 1;
	for (;;) {
		u64 k1 = k + 1;
		if (udiv((u64)pw, k1) < ww || udiv((u64)ph, k1) < wh)
			break;
		k = k1;
	}
	u64 ow = udiv((u64)pw, k), oh = udiv((u64)ph, k);
	st32(hd + 16, ow);
	st32(hd + 20, oh);
	st32(hd + 24, IMG_DEFLATE);
	u64 bytes = ow * oh * 4;
	u64 rsize = (bytes + 4095) & ~(u64)4095;
	long reg = korin_sys4(SYS_VM_MAP, 0, rsize, PROT_READ | PROT_WRITE, 0);
	if (reg < 0)
		return 0;
	u32 *out = (u32 *)reg;
	u64 alpha = (u64)pj_imgf & PRINT_IMG_ALPHA;
	for (u64 yy = 0; yy < oh; yy++)
		for (u64 xx = 0; xx < ow; xx++) {
			/* le bloc k x k */
			const u32 *row = (const u32 *)px + (yy * k * (u64)pw + xx * k);
			u64 a = 0, r = 0, g = 0, b = 0;
			u64 v = k;
			do {
				const u32 *p = row;
				u64 u = k;
				do {
					u32 c = *p++;
					u64 al = alpha ? c >> 24 : 255;
					a += al;
					r += ((c >> 16) & 255) * al;
					g += ((c >> 8) & 255) * al;
					b += (c & 255) * al;
				} while (--u);
				row += pw;
			} while (--v);
			if (a) {
				r = udiv(r, a);
				g = udiv(g, a);
				b = udiv(b, a);
			}
			a = udiv(a, k * k);
			*out++ = (u32)(b | g << 8 | r << 16 | a << 24);
		}
	/* (le fichier des pages reste petit : la carte s'écrit lentement, et une page est surtout blanche) */
	long d = ar_deflate_new(reg, (long)bytes, AR_ZLIB, 1);
	if (d > 0) {
		long n = ar_bytes(d);
		pj_head(PJR_IMAGE, (u64)n + 28);
		pj_put(hd, 28);
		pj_put((const void *)ar_data(d), (u64)n);
		ar_free(d);
	}
	korin_sys2(SYS_VM_UNMAP, reg, rsize);
	return 0;
}

/* travail, fichier JPEG, octets, largeur | hauteur << 32, x, y, l, h */
EXPORT long print_image_jpeg(long job, long jpg, long n, long wh, long x, long y, long w, long h)
{
	if (!PJ_ON(job))
		return 0;
	u8 hd[28];
	st32(hd, x);
	st32(hd + 4, y);
	st32(hd + 8, w);
	st32(hd + 12, h);
	st64(hd + 16, wh);
	st32(hd + 24, IMG_JPEG);
	pj_head(PJR_IMAGE, (u64)n + 28);
	pj_put(hd, 28);
	pj_put((const void *)jpg, (u64)n);
	return 0;
}

/* travail, famille (0 : DejaVu Sans), style -> police (-1 : aucune). (Sans
 * page ni travail ouvert, l'enregistrement va quand même au tampon, comme
 * l'asm.) */
EXPORT long print_font(long job, long family, long style)
{
	if (!job)
		return -1;
	const char *fam = family ? (const char *)family : s_sans;
	long key = pk_simple(PD_FONT, style & 3, fam);
	if (key < 0)
		return -1;
	long synth = MSG64(WM_A); /* style à fabriquer */
	long n = pj_nfont;
	for (long i = 0; (u64)i < (u64)n; i++)
		if (pj_fonts[i].key == key && pj_fonts[i].synth == synth)
			return i;
	if ((u64)n >= PJ_NFONT)
		return -1;
	pj_fonts[n].key = key;
	pj_fonts[n].synth = synth;
	pj_fonts[n].data = 0;
	pj_nfont = n + 1;
	/* l'enregistrement : police, style, chemin, 0 */
	u64 len = pk_slen((const char *)pk_msg + WM_DATA) + 1;
	pj_head(PJR_FONTREF, len + 8);
	u8 h[8];
	st32(h, n);
	st32(h + 4, synth);
	pj_put(h, 8);
	pj_put(pk_msg + WM_DATA, len);
	return n;
}

/* travail, TrueType, octets -> police (la même adresse : la même) */
EXPORT long print_font_data(long job, long ttf, long n)
{
	if (!job || !ttf || (u64)n < 12)
		return -1;
	long nf = pj_nfont;
	for (long i = 0; (u64)i < (u64)nf; i++)
		if (pj_fonts[i].data == ttf)
			return i;
	if ((u64)nf >= PJ_NFONT)
		return -1;
	pj_fonts[nf].key = -1;
	pj_fonts[nf].synth = 0;
	pj_fonts[nf].data = ttf;
	pj_nfont = nf + 1;
	pj_head(PJR_FONT, (u64)n + 4);
	u8 h[4];
	st32(h, nf);
	pj_put(h, 4);
	pj_put((const void *)ttf, (u64)n);
	return nf;
}

/* travail, police | style << 16, taille, x, base, glyphe, caractère, couleur */
EXPORT long print_glyph(long job, long fs, long size, long x, long base, long gid, long ch, long color)
{
	if (!PJ_ON(job))
		return 0;
	u64 f = (u64)fs & 0xFFFF;
	if (f >= (u64)pj_nfont)
		return 0;
	u64 synth = (u64)pj_fonts[f].synth | ((u64)fs >> 16); /* style + celui à fabriquer */
	u8 r[32];
	st32(r, f);
	st32(r + 4, synth);
	st32(r + 8, size);
	st32(r + 12, x);
	st32(r + 16, base);
	st32(r + 20, gid);
	st32(r + 24, ch);
	st32(r + 28, color & 0xFFFFFF);
	pj_rec(PJR_GLYPH, r, 32);
	return 0;
}

/* clé de la police, taille, texte -> avance (les longs textes par morceaux) */
static long pk_measure(long key, long size, const char *t)
{
	long sum = 0;
	if (key < 0)
		return 0;
	while (*t) {
		pk_zero(pk_msg, WM_DATA);
		SETMSG(WM_OP, PD_MEASURE);
		SETMSG(WM_A, key);
		SETMSG(WM_B, size);
		u8 *d = pk_msg + WM_DATA;
		u64 i = 0;
		for (;;) {
			u8 c = (u8)t[i];
			if (!c)
				break;
			if (i >= MSG_TEXT && (c & 0xC0) != 0x80)
				break; /* (pas au milieu d'un caractère) */
			d[i++] = c;
		}
		d[i] = 0;
		t += i;
		long r = pk_call(pk_msg);
		if (r < 0)
			return sum;
		sum += r;
	}
	return sum;
}

/* police -> sa case (0 : inconnue) */
static struct pk_font *pk_font(long f)
{
	if (f < 0 || (u64)f >= (u64)pj_nfont)
		return 0;
	return &pj_fonts[f];
}

/* travail, police, taille, x, base, UTF-8, couleur -> avance */
EXPORT long print_text(long job, long f, long size, long x, long base, long text, long color)
{
	if (!job || !text || size <= 0)
		return 0;
	struct pk_font *pf = pk_font(f);
	if (!pf)
		return 0;
	if (pj_on) {
		u8 h[28];
		u64 len = pk_slen((const char *)text);
		st32(h, f);
		st32(h + 4, pf->synth);
		st32(h + 8, size);
		st32(h + 12, x);
		st32(h + 16, base);
		st32(h + 20, color & 0xFFFFFF);
		st32(h + 24, len);
		pj_head(PJR_TEXT, len + 28);
		pj_put(h, 28);
		pj_put((const void *)text, len);
	}
	return pk_measure(pf->key, size, (const char *)text);
}

/* travail, police, taille, UTF-8 -> avance */
EXPORT long print_text_width(long job, long f, long size, long text)
{
	if (!job || !text)
		return 0;
	struct pk_font *pf = pk_font(f);
	if (!pf)
		return 0;
	return pk_measure(pf->key, size, (const char *)text);
}

/* travail, police, taille, 3 longs : ascendante, descendante, interligne */
EXPORT long print_font_metrics(long job, long f, long size, long out)
{
	u64 a, d, l;
	struct pk_font *pf;
	if (!job || !(pf = pk_font(f)) || pf->key < 0)
		goto def;
	SETMSG(WM_OP, PD_METRICS); /* (le reste du message : celui d'avant, comme l'asm) */
	SETMSG(WM_A, pf->key);
	SETMSG(WM_B, size);
	if (pk_call(pk_msg) != 1)
		goto def;
	a = (u64)MSG64(WM_A);
	d = (u64)MSG64(WM_B);
	l = (u64)MSG64(WM_C);
	goto out;
def:	/* (comme Onyx : sans police) */
	a = (u64)size * 93 / 100;
	d = (u64)size * 24 / 100;
	l = (u64)size * 120 / 100;
out:
	if (out) {
		st64((u8 *)out, (long)a);
		st64((u8 *)out + 8, (long)d);
		st64((u8 *)out + 16, (long)l);
	}
	return 0;
}

/* ferme le fichier */
static void pj_shut(void)
{
	if (!pj_open)
		return;
	pj_flush();
	f_close(pj_fd);
	pj_open = 0;
	pj_on = 0;
}

/* travail : rien d'imprimé */
EXPORT long print_abort(long job)
{
	if (!job)
		return 0;
	pj_shut();
	return pk_simple(PD_ABORT, pj_id, 0);
}

/* travail -> son numéro en file (> 0), < 0 : refusé */
EXPORT long print_end(long job)
{
	if (!job)
		return -1;
	pj_end_page();
	u8 e[4];
	st32(e, pj_pages);
	pj_rec(PJR_END, e, 4);
	pj_shut();
	long pages = pj_pages;
	if (pages) {
		pk_zero(pk_msg, 512);
		SETMSG(WM_OP, PD_SUBMIT);
		SETMSG(WM_A, pj_id);
		u32 c = ld32(pj_s + PS_COPIES);
		SETMSG(WM_B, (s32)c >= 1 ? (u64)c : 1);
		SETMSG(WM_C, (u64)ld32(pj_s + PS_MONO) | (u64)ld32(pj_s + PS_QUALITY) << 8);
		SETMSG(WM_D, pages);
		/* imprimante, titre, papier, sortie */
		pk_copy(pk_msg + WM_DATA, pj_s + PS_PRINTER, 64);
		pk_copy(pk_msg + WM_DATA + 64, pj_title, 80);
		pk_copy(pk_msg + WM_DATA + 144, pj_s + PS_MEDIA, 64);
		pk_copy(pk_msg + WM_DATA + 208, pj_s + PS_OUTPUT, 256);
		if (pk_call(pk_msg) == 1)
			return pj_id;
	}
	pk_simple(PD_ABORT, pj_id, 0);
	return -1;
}

/* ---- le réglage */

/* PrintSetup, papier (0 : celui du réglage), orientation -> 1, 0 : papier
 * inconnu (le réglage est rempli quand même, comme l'asm) */
EXPORT long print_setup_paper(long s, long media, long ori)
{
	if (!s)
		return 0;
	u8 *ps = (u8 *)s;
	pk_zero(pk_msg, 512);
	SETMSG(WM_OP, PD_SETUP);
	SETMSG(WM_A, ori);
	SETMSG(WM_B, lds32(ps + PS_COPIES));
	SETMSG(WM_C, lds32(ps + PS_QUALITY));
	SETMSG(WM_D, lds32(ps + PS_MONO));
	pk_copy(pk_msg + WM_DATA, ps + PS_PRINTER, 64);
	pk_scpy((char *)pk_msg + WM_DATA + 64, 64, media ? (const char *)media : (const char *)ps + PS_MEDIA);
	long r = pk_call(pk_msg);
	if (r < 0)
		return 0;
	pk_copy(ps + PS_PRINTER, pk_msg + WM_DATA, 128 + 24); /* imprimante, papier, 6 mots */
	st32(ps + PS_ORIENTATION, ori);
	st32(ps + PS_COPIES, MSG64(WM_A));
	st32(ps + PS_QUALITY, MSG64(WM_B));
	st32(ps + PS_MONO, MSG64(WM_C));
	return r;
}

/* PrintSetup : l'imprimante par défaut (nom vide), son papier, debout, un exemplaire */
EXPORT long print_setup_default(long s)
{
	if (!s)
		return 0;
	u8 *ps = (u8 *)s;
	pk_zero(ps, PS_BYTES);
	st32(ps + PS_SIZE, PS_BYTES);
	st32(ps + PS_COPIES, 1);
	st32(ps + PS_QUALITY, PRINT_NORMAL);
	return print_setup_paper(s, 0, PRINT_PORTRAIT);
}

/* ---- les imprimantes et la file */

/* out, places, op, octets d'une entrée -> nombre (a = rang, réponse 1 : une entrée) */
static long pk_list(long out, long n, long op, long size)
{
	long i = 0;
	while ((u64)i < (u64)n) {
		if (pk_simple(op, i, 0) != 1)
			break;
		pk_copy((u8 *)out + i * size, pk_msg + WM_DATA, (u64)size);
		i++;
	}
	return i;
}

EXPORT long print_printers(long out, long n)
{
	return pk_list(out, n, PD_PRINTER, PR_BYTES);
}

EXPORT long print_jobs(long out, long n)
{
	return pk_list(out, n, PD_JOB, PJI_BYTES);
}

EXPORT long print_printers_find(long out, long n)
{
	long r = pk_simple(PD_SCAN, 0, 0);
	if (r <= 0)
		return 0;
	return pk_list(out, n < r ? n : r, PD_FOUND, PF_BYTES);
}

/* imprimante, rang, nom, taille, libellé, taille -> 1, 0 : plus de papier */
EXPORT long print_printer_media(long pr, long rank, long name, long nsz, long lbl, long lsz)
{
	if (pk_simple(PD_MEDIA, rank, (const char *)pr) != 1)
		return 0;
	if (name)
		pk_scpy((char *)name, (u64)nsz, (const char *)pk_msg + WM_DATA);
	if (lbl)
		pk_scpy((char *)lbl, (u64)lsz, (const char *)pk_msg + WM_DATA + 64);
	return 1;
}

/* op, nom, adresse, out, taille -> 1 / 0 (out : le texte du service, ou
 * pourquoi il ne répond pas) */
static long pk_named(long op, long name, long addr, long out, long osz)
{
	pk_zero(pk_msg, 512);
	SETMSG(WM_OP, op);
	pk_scpy((char *)pk_msg + WM_DATA, 64, (const char *)name);
	pk_scpy((char *)pk_msg + WM_DATA + 64, 160, (const char *)addr);
	long r = pk_call(pk_msg);
	long res = r == 1;
	if (out)
		pk_scpy((char *)out, (u64)osz, r < 0 ? s_nosvc : (const char *)pk_msg + WM_DATA);
	return res;
}

EXPORT long print_printer_add(long name, long addr, long out, long size)
{
	return pk_named(PD_ADD, name, addr, out, size);
}

EXPORT long print_printer_remove(long name)
{
	return pk_named(PD_REMOVE, name, 0, 0, 0);
}

EXPORT long print_printer_default(long name)
{
	return pk_named(PD_DEFAULT, name, 0, 0, 0);
}

EXPORT long print_printer_status(long name, long out, long size)
{
	return pk_named(PD_REFRESH, name, 0, out, size);
}

EXPORT long print_job_cancel(long id)
{
	return pk_simple(PD_CANCEL, id, 0);
}

EXPORT long print_jobs_forget(void)
{
	return pk_simple(PD_FORGET, 0, 0);
}

/* ---- le dialogue */

/* texte, y : un libellé dans la colonne de gauche */
static void pd_lab(const char *t, long y)
{
	ui_label(PD_LX, y + 4, PD_CX - PD_LX - 6, 22, (long)t, 0);
}

/* éléments, y, valeur -> liste déroulante */
static long pd_drop(const char *items, long y, long v)
{
	long w = ui_dropdown(PD_CX, y, PD_CW, 26, (long)items);
	ui_w_set_value(w, v);
	return w;
}

/* imprimante (PrintPrinter) -> 1 si réseau */
static long pd_is_ipp(const u8 *pr)
{
	return pk_seq((const char *)pr + PR_KIND, s_pdf) ^ 1;
}

/* -> l'imprimante choisie (PrintPrinter) */
static u8 *pd_cur(void)
{
	return pd_pr + pd_st_printer * PR_BYTES;
}

/* les papiers de l'imprimante choisie -> pd_nmedia, pd_mname, pd_mlist */
static void pd_media_load(void)
{
	u8 *pr = pd_cur();
	char *p = pd_mlist;
	long n = 0;
	while (n < PD_NMEDIA) {
		if (!print_printer_media((long)pr, n, (long)(pd_mname + n * 64), 64, (long)pd_lbl, 40))
			break;
		if (n)
			*p++ = '|';
		for (const char *q = pd_lbl; *q;)
			*p++ = *q++;
		n++;
	}
	*p = 0;
	pd_nmedia = n;
}

/* -> le papier à montrer : celui choisi, sinon celui de l'imprimante */
static long pd_media_sel(void)
{
	long n = pd_nmedia;
	for (long i = 0; (u64)i < (u64)n; i++)
		if (pk_seq(pd_mname + i * 64, pd_st_media))
			return i;
	const char *def = (const char *)pd_cur() + PR_MEDIA_DEFAULT;
	for (long i = 0; (u64)i < (u64)n; i++)
		if (pk_seq(pd_mname + i * 64, def))
			return i;
	return 0;
}

/* (dialogue) dialogue, widget, code */
static long pd_cb(long dlg, long w, long code)
{
	if (code != UN_CHANGE)
		return dlg;
	if (w == pd_w.printer) {
		if (ui_w_get_value(w) != pd_st_printer)
			ui_dialog_close(dlg, RD_PRINTER);
	} else if (w == pd_w.from || w == pd_w.to) {
		ui_w_set_value(pd_w.range, 1); /* une borne touchée : le choix « de .. à » */
	}
	return dlg;
}

/* ce que l'utilisateur a réglé, lu dans les widgets */
static void pd_keep(void)
{
	long v;
	pd_new_printer = ui_w_get_value(pd_w.printer);
	if (ui_w_get_value(pd_w.range))
		v = 2;
	else if (pd_w.cur)
		v = ui_w_get_value(pd_w.cur);
	else
		v = 0;
	pd_st_mode = v;
	pd_st_from = ui_w_get_value(pd_w.from);
	v = ui_w_get_value(pd_w.to);
	pd_st_to = v < pd_st_from ? pd_st_from : v;
	if (pd_w.copies)
		pd_st_copies = ui_w_get_value(pd_w.copies);
	if (pd_w.color)
		pd_st_mono = ui_w_get_value(pd_w.color);
	if (pd_w.qual)
		pd_st_quality = pd_qval[ui_w_get_value(pd_w.qual)];
	if (pd_w.media) {
		v = ui_w_get_value(pd_w.media);
		if (pd_nmedia)
			pk_scpy(pd_st_media, 64, pd_mname + v * 64);
	}
	if (pd_w.ori)
		pd_st_ori = ui_w_get_value(pd_w.ori);
}

/* construit et montre le dialogue pour l'imprimante choisie -> RD_* */
static long pd_show(void)
{
	u8 *pr = pd_cur();
	long ipp = pd_is_ipp(pr);
	/* le nombre de lignes */
	long rows = 1;
	if (ipp) {
		rows++; /* copies */
		if (ld32(pr + PR_COLOR))
			rows++; /* couleur */
		u32 q = ld32(pr + PR_QUALITY) & 0x38;
		if (q & (q - 1))
			rows++; /* qualité : plus d'une */
	}
	u32 curpage = ld32(pd_info + PDI_CURRENT);
	u64 own = ld32(pd_info + PDI_FLAGS) & PRINT_DLG_OWN_PAPER;
	if (!own)
		rows += 2; /* papier, orientation */
	long h = rows * PD_ROW + 44 + 24 + 10 + 54 + 28 * 2;
	if (curpage)
		h += 28;
	long dlg = ui_dialog_begin(PD_DW, h, (long)pd_title);
	if (!dlg)
		return RD_CANCEL;
	pk_zero(&pd_w, sizeof pd_w); /* tous les widgets à 0 */
	ui_label(PD_LX, 12, PD_DW - PD_LX - 80, 24, (long)pd_title, ULB_BOLD); /* (le haut droit reste aux boutons du cadre) */
	long y = 44;
	pd_lab(s_printer, y);
	pd_w.printer = pd_drop(pd_names, y, pd_st_printer);
	ui_label(PD_CX, y + 30, PD_CW, 20, (long)(pr + PR_MODEL), ULB_DIM); /* le modèle */
	y += PD_ROW + 24;
	/* les pages */
	pd_lab(s_pages, y);
	long w = ui_radio(PD_CX, y, PD_CW, 24, (long)s_all, 1);
	if (!pd_st_mode)
		ui_w_set_value(w, 1);
	y += 28;
	if (curpage) {
		pd_w.cur = ui_radio(PD_CX, y, PD_CW, 24, (long)s_current, 1);
		if (pd_st_mode == 1)
			ui_w_set_value(pd_w.cur, 1);
		y += 28;
	}
	pd_w.range = ui_radio(PD_CX, y, 76, 24, (long)s_from, 1);
	if (pd_st_mode == 2)
		ui_w_set_value(pd_w.range, 1);
	long pages = lds32(pd_info + PDI_PAGES); /* (la borne : le nombre de pages, s'il est connu) */
	pd_most = pages > 0 ? pages : 9999;
	pd_w.from = ui_numeric(PD_CX + 78, y - 1, 84, 26, 1, pd_most, pd_st_from);
	ui_label(PD_CX + 78 + 84 + 6, y + 3, 20, 22, (long)s_to, 0);
	pd_w.to = ui_numeric(PD_CX + 78 + 84 + 28, y - 1, 84, 26, 1, pd_most, pd_st_to);
	y += 28 + 10;
	if (ipp) {
		/* copies, couleur, qualité (imprimante réseau) */
		pd_lab(s_copies, y);
		u32 mc = ld32(pr + PR_COPIES);
		if ((s32)mc < 1)
			mc = 1;
		pd_w.copies = ui_numeric(PD_CX, y, 84, 26, 1, mc, pd_st_copies);
		y += PD_ROW;
		if (ld32(pr + PR_COLOR)) {
			pd_lab(s_colour, y);
			pd_w.color = pd_drop(s_colours, y, pd_st_mono != 0);
			y += PD_ROW;
		}
		/* la liste des qualités */
		u64 q = ld32(pr + PR_QUALITY);
		char *p = pd_qlist;
		long n = 0, sel = 0;
		for (long b = 3; b <= 5; b++) {
			if (!(q & (1UL << b)))
				continue;
			pd_qval[n] = b;
			if (b == pd_st_quality)
				sel = n;
			if (n)
				*p++ = '|';
			for (const char *t = q_names[b - 3]; *t;)
				*p++ = *t++;
			n++;
		}
		*p = 0;
		if (n >= 2) {
			pd_tmp = sel;
			pd_lab(s_quality, y);
			pd_w.qual = pd_drop(pd_qlist, y, pd_tmp);
			y += PD_ROW;
		}
	}
	if (!own) {
		pd_media_load();
		pd_tmp = pd_media_sel();
		pd_lab(s_paper, y);
		pd_w.media = pd_drop(pd_mlist, y, pd_tmp);
		y += PD_ROW;
		pd_lab(s_orient, y);
		pd_w.ori = pd_drop(s_orients, y, pd_st_ori);
	}
	long e = ui_dialog_button(dlg, 0, (long)s_print, RD_PRINT, UB_DEFAULT, PD_DW - 12);
	ui_dialog_button(dlg, 1, (long)s_cancel, RD_CANCEL, 0, e);
	ui_dialog_cancel(dlg, RD_CANCEL);
	long r = ui_dialog_run(dlg, (long)pd_cb, 0); /* (le 3e : du prototype généré, sans usage) */
	if (r != RD_CANCEL)
		pd_keep();
	ui_dialog_end(dlg);
	return r;
}

/* PrintSetup, PrintDialogInfo -> 1 : imprimer (réglage rempli), 0 : abandon */
EXPORT long print_dialog(long s, long info)
{
	if (!s)
		return 0;
	u8 *ps = (u8 *)s;
	pk_kits();
	pk_zero(pd_info, PDI_BYTES);
	if (info) {
		u64 n = ld32((u8 *)info + PDI_SIZE);
		if (n >= PDI_BYTES)
			n = PDI_BYTES;
		pk_copy(pd_info, (const void *)info, n);
	}
	if (!ld32(ps + PS_SIZE))
		print_setup_default(s);
	/* les imprimantes */
	long n = print_printers((long)pd_pr, PD_NPR);
	pd_npr = n;
	if (!n)
		return 0;
	char *p = pd_names;
	pd_st_printer = 0;
	for (long i = 0; (u64)i < (u64)n; i++) {
		const char *e = (const char *)pd_pr + i * PR_BYTES;
		if (i)
			*p++ = '|';
		for (const char *t = e; *t;)
			*p++ = *t++;
		if (pk_seq(e, (const char *)ps + PS_PRINTER)) /* la choisie : celle du réglage */
			pd_st_printer = i;
	}
	*p = 0;
	/* le reste du réglage */
	long from = lds32(ps + PS_FROM);
	pd_st_mode = from > 0 ? 2 : 0;
	pd_st_from = from > 0 ? from : 1;
	long pages = lds32(pd_info + PDI_PAGES);
	if (pages <= 0)
		pages = 1;
	long to = lds32(ps + PS_TO);
	pd_st_to = to > 0 ? to : pages;
	long c = lds32(ps + PS_COPIES);
	pd_st_copies = c > 0 ? c : 1;
	pd_st_mono = lds32(ps + PS_MONO);
	pd_st_quality = lds32(ps + PS_QUALITY);
	pd_st_ori = lds32(ps + PS_ORIENTATION);
	pk_scpy(pd_st_media, 64, (const char *)ps + PS_MEDIA);
	/* le titre. Comme l'asm, « - » est recopié sans son 0 final : le titre
	 * d'un dialogue précédent, s'il y en avait un, reste au bout de
	 * « Imprimer - » et le nouveau s'y ajoute ; et le titre s'écrit sur
	 * 144 octets depuis sa fin (il peut déborder sur pd_def). */
	pk_scpy(pd_title, 160, s_print);
	long t = ld64(pd_info + PDI_TITLE);
	if (t) {
		pk_copy(pd_title + pk_slen(pd_title), s_dash, 3);
		pk_scpy(pd_title + pk_slen(pd_title), 160 - 16, (const char *)t);
	}
	long r;
	while ((r = pd_show()) == RD_PRINTER)
		pd_st_printer = pd_new_printer;
	if (r != RD_PRINT)
		return 0;
	pd_st_printer = pd_new_printer;
	u8 *pr = pd_cur();
	long ipp = pd_is_ipp(pr);
	pk_scpy((char *)ps + PS_PRINTER, 64, (const char *)pr);
	long cp = 1, mono = 0;
	if (ipp) {
		cp = pd_st_copies;
		mono = pd_st_mono;
	}
	st32(ps + PS_COPIES, cp);
	st32(ps + PS_MONO, mono);
	st32(ps + PS_QUALITY, pd_st_quality);
	long f = 0, l = 0;
	if (pd_st_mode == 2) {
		f = pd_st_from;
		l = pd_st_to;
	} else if (pd_st_mode == 1) {
		f = l = ld32(pd_info + PDI_CURRENT);
	}
	st32(ps + PS_FROM, f);
	st32(ps + PS_TO, l);
	print_setup_paper(s, (long)pd_st_media, pd_st_ori);
	if (ipp)
		return 1;
	/* l'imprimante PDF : le fichier à écrire */
	const char *src = t ? (const char *)t : s_doc;
	for (const char *q = src; *q;) { /* (le dernier élément du chemin) */
		char ch = *q++;
		if (ch == '/' || ch == ':')
			src = q;
	}
	pk_scpy(pd_def, 88, src);
	u64 k = pk_slen(pd_def); /* (son extension s'en va) */
	while (k) {
		k--;
		if (pd_def[k] != '.')
			continue;
		if (k)
			pd_def[k] = 0;
		break;
	}
	pk_copy(pd_def + pk_slen(pd_def), s_ext, 5);
	return ui_file_save((long)s_docs, (long)pd_def, (long)(ps + PS_OUTPUT), 256);
}
