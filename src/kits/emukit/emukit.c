/* EmuKit : le socle des émulateurs (docs/EMULATEURS.md), en C commun au Pi et
 * au PC ; remplace emukit.asm, même table (emukit.abi), même conduite.
 *
 * Tout ce qui n'est pas la console. L'application donne un descripteur
 * (EMD_*, emukit.inc ; struct emu_desc de emukit.h) ; emu_run ouvre la
 * fenêtre, charge la ROM, et tourne jusqu'à la fermeture.
 *
 *   thread de la fenêtre : événements, menus, image à l'échelle dans une
 *     surface UIKit, écriture de <rom>.sav, vitesse dans le titre ;
 *   thread de la machine (priorité haute) : une image, le son vers AudioKit,
 *     la cadence ; les boutons viennent de WEV_PAD (Elegant : la fenêtre qui
 *     a le clavier) ; remise à zéro et commandes de l'application, entre deux
 *     images.
 *
 * Image : trois tampons (la machine dessine dans l'un, le dernier fini attend
 * dans le deuxième, la fenêtre lit le troisième) ; un échange atomique.
 * Cadence : l'horloge du processeur (CNTPCT_EL0 sur le Pi ; les millisecondes
 * du noyau sur PC, faute d'horloge plus fine), corrigée par ce qui attend dans
 * le flux du son.
 *
 * Toutes les variables de l'asm faisaient 64 bits (dd de FASMARM) ; celles que
 * les deux threads se partagent sont volatiles (l'asm les relisait toujours).
 * Le kit a ses propres variables de programme pour UIKit (macro ui_data :
 * ui_init les lui donne), comme l'asm. */
#include "korin_kit.h"

#include "appkit_import.h"
#include "audiokit_import.h"
#include "uikit_import.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

#define EXPORT __attribute__((visibility("hidden"), noinline))

/* ---- constantes (armpi.inc, uikit.inc, pad.inc, emukit.inc) */

#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define PROT_READ 1
#define PROT_WRITE 2
#define E_NOENT (-2)
#define THREAD_PRIO_HIGH 1

#define UI_FONT_SIZE 320
#define WF_FRAMELESS 1
#define WF_TOP 4
#define WF_RESIZABLE 512
#define WF_GLYPH_LIGHT 4096
#define WF_BODY_DARK 8192
#define WEV_KEY 1
#define WEV_MOUSE 2
#define WEV_CLOSE 3
#define WEV_WHEEL 4
#define WEV_FOCUS 5
#define WEV_MENU 7
#define WEV_RESIZE 8
#define WEV_PAD 11
#define WEV_PAD_BUTTONS 8
#define WEV_PAD_AXES 16
#define WEV_M_REL 3
#define EV_KIND 4
#define EV_X 8
#define EV_Y 12
#define EV_CODE 16
#define EV_CHAR 20
#define EV_MODS 24
#define EV_WIN 28
#define UMS_CHECKED 2
#define UM_OK 0
#define TJ_DARK 88
#define WS_SF_XRGB8888 0

#define PAD_MAX 4
#define PAD_START (1 << 13)

#define EMD_NAME 0
#define EMD_EXT 8
#define EMD_W 16
#define EMD_H 24
#define EMD_MAXW 32
#define EMD_MAXH 40
#define EMD_FPS 48
#define EMD_RATE 56
#define EMD_CTX 64
#define EMD_LOAD 72
#define EMD_FRAME 80
#define EMD_AUDIO 88
#define EMD_RESET 96
#define EMD_SAVE 104
#define EMD_RESTORE 112
#define EMD_MENU 120
#define EMD_CMD 128
#define EMD_MOUSE 136
#define EMD_BUFS 144
#define EMD_SKIP 152
#define EMD_ERROR 160
#define EME_CANCEL 1
#define EMG_KEY_WHEEL 0x1000
#define EMG_KEY_NONE 0x1001

#define EM_NBUF 3
#define EM_STACK 0x800000 /* pile de la machine (n64emu y compile ses shaders par Mesa) */
#define EM_PATH 512
#define EM_AUDIO 4096 /* trames par lecture du son */
#define EM_SAVEBUF 0x400000
#define EM_CHUNK 0x100000 /* lecture de la ROM */
#define EM_SAVE_EVERY 300 /* images entre deux coups d'œil à la sauvegarde */
#define EM_XMAP 4096
#define EM_TW 160 /* (--thumb) comme TW, TH de gamelib */
#define EM_TH 120
#define EM_TH_FRAMES 900
#define EM_Q_LOW 0x2000 /* octets en attente dans le flux (s16 stéréo 44,1 kHz) : 46 ms */
#define EM_Q_HIGH 0x3000

#define ID_OPEN 1
#define ID_PAUSE 2
#define ID_RESET 3
#define ID_QUIT 4
#define ID_ZOOM1 5
#define ID_MAX 8
#define ID_SPEED 9
#define ID_SOUND 10
#define ID_FULL 11
#define KEY_F9 0x42
#define KEY_ESC 0x29
#define KEY_F12 0x45
#define ID_USER 100

/* ---- chaînes (comme l'asm) */

static const char em_tname[] = "emu-machine";
static const char em_dir0[] = "SD0:/home";
static const char em_dir1[] = "SD0:/home/roms";
static const char em_sep[] = " - ";
static const char em_paused[] = " (paused)";
static const char em_s1[] = " - ";
static const char em_s2[] = " fps, shown ";
static const char em_s3[] = ", ";
static const char em_s4[] = " ms";
static const char em_s5[] = " %";
static const char em_badrom[] = "This file cannot be played here.";
static const char em_menu_norom[] = "MGame\n"
				    "I2\tPause\n"
				    "-\n"
				    "I4\tQuit\n"
				    "MView\n"
				    "I5\tZoom 1x\n"
				    "I6\tZoom 2x\n"
				    "I7\tZoom 3x\n"
				    "I8\tMaximize\n"
				    "I11\tFull Screen\tF9\n"
				    "-\n"
				    "I9\tShow Speed\tF12\n"
				    "MSound\n"
				    "I10\tSound\n";
static const char em_menu_game[] = "MGame\n"
				   "I1\tOpen...\tCtrl+O\n"
				   "-\n"
				   "I2\tPause\tP\n"
				   "I3\tReset\n"
				   "-\n"
				   "I4\tQuit\tCtrl+Q\n"
				   "MView\n"
				   "I5\tZoom 1x\n"
				   "I6\tZoom 2x\n"
				   "I7\tZoom 3x\n"
				   "I8\tMaximize\n"
				   "I11\tFull Screen\tF9\n"
				   "-\n"
				   "I9\tShow Speed\tF12\n"
				   "MSound\n"
				   "I10\tSound\n";
static const char kits_msg[] = "kit absent ou trop ancien : SD0:/lib/";

/* ---- variables (segment inscriptible de l'asm) */

static struct { /* macro ui_data de uikit.inc */
	long win, surf, w, h;
	u8 ev[32];
	u8 font[3 * UI_FONT_SIZE];
} __attribute__((aligned(8))) ui_v;

static long em_desc;
static long em_scr[4];
static long em_stat[4];
static long em_hz, em_tok, em_iw, em_ih, em_maxw, em_maxh, em_pitch, em_rom;
static long em_lastpc = -1;
static long em_buf[EM_NBUF];
static volatile long em_fw[EM_NBUF], em_fh[EM_NBUF];
static volatile long em_back;
static long em_front;
static u32 em_mid; /* (mot de 32 bits : échange atomique) */
static volatile long em_quit, em_pause;
static long em_show, em_thumb, em_wantfull;
static char em_thumb_out[320];
static long em_full, em_win0, em_surf0;
static volatile long em_sound; /* 1 : muet */
static volatile u32 em_pad[PAD_MAX];
static volatile long em_axes[PAD_MAX];
static volatile long em_cmd, em_same;
static long em_bypath;
static long em_tid = -1;
static long em_keyfn, em_gflags; /* emu_game */
static long em_focus = 1;
static long em_ptr_want, em_ptr_on;
static long em_stream = -1;
static long em_surf, em_glfn, em_glctx;
static long em_vx, em_vy;
static long em_vw = 1, em_vh = 1;
static long em_xsw, em_xdw;
static volatile long em_frames, em_spent;
static long em_savebuf;
static volatile long em_savelen;
static char em_path[EM_PATH];
static char em_sav[EM_PATH + 8];
static char em_title[256];
static char em_line[320];
static char em_menu_buf[2048];
static short em_pcm[EM_AUDIO * 2];
static u32 em_xmap[EM_XMAP];

/* ---- aides */

static inline u32 ld32(const void *p)
{
	u32 v;
	__builtin_memcpy(&v, p, 4);
	return v;
}
static inline long ld64(const void *p)
{
	long v;
	__builtin_memcpy(&v, p, 8);
	return v;
}
#define EV32(o) ld32(ui_v.ev + (o))

/* udiv / sdiv d'AArch64 : une division par 0 rend 0 */
static inline u64 udiv(u64 a, u64 b)
{
	return b ? a / b : 0;
}
static inline long sdiv(long a, long b)
{
	if (!b)
		return 0;
	if (b == -1)
		return (long)(0 - (u64)a);
	return a / b;
}

/* un champ du descripteur */
#define D(o) (((const long *)em_desc)[(o) / 8])
#define CTX D(EMD_CTX)
typedef long (*fn1)(long);
typedef long (*fn2)(long, long);
typedef long (*fn3)(long, long, long);
typedef long (*fn5)(long, long, long, long, long);
typedef long (*fn6)(long, long, long, long, long, long);

static inline u64 em_now(void)
{
#if defined(__aarch64__)
	u64 v;
	__asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(v)::"memory");
	return v;
#else
	return (u64)korin_sys0(SYS_TIME_MS);
#endif
}

static inline long em_freq(void)
{
#if defined(__aarch64__)
	long v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
#else
	return 1000;
#endif
}

/* ui_surface_pixels rend quatre registres (pixels, pas, largeur, hauteur) */
typedef struct {
	long r0, r1, r2, r3;
} r4;
#if defined(__aarch64__)
void em_surface_pixels4(long surf, r4 *out);
__asm__("\t.text\n"
	"\t.p2align 2\n"
	"\t.hidden em_surface_pixels4\n"
	"\t.globl em_surface_pixels4\n"
	"em_surface_pixels4:\n"
	"\tstp x29, x30, [sp, -32]!\n"
	"\tmov x29, sp\n"
	"\tstr x1, [sp, 16]\n"
	"\tbl ui_surface_pixels\n"
	"\tldr x16, [sp, 16]\n"
	"\tstp x0, x1, [x16]\n"
	"\tstp x2, x3, [x16, 16]\n"
	"\tldp x29, x30, [sp], 32\n"
	"\tret\n");
#else
/* UIKit en C sur x86-64 : la structure rendue par la mémoire (System V) */
extern r4 ui_surface_pixels_r4(long surf) __asm__("ui_surface_pixels");
static void em_surface_pixels4(long surf, r4 *out)
{
	*out = ui_surface_pixels_r4(surf);
}
#endif

/* ---- kits (kits_open de kit.inc) : un kit pas encore en mémoire est lu par
 * AppKit (f_kit_load) ; absent ou trop ancien : message et fin du programme
 * (127), comme l'asm. */

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
	kit_need("audiokit", KIT_AUDIOKIT_FUNCTIONS);
}

/* destination, source -> après le 0 final */
static char *em_strcpy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
	return d;
}

/* destination, texte -> sur le 0 final */
static char *em_strcpy2(char *d, const char *s)
{
	while ((*d = *s++))
		d++;
	return d;
}

/* chaîne, à ajouter -> sur le 0 final */
static char *em_cat0(char *d, const char *s)
{
	while (*d)
		d++;
	return em_strcpy2(d, s);
}

static char *em_dec(long v, char *d)
{
	return (char *)fmt_dec(v, (long)d);
}

/* octets -> mémoire à zéro (0 : échec) */
static long em_map(long n)
{
	long r = korin_sys4(SYS_VM_MAP, 0, n, PROT_READ | PROT_WRITE, 0);
	return r < 0 ? 0 : r;
}

/* ---- les petites fonctions de la table */

/* manette, 4 x 16 bits signés (lx, ly, rx, ry) : thread de la machine */
EXPORT long emu_pad_axes(long pad, long out)
{
	*(long *)out = em_axes[pad & (PAD_MAX - 1)];
	return pad & (PAD_MAX - 1);
}

/* numéro d'une commande (thread de la fenêtre) */
EXPORT long emu_command(long id)
{
	em_cmd = id;
	return id;
}

EXPORT long emu_rom_path(void)
{
	return (long)em_path;
}

/* largeur, hauteur : depuis EMD_FRAME */
EXPORT long emu_frame_size(long w, long h)
{
	long b = em_back;
	em_fw[b] = w;
	em_fh[b] = h;
	return w;
}

/* avant emu_run : EMD_LOAD recevra le chemin, pas le contenu */
EXPORT long emu_rom_by_path(void)
{
	em_bypath = 1;
	return 1;
}

/* depuis EMD_FRAME : rien de neuf, la dernière image publiée reste */
EXPORT long emu_frame_same(void)
{
	em_same = 1;
	return 1;
}

/* touches, EMG_* : avant emu_run */
EXPORT long emu_game(long keyfn, long flags)
{
	em_keyfn = keyfn;
	em_gflags = flags;
	return keyfn;
}

/* 1 : souris relative (le thread de la fenêtre l'applique) */
EXPORT long emu_pointer(long on)
{
	em_ptr_want = on;
	return on;
}

/* fonction, contexte : à chaque tour de la fenêtre, sur son thread,
 * f (ctx, fenêtre, x, y, l, h) -> images montrées. Le rectangle est celui de
 * l'image ; pas de surface du kit : l'application y rend (armgl_window). */
EXPORT long emu_gl_hook(long fn, long ctx)
{
	em_glfn = fn;
	em_glctx = ctx;
	return fn;
}

/* ---- les threads */

/* entrée d'un thread : sommet = fonction, argument */
static __attribute__((noreturn, used)) void em_tramp(long *top)
{
	fn1 f = (fn1)top[0];
	f(top[1]);
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

/* fonction (argument), argument, octets de pile, nom -> tid. Le thread garde
 * le registre de thread de l'appelant (TPIDR_EL0) ; sur PC, le programme ne
 * peut pas lire la base de FS : 0. */
EXPORT long emu_thread(long fn, long arg, long stack, long name)
{
	long p = em_map(stack);
	if (!p)
		return -12;
	long *top = (long *)(p + stack - 16);
	top[0] = fn; /* fonction, argument */
	top[1] = arg;
	long tls = 0;
#if defined(__aarch64__)
	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(tls));
#endif
	return korin_sys6(SYS_THREAD_CREATE_EX, (long)em_tramp, (long)top, (long)top, tls, THREAD_PRIO_HIGH, name);
}

/* le thread de la machine */
static long em_machine(long arg)
{
	(void)arg;
	u64 hz = (u64)em_hz;
	u64 per = udiv(udiv(hz * 1000000, (u64)D(EMD_FPS)), 1000); /* durée d'une image (ticks) */
	long flags = 0;    /* EMF_* */
	u64 frames = 0;    /* images */
	u64 due = em_now(); /* prochaine échéance */
	for (;;) {
		if (em_quit)
			return 0;
		/* les commandes */
		long cmd = em_cmd;
		if (cmd) {
			em_cmd = 0;
			if (cmd == ID_RESET) {
				if (D(EMD_RESET))
					((fn1)D(EMD_RESET))(CTX);
			} else if (D(EMD_CMD)) {
				((fn2)D(EMD_CMD))(CTX, cmd);
			}
		}
		if (em_pause) {
			korin_sys1(SYS_SLEEP, 20);
			due = em_now();
			continue;
		}
		u32 b0 = em_pad[0], b1 = em_pad[1], b2 = em_pad[2], b3 = em_pad[3];
		if (em_gflags)
			b0 = 0; /* (EMG_NOROM : la manette 0 est le clavier, déjà lu) */
		u32 buttons = b0 | b1 | b2 | b3;
		u64 t0 = em_now();
		((fn5)D(EMD_FRAME))(CTX, em_buf[em_back], em_pitch, buttons, flags);
		em_spent += (long)(em_now() - t0);
		em_frames++;
		frames++;
		if (!(flags & 1)) { /* (image sautée : la précédente reste) */
			if (em_same) {
				em_same = 0;
			} else {
				/* publier : back <-> mid */
				u32 v = (u32)em_back | 4;
				__atomic_thread_fence(__ATOMIC_SEQ_CST);
				u32 old = __atomic_exchange_n(&em_mid, v, __ATOMIC_ACQ_REL);
				em_back = old & 3;
			}
		}
		/* le son */
		if (D(EMD_AUDIO)) {
			long n;
			do {
				n = ((fn3)D(EMD_AUDIO))(CTX, (long)em_pcm, EM_AUDIO);
				if (n <= 0)
					break;
				if (em_stream >= 0 && !em_sound) /* (em_sound : 1 = muet) */
					ak_write(em_stream, (long)em_pcm, n << 2);
			} while (n == EM_AUDIO);
		}
		/* la sauvegarde */
		if (frames % EM_SAVE_EVERY == 0 && D(EMD_SAVE) && !em_savelen) {
			long p = 0;
			long n = ((fn2)D(EMD_SAVE))(CTX, (long)&p);
			if (n > 0 && (u64)n <= EM_SAVEBUF && em_savebuf) {
				mem_move(em_savebuf, p, n);
				__atomic_thread_fence(__ATOMIC_SEQ_CST);
				em_savelen = n;
			}
		}
		/* l'échéance suivante */
		due += per;
		if (!em_gflags && em_stream >= 0) { /* (un jeu vit au temps réel : le son le suit) */
			long q = ak_pending(em_stream);
			if (q > 0) { /* 0 juste après l'écriture : pas de son, l'horloge seule */
				u64 adj = per >> 2;
				if ((u64)q > EM_Q_HIGH)
					due += adj; /* trop d'avance : on ralentit */
				else if ((u64)q < EM_Q_LOW)
					due -= adj; /* le flux se vide : on accélère */
			}
		}
		for (;;) {
			u64 t = em_now();
			flags = 0;
			if (due > t) {
				u64 ms = udiv(due - t, udiv(hz, 1000));
				if (ms >= 2)
					korin_sys1(SYS_SLEEP, ms - 1);
				else
					korin_sys0(SYS_YIELD);
				continue;
			}
			/* en retard */
			u64 late = t - due;
			if (late <= per)
				break;
			flags = D(EMD_SKIP) != 0; /* la prochaine sans dessin */
			if (late <= per << 2)
				break;
			due = t; /* trop loin : on oublie ce retard */
			flags = 0;
			break;
		}
	}
}

static long em_start(void)
{
	em_quit = 0;
	em_back = 0;
	em_front = 1;
	__atomic_store_n(&em_mid, 2, __ATOMIC_RELAXED);
	em_tid = emu_thread((long)em_machine, 0, EM_STACK, (long)em_tname);
	return em_tid;
}

/* données, octets : <rom>.sav */
static void em_save_write(long data, long n)
{
	long fd = f_open((long)em_sav, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return;
	f_write(fd, data, n);
	f_close(fd);
}

static void em_save_flush(void)
{
	long n = em_savelen;
	if (!n)
		return;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	em_save_write(em_savebuf, n);
	em_savelen = 0;
}

/* arrête la machine ; la dernière sauvegarde */
static void em_stop(void)
{
	em_quit = 1;
	if (em_tid >= 0)
		korin_sys1(SYS_THREAD_JOIN, em_tid);
	em_save_flush();
	if (!D(EMD_SAVE))
		return;
	long p = 0;
	long n = ((fn2)D(EMD_SAVE))(CTX, (long)&p);
	if (n > 0)
		em_save_write(p, n);
}

/* ---- la ROM */

/* arguments -> le chemin ; « --full chemin », « --thumb sortie chemin ».
 * Comme l'asm : « --full » saute 7 octets, « --thumb » 8. */
static const char *em_options(const char *a)
{
	if (!a)
		return a;
	for (;;) {
		while (*a == ' ')
			a++;
		if (*a != '-')
			return a;
		if (a[2] == 'f') {
			a += 7; /* « --full » */
			em_wantfull = 1;
			continue;
		}
		if (a[2] != 't')
			return a;
		a += 8; /* « --thumb » */
		while (*a == ' ')
			a++;
		char *o = em_thumb_out;
		for (;;) {
			char c = *a;
			if (!c)
				break;
			a++;
			if (c == ' ')
				break;
			*o++ = c;
		}
		*o = 0;
		em_thumb = 1;
		return a;
	}
}

/* lus, en tout : le pourcentage dans le titre, pendant le chargement */
static void em_progress(u64 got, u64 total)
{
	if (em_thumb)
		return;
	long pc = (long)udiv(got * 100, total);
	if (em_lastpc == pc)
		return;
	em_lastpc = pc;
	char *p = em_strcpy2(em_line, (const char *)D(EMD_NAME));
	p = em_strcpy2(p, em_sep);
	p = em_dec(pc, p);
	em_strcpy2(p, em_s5);
	ui_title((long)em_line);
}

/* le titre : la console - la ROM, sans son extension */
static void em_title_rom(void)
{
	char *e = (char *)str_cat((long)em_title, D(EMD_NAME));
	e = (char *)str_cat((long)e, (long)em_sep);
	const char *n = em_path;
	for (const char *s = em_path;;) {
		char c = *s++;
		if (!c)
			break;
		if (c == '/' || c == ':')
			n = s;
	}
	e = (char *)str_cat((long)e, (long)n);
	for (;;) { /* sans l'extension */
		e--;
		if (*e == '.') {
			*e = 0;
			break;
		}
		if (*e == '/' || *e == ' ')
			break;
	}
	ui_title((long)em_title);
}

/* em_path, son extension remplacée par .sav -> em_sav */
static void em_sav_name(void)
{
	const char *s = em_path;
	char *d = em_sav, *dot = 0; /* le dernier point */
	for (;;) {
		char c = *s++;
		*d++ = c;
		if (!c)
			break;
		if (c == '.')
			dot = d - 1;
		if (c == '/')
			dot = 0;
	}
	if (!dot)
		dot = d - 1;
	dot[0] = '.';
	dot[1] = 's';
	dot[2] = 'a';
	dot[3] = 'v';
	dot[4] = 0;
}

/* <rom>.sav -> EMD_RESTORE */
static void em_restore(void)
{
	if (!D(EMD_RESTORE))
		return;
	if (f_stat((long)em_sav, (long)em_stat) < 0)
		return;
	long size = em_stat[0];
	if (!size)
		return;
	long buf = em_map(size);
	if (!buf)
		return;
	long fd = f_open((long)em_sav, O_READ);
	if (fd < 0)
		return;
	long n = f_read(fd, buf, size);
	f_close(fd);
	if (n <= 0)
		return;
	((fn3)D(EMD_RESTORE))(CTX, buf, n);
}

/* lit em_path, la passe au cœur, puis <rom>.sav -> 1 */
static long em_load(void)
{
	if (em_gflags & 1) { /* (EMG_NOROM) */
		if (!((fn3)D(EMD_LOAD))(CTX, 0, 0))
			return 0;
		str_cat((long)em_title, D(EMD_NAME));
		ui_title((long)em_title);
		return 1;
	}
	if (f_stat((long)em_path, (long)em_stat) < 0)
		return 0;
	u64 size = (u64)em_stat[0];
	if (!size)
		return 0;
	long rom;
	if (em_bypath) {
		rom = (long)em_path; /* (trop grosse pour la mémoire : l'app lit ce qu'il lui faut) */
	} else {
		rom = em_map((long)size + 4096);
		if (!rom)
			return 0;
		em_rom = rom; /* la ROM reste en mémoire */
		long fd = f_open((long)em_path, O_READ);
		if (fd < 0)
			return 0;
		u64 got = 0;
		while (got < size) {
			u64 n = size - got;
			if (n > EM_CHUNK)
				n = EM_CHUNK;
			long r = f_read(fd, rom + (long)got, (long)n);
			if (r <= 0)
				break;
			got += (u64)r;
			em_progress(got, size);
		}
		f_close(fd);
		if (got != size)
			return 0;
	}
	if (!((fn3)D(EMD_LOAD))(CTX, rom, (long)size))
		return 0;
	if (!em_thumb)
		em_title_rom();
	em_sav_name();
	em_restore();
	return 1;
}

/* les images ; le tampon de sauvegarde -> 1 */
static long em_alloc(void)
{
	em_pitch = em_maxw << 2;
	long size = (em_maxw * em_maxh) << 2;
	const long *bufs = (const long *)D(EMD_BUFS);
	for (int i = 0; i < EM_NBUF; i++) {
		long p = bufs ? bufs[i] : 0;
		if (!p) {
			p = em_map(size);
			if (!p)
				return 0;
		}
		em_buf[i] = p;
		em_fw[i] = em_iw;
		em_fh[i] = em_ih;
	}
	em_savebuf = em_map(EM_SAVEBUF);
	return 1;
}

static void em_open_sound(void)
{
	em_stream = -1;
	if (!D(EMD_AUDIO) || !D(EMD_RATE))
		return;
	em_stream = ak_open(D(EMD_RATE), 2, 16, 0);
	if (em_stream >= 0)
		ak_latency(em_stream, 16384); /* ~90 ms d'avance au plus */
}

/* --thumb : EM_TH_FRAMES images sans les montrer, la dernière réduite à
 * EM_TW x EM_TH (pixels bruts), pour la bibliothèque de jeux */
static long em_thumbnail(void)
{
	em_iw = D(EMD_W);
	em_ih = D(EMD_H);
	em_maxw = D(EMD_MAXW) ? D(EMD_MAXW) : em_iw;
	em_maxh = D(EMD_MAXH) ? D(EMD_MAXH) : em_ih;
	if (!em_load() || !em_alloc())
		return 1;
	em_back = 0;
	for (long i = EM_TH_FRAMES; i; i--) {
		long b = 0; /* pas de boutons, sauf Start de temps en temps */
		if ((i & 0x100) && !(i & 0x10))
			b = PAD_START;
		((fn5)D(EMD_FRAME))(CTX, em_buf[0], em_pitch, b, 0);
		if (D(EMD_AUDIO))
			while (((fn3)D(EMD_AUDIO))(CTX, (long)em_pcm, EM_AUDIO) == EM_AUDIO)
				;
	}
	long o = em_map(EM_TW * EM_TH * 4);
	if (!o)
		return 1;
	u32 *d = (u32 *)o;
	const u8 *src = (const u8 *)em_buf[0];
	u64 pitch = (u64)em_pitch, fw = (u64)em_fw[0], fh = (u64)em_fh[0];
	for (u64 y = 0; y < EM_TH; y++) {
		const u32 *row = (const u32 *)(src + (y * fh / EM_TH) * pitch);
		for (u64 x = 0; x < EM_TW; x++)
			*d++ = row[x * fw / EM_TW];
	}
	long fd = f_open((long)em_thumb_out, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return 1;
	f_write(fd, o, EM_TW * EM_TH * 4);
	f_close(fd);
	return 0;
}

/* ---- la fenêtre */

/* les menus, avec ceux de l'application */
static void em_menus(void)
{
	long e = str_cat((long)em_menu_buf, (long)((em_gflags & 1) ? em_menu_norom : em_menu_game));
	if (D(EMD_MENU))
		str_cat(e, D(EMD_MENU));
	ui_menu((long)em_menu_buf);
	ui_menu_state(ID_SOUND, UMS_CHECKED);
}

/* -> 1 : em_path rempli */
static long em_pick(void)
{
	f_mkdir((long)em_dir0);
	f_mkdir((long)em_dir1);
	ui_file_filter(D(EMD_EXT));
	long r = ui_file_open((long)em_dir1, (long)em_path, EM_PATH);
	ui_file_filter(0);
	return r;
}

/* la zone de l'image : la plus grande qui garde les proportions, au centre */
static long em_layout(void)
{
	u64 w = (u64)ui_v.w, h = (u64)ui_v.h;
	u64 dw = w, dh = udiv(w * (u64)em_ih, (u64)em_iw);
	if (dh > h) {
		dh = h;
		dw = udiv(h * (u64)em_iw, (u64)em_ih);
	}
	em_vw = (long)dw;
	em_vh = (long)dh;
	/* (comme l'asm : la couleur est le jeton SOMBRE, 0 ou 1) */
	ui_fill(0, 0, (long)w, (long)h, ld32((const u8 *)em_tok + TJ_DARK));
	ui_update_all();
	em_vx = (long)((w - dw) >> 1);
	em_vy = (long)((h - dh) >> 1);
	if (!em_glfn) { /* (sinon l'application rend elle-même) */
		if (em_surf)
			ui_surface_move(em_surf, em_vx, em_vy, (long)dw, (long)dh);
		else
			em_surf = ui_surface(em_vx, em_vy, (long)dw, (long)dh, WS_SF_XRGB8888);
	}
	em_xsw = 0; /* (table à refaire) */
	return 0;
}

/* fonction de ui_on_resize : largeur, hauteur */
static long em_on_resize(long w, long h)
{
	(void)w;
	(void)h;
	return em_layout();
}

/* une image neuve ? On la prend et on la montre -> 1 */
static long em_take(void)
{
	if (em_glfn)
		return ((fn6)em_glfn)(em_glctx, ui_v.win, em_vx, em_vy, em_vw, em_vh);
	u32 m = __atomic_load_n(&em_mid, __ATOMIC_ACQUIRE);
	if (!(m & 4))
		return 0;
	u32 old = __atomic_exchange_n(&em_mid, (u32)em_front, __ATOMIC_ACQ_REL);
	long f = old & 3;
	em_front = f;
	u64 sw = (u64)em_fw[f], sh = (u64)em_fh[f]; /* taille de la source */
	const u8 *src = (const u8 *)em_buf[f];
	if ((u64)em_iw != sw || (u64)em_ih != sh) {
		em_iw = (long)sw;
		em_ih = (long)sh;
		em_layout();
	}
	if (!em_surf)
		return 0;
	r4 s;
	em_surface_pixels4(em_surf, &s);
	if (!s.r0)
		return 0;
	u8 *dst = (u8 *)s.r0;
	u64 dp = (u64)s.r1, dw = (u64)s.r2, dh = (u64)s.r3;
	/* table des colonnes (comme l'asm : au moins une colonne, une ligne) */
	if ((u64)em_xsw != sw || (u64)em_xdw != dw) {
		em_xsw = (long)sw;
		em_xdw = (long)dw;
		if (dw > EM_XMAP)
			return 0;
		u64 i = 0, acc = 0;
		do {
			em_xmap[i] = (u32)udiv(acc, dw);
			acc += sw;
			i++;
		} while (i < dw);
	}
	u64 sp = (u64)em_pitch;
	u64 y = 0, ysh = 0, prev = (u64)-1;
	do {
		u64 sy = udiv(ysh, dh);
		if (sy == prev) {
			/* la même : recopiée */
			const u32 *a = (const u32 *)(dst - dp);
			u32 *b = (u32 *)dst;
			for (u64 n = dw; n; n--)
				*b++ = *a++;
		} else {
			prev = sy;
			const u32 *row = (const u32 *)(src + sy * sp);
			u32 *b = (u32 *)dst;
			u64 x = 0;
			do {
				b[x] = row[em_xmap[x]];
				x++;
			} while (x < dw);
		}
		dst += dp;
		ysh += sh;
		y++;
	} while (y < dh);
	ui_surface_damage(em_surf, 0, 0, 0, 0);
	return 1;
}

/* la souris relative voulue par le jeu (emu_pointer), si la fenêtre a le clavier */
static void em_pointer_apply(void)
{
	long want = em_focus ? em_ptr_want : 0;
	if (want == em_ptr_on)
		return;
	em_ptr_on = want;
	ui_pointer_lock(want);
}

static void em_retitle(void)
{
	if (em_pause) {
		em_strcpy(em_line, em_title);
		em_cat0(em_line, em_paused);
		ui_title((long)em_line);
	} else if (!em_show) {
		ui_title((long)em_title);
	}
}

/* images montrées cette seconde : la vitesse dans le titre (F12) */
static void em_speed(long shown)
{
	u64 frames = (u64)em_frames; /* images faites */
	em_frames = 0;
	u64 spent = (u64)em_spent;
	em_spent = 0;
	if (!em_show || em_pause)
		return;
	/* ms par image, en dixièmes */
	u64 t = udiv(spent * 10000, (u64)em_hz);
	t = udiv(t, frames >= 1 ? frames : 1);
	em_strcpy(em_line, em_title);
	char *p = em_cat0(em_line, em_s1);
	p = em_dec((long)frames, p);
	p = em_strcpy2(p, em_s2);
	p = em_dec(shown, p);
	p = em_strcpy2(p, em_s3);
	p = em_dec((long)(t / 10), p);
	*p++ = '.';
	*p++ = (char)('0' + t % 10);
	em_strcpy2(p, em_s4);
	ui_title((long)em_line);
}

/* Plein écran : une fenêtre sans cadre, au-dessus de tout, de la taille de
 * l'écran ; la surface y passe, l'image s'étire en gardant ses proportions.
 * (F10 et F11 sont à Elegant : son diagnostic.) F9 ou Échap : retour à la fenêtre. */
static void em_fullscreen(void)
{
	if (!em_full) {
		em_win0 = ui_v.win;
		em_surf0 = em_surf;
		em_surf = 0;
		ui_screen((long)em_scr);
		if (ui_window_ex(0, 0, em_scr[0], em_scr[1], D(EMD_NAME), WF_FRAMELESS | WF_TOP) < 0) {
			em_surf = em_surf0;
		} else {
			em_full = 1;
			em_layout();
		}
	} else {
		ui_surface_free(em_surf);
		ui_window_close(0);
		ui_window_use(em_win0);
		em_surf = em_surf0;
		em_full = 0;
		em_layout();
	}
	em_ptr_on = 0; /* (une autre fenêtre : à reprendre) */
}

/* Game > Open : la machine s'arrête, une autre ROM, elle repart */
static void em_reopen(void)
{
	if (!em_pick())
		return;
	em_stop();
	em_title[0] = 0;
	if (!em_load())
		ui_message(D(EMD_NAME), (long)em_badrom, UM_OK);
	em_start();
}

/* id -> 1 : quitter */
static long em_do(long id)
{
	if (id == ID_QUIT)
		return 1;
	if ((u64)id >= ID_USER || id == ID_RESET) {
		em_cmd = id;
	} else if (id == ID_PAUSE) {
		em_pause ^= 1;
		ui_menu_state(ID_PAUSE, em_pause ? UMS_CHECKED : 0);
		em_retitle();
	} else if (id == ID_OPEN) {
		em_reopen();
	} else if (id == ID_MAX) {
		ui_maximize();
	} else if (id == ID_FULL) {
		em_fullscreen();
	} else if (id == ID_SPEED) {
		em_show ^= 1;
		ui_menu_state(ID_SPEED, em_show ? UMS_CHECKED : 0);
		em_retitle();
	} else if (id == ID_SOUND) {
		em_sound ^= 1;
		ui_menu_state(ID_SOUND, em_sound ? 0 : UMS_CHECKED);
	} else {
		u64 z = (u64)id - ID_ZOOM1;
		if (z <= 2)
			ui_resize(em_iw * (long)(z + 1), em_ih * (long)(z + 1));
	}
	return 0;
}

/* WEV_MOUSE : dans l'image -> EMD_MOUSE (comme l'asm : x et y de l'événement
 * pris sans signe, ramenés à 32 bits signés après la mise à l'échelle) */
static void em_mouse(void)
{
	if (!D(EMD_MOUSE))
		return;
	u64 x = EV32(EV_X), y = EV32(EV_Y);
	u32 kind = EV32(EV_KIND);
	if (kind != WEV_M_REL) {
		x -= (u64)em_vx;
		y -= (u64)em_vy;
		x = (u64)sdiv((long)(x * (u64)em_iw), em_vw);
		y = (u64)sdiv((long)(y * (u64)em_ih), em_vh);
	}
	((fn5)D(EMD_MOUSE))(CTX, kind, (s32)(u32)x, (s32)(u32)y, EV32(EV_CODE));
}

static void em_loop(void)
{
	u64 last = em_now();
	long shown = 0; /* images à l'écran */
	for (;;) {
		long type = ui_event(4);
		if (type == WEV_CLOSE)
			return;
		if (type == WEV_MENU) {
			if (em_do(EV32(EV_CODE)))
				return;
		} else if (type == WEV_KEY) {
			u32 kind = EV32(EV_KIND), code = EV32(EV_CODE);
			if (em_keyfn) { /* un jeu : tout lui va, sauf la vue */
				if (kind && code == KEY_F9)
					em_fullscreen();
				else if (kind && code == KEY_F12)
					em_do(ID_SPEED);
				else
					((fn5)em_keyfn)(CTX, code, kind, EV32(EV_CHAR), EV32(EV_MODS));
			} else if (kind) {
				if (code == KEY_F9 || (code == KEY_ESC && em_full)) {
					em_fullscreen();
				} else {
					long id = ui_menu_key(0);
					if (id >= 0 && em_do(id))
						return;
				}
			}
		} else if (type == WEV_FOCUS) {
			if (EV32(EV_WIN) == (u64)ui_v.win) { /* (la fenêtre en cours seulement) */
				u32 kind = EV32(EV_KIND);
				em_focus = kind;
				em_ptr_on = 0; /* (Elegant l'a repris) */
				if (!kind) {
					/* plus le clavier : tout est relâché */
					em_pad[0] = em_pad[1] = em_pad[2] = em_pad[3] = 0;
					if (em_keyfn)
						((fn5)em_keyfn)(CTX, EMG_KEY_NONE, 0, 0, 0);
				}
			}
		} else if (type == WEV_MOUSE) {
			em_mouse();
		} else if (type == WEV_PAD) {
			u32 i = EV32(4); /* l'état de cette manette */
			if (i < PAD_MAX) {
				em_pad[i] = EV32(WEV_PAD_BUTTONS);
				em_axes[i] = ld64(ui_v.ev + WEV_PAD_AXES);
			}
		} else if (type == WEV_WHEEL) {
			if (em_keyfn)
				((fn5)em_keyfn)(CTX, EMG_KEY_WHEEL, (s32)EV32(EV_Y), 0, 0);
		} else if (type == WEV_RESIZE) {
			ui_w_event(0); /* (ui_on_resize, em_layout) */
		}
		em_pointer_apply();
		shown += em_take();
		em_save_flush();
		u64 t = em_now();
		if (t - last >= (u64)em_hz) {
			last = t;
			em_speed(shown);
			shown = 0;
		}
	}
}

/* ---- emu_run */

/* descripteur EMD_*, arguments (« [--full | --thumb sortie] rom », 0 :
 * dialogue) -> code de sortie */
EXPORT long emu_run(long desc, long args)
{
	em_desc = desc;
	kits_open();
	const char *path = em_options((const char *)args);
	if (em_thumb) { /* (--thumb : sans fenêtre) */
		em_strcpy(em_path, path);
		em_hz = em_freq();
		return em_thumbnail();
	}
	ui_init((long)&ui_v);
	ui_screen((long)em_scr);
	em_hz = em_freq();
	long iw = D(EMD_W), ih = D(EMD_H);
	em_iw = iw;
	em_ih = ih;
	em_maxw = D(EMD_MAXW) ? D(EMD_MAXW) : iw;
	em_maxh = D(EMD_MAXH) ? D(EMD_MAXH) : ih;
	/* la fenêtre : le plus grand zoom entier qui tient, 3 au plus */
	u64 zx = udiv((u64)em_scr[0] - 8, (u64)iw);
	u64 zy = udiv((u64)em_scr[1] - (u64)em_scr[2] - (u64)em_scr[3] - 128, (u64)ih); /* cadre, dock */
	u64 z = zx > zy ? zy : zx;
	if (z > 3)
		z = 3;
	if (z < 1)
		z = 1;
	if (ui_window_ex(0, em_scr[3], iw * (long)z, ih * (long)z, D(EMD_NAME),
			 WF_RESIZABLE | WF_GLYPH_LIGHT | WF_BODY_DARK) < 0)
		return 1;
	ui_min_size(iw, ih);
	ui_on_resize((long)em_on_resize, 0);
	em_tok = ui_tokens();
	em_menus();
	em_layout();
	/* la ROM */
	if (path) {
		while (*path == ' ' || *path == '"') /* (un argument entre guillemets) */
			path++;
		char *e = em_strcpy(em_path, path) - 1;
		while (e > em_path && (e[-1] == ' ' || e[-1] == '"'))
			*--e = 0;
	} else if (!(em_gflags & 1) && !em_pick()) {
		return EME_CANCEL;
	}
	if (!em_load()) {
		const char *t = D(EMD_ERROR) ? (const char *)D(EMD_ERROR) : em_badrom;
		ui_message(D(EMD_NAME), (long)t, UM_OK);
		return 1;
	}
	if (!em_alloc())
		return 1;
	em_open_sound();
	em_start();
	if (em_wantfull)
		em_fullscreen();
	em_loop();
	em_stop();
	if (em_stream >= 0)
		ak_close(em_stream);
	ui_close();
	return 0;
}
