/* stickies - les notes épinglées sur le bureau (apps/stickies d'Onyx), lancé par la session
 * (/etc/session) : des cartes de papier en haut à droite de l'écran, sous toutes les fenêtres. Il
 * ne fait que lire ce que Notes écrit : /Notes/notes.ini (quelles notes sont épinglées, leurs
 * couleurs) et les notes .txt, les mêmes fichiers que sur Onyx (src/apps/notes/notesmodel.h).
 * Service « stickies » (op 2 : relire, op 3 : s'arrêter), comme stickies_proto.h ; relu aussi
 * toutes les 30 s. Au plus 6 cartes, les notes épinglées les plus récentes, chacune son titre et
 * 6 lignes. Un clic sur une carte ouvre Notes sur cette note, un clic sur la place vide ou sur
 * « +N autres » ouvre Notes. Sa place est fixe, sous la barre de menus. Il ne démarre pas quand la
 * configuration de Notes dit stickies = 0.
 *
 * Traduit ligne à ligne de stickies.asm (le PC n'avait pas l'applet). */
#include "sys_consts.h"
#include "uikit_import.h"

#define W_ 240
#define HDR 34
#define CX 12
#define CWD 216
#define CPAD 10
#define CBAND 5
#define CGAP 12
#define CR 6
#define TOP 44
#define LINES 6
#define LINE_H 17
#define TITLE_H 18
#define CARDS 6
#define EMPTY_H 62
#define MORE_H 22
#define NOTE_FILE 72
#define PIN_MAX 64
#define NOTE_HEAD 2048
#define NOTE_INK 0x002B2925u
#define FBUF 32768
#define ALPHA 0x28000000u
#define CLEAR 0xFF000000u
#define OP_RELOAD 2
#define OP_QUIT 3
#define HIT_MORE 100
#define APPLET_X 16 /* place fixe (src/inc/applet.inc) */
#define APPLET_Y 44
#define APPLET_BAS 64 /* (panel compris) */

struct pin { /* une note épinglée (son empreinte compte : pas de trous) */
	char file[NOTE_FILE];
	u64 col, mod;
};
struct card {
	struct pin *pin;
	i64 y, h;
	char *title, *body; /* dans heads */
};

static struct krt_ui ui;
static long g_svc = -1;
static long g_x, g_h;
static i64 g_npin, g_ncard, g_shown, g_moreY = -1, g_hot = -1;
static u32 g_sig;
static u64 g_ticks;
static int cur_pin;
static u64 cur_col, cur_mod;
static long scr[4];
static u8 msg[WS_MSG];
static char textbuf[96];
static char pathbuf[128];
static char cur_name[NOTE_FILE];
static struct card g_cards[CARDS];
static struct pin g_pins[PIN_MAX];
static char heads[CARDS][NOTE_HEAD + 8];
static char fbuf[FBUF];

static const u32 t_paper[6] = { 0x00FCE9A6, 0x00D3EBC6, 0x00CFE0F3, 0x00F8D3D8, 0x00E2D6F0, 0x00E4E2DE };
static const u32 t_dot[6] = { 0x00E8B21F, 0x0067A657, 0x005284C4, 0x00D9667A, 0x008A68C2, 0x00908C86 };
static const char *const s_colours[7] = { "yellow", "green", "blue", "pink", "purple", "grey", "gray" };
static const char s_title[] = "Notes \xC3\xA9pingl\xC3\xA9" "es";
static const char s_dir[] = "/Notes/";
static const char s_ini_notes[] = "/Notes/notes.ini";
static const char s_cfg[] = "/apps/notes.app/config.ini";       /* (l'ancienne place) */
static const char s_cfg_u[] = "HOME:/.settings/notes/config.ini"; /* (celle de l'utilisateur, depuis le 25/11) */
static const char p_notes[] = "/apps/notes.app/main";

static u32 token(long off)
{
	return *(const u32 *)((const u8 *)ui_tokens() + off);
}

/* chemin -> octets lus dans buf (cap au plus ; 0 : pas de fichier) */
static u64 read_into(const char *path, char *buf, u64 cap)
{
	long n = 0, fd = sys_open(path, O_READ);
	if (fd >= 0) {
		n = sys_read(fd, buf, cap);
		if (n < 0)
			n = 0;
		sys_close(fd);
	}
	return (u64)n;
}

/* chemin -> octets dans fbuf, terminé par 0 */
static u64 read_file(const char *path)
{
	u64 n = read_into(path, fbuf, FBUF - 1);
	fbuf[n] = 0;
	return n;
}

/* ligne, clé -> sa valeur (0 : pas cette clé) : « clé = valeur » */
static const char *key_value(const char *l, const char *k)
{
	while (*k)
		if (*l++ != *k++)
			return 0;
	while (*l == ' ')
		l++;
	if (*l != '=')
		return 0;
	l++;
	while (*l == ' ')
		l++;
	return l;
}

/* -> 1 si la configuration de Notes dit stickies = 0 */
static int cfg_off(void)
{
	long st[2];
	if (!read_file(sys_stat(s_cfg_u, st) >= 0 ? s_cfg_u : s_cfg))
		return 0;
	const char *p = fbuf;
	for (;;) {
		const char *v = key_value(p, "stickies");
		if (v)
			return *v == '0';
		while (*p && *p != 10)
			p++;
		if (!*p)
			return 0;
		p++;
	}
}

/* mot de notes.ini -> couleur (0 jaune...) */
static u64 colour_of(const char *w)
{
	for (u64 i = 0; i < 7; i++)
		if (str_eq(w, s_colours[i]))
			return i == 6 ? 5 : i; /* (« gray ») */
	return 0;
}

/* (une fin de section) la note cur_* s'ajoute aux épinglées, si son fichier existe */
static void pin_flush(void)
{
	int pinned = cur_pin;
	cur_pin = 0;
	if (!pinned || g_npin >= PIN_MAX)
		return;
	*pathbuf = 0;
	str_cat(str_cat(pathbuf, s_dir), cur_name);
	long st[2];
	if (sys_stat(pathbuf, st) != 0)
		return;
	struct pin *p = &g_pins[g_npin++];
	memcpy(p->file, cur_name, NOTE_FILE);
	p->col = cur_col;
	p->mod = cur_mod;
}

/* notes.ini lu : g_pins, les plus récentes d'abord */
static void read_pins(void)
{
	g_npin = 0;
	cur_pin = 0;
	u64 n = read_file(s_ini_notes);
	char *p = fbuf, *end = fbuf + n;
	while (p < end) {
		char *l = p;
		while (p < end && *p != 10)
			p++;
		char *le = p++;
		while (le > l && (u8)le[-1] <= ' ') /* coupée, sans blancs */
			le--;
		*le = 0;
		if (*l == '[') { /* « [nom] » */
			pin_flush();
			cur_col = 0;
			cur_mod = 0;
			u64 i = 0;
			for (const char *s = l + 1; *s && *s != ']' && i < NOTE_FILE - 1; s++)
				cur_name[i++] = *s;
			cur_name[i] = 0;
			continue;
		}
		const char *v;
		if ((v = key_value(l, "pinned")))
			cur_pin = *v != '0';
		else if ((v = key_value(l, "colour")))
			cur_col = colour_of(v);
		else if ((v = key_value(l, "modified")))
			cur_mod = parse_dec(v);
	}
	pin_flush();
	for (i64 i = 1; i < g_npin; i++) /* par date, les plus récentes d'abord (insertion) */
		for (i64 j = i; j > 0 && g_pins[j - 1].mod < g_pins[j].mod; j--) {
			struct pin t = g_pins[j];
			g_pins[j] = g_pins[j - 1];
			g_pins[j - 1] = t;
		}
}

static u32 fnv(u32 h, const void *b, u64 n)
{
	const u8 *p = b;
	while (n--) {
		h ^= *p++;
		h *= 16777619u;
	}
	return h;
}

/* carte i : le début de sa note, dans heads ; titre et corps repérés */
static void read_head(i64 i)
{
	char *t = heads[i];
	struct card *c = &g_cards[i];
	c->pin = &g_pins[i];
	*pathbuf = 0;
	str_cat(str_cat(pathbuf, s_dir), c->pin->file);
	u64 n = read_into(pathbuf, t, NOTE_HEAD);
	t[n] = 0;
	if (n >= 3 && (u8)t[0] == 0xEF && (u8)t[1] == 0xBB) { /* (une marque BOM : sautée) */
		mem_move(t, t + 3, n - 2);
		n -= 3;
	}
	for (u64 k = 0; k < n; k++) /* \r : \n */
		if (t[k] == 13) {
			t[k] = 10;
			if (t[k + 1] == 10)
				t[k + 1] = ' ';
		}
	char *s = t; /* première ligne non vide */
	while (*s && (u8)*s <= ' ')
		s++;
	c->title = s;
	while (*s && *s != 10)
		s++;
	if (*s == 10) {
		*s++ = 0;
		while (*s == 10 || *s == ' ') /* le corps : sans blancs devant */
			s++;
	}
	c->body = s;
}

/* relit les notes -> 1 si ce qui est montré change */
static int reload(void)
{
	read_pins();
	g_ncard = g_npin > CARDS ? CARDS : g_npin;
	for (i64 i = 0; i < g_ncard; i++)
		read_head(i);
	u32 h = fnv(2166136261u, g_pins, (u64)g_npin * sizeof g_pins[0]);
	h = fnv(h, heads, (u64)g_ncard * sizeof heads[0]);
	int changed = h != g_sig;
	g_sig = h;
	return changed;
}

/* ---- souris */

/* point -> carte, HIT_MORE, -1 : rien */
static i64 hit(i64 x, i64 y)
{
	if (y < HDR || x < CX || x >= CX + CWD)
		return -1;
	if (!g_npin)
		return y >= TOP && y < TOP + EMPTY_H ? HIT_MORE : -1;
	for (i64 i = 0; i < g_shown; i++)
		if (y >= g_cards[i].y && y < g_cards[i].y + g_cards[i].h)
			return i;
	if (g_moreY >= 0 && y >= g_moreY && y < g_moreY + MORE_H)
		return HIT_MORE;
	return -1;
}

static void draw(void);

static void on_mouse(void)
{
	u32 kind = *(u32 *)(ui.ev + EV_KIND);
	i64 x = *(i32 *)(ui.ev + EV_X), y = *(i32 *)(ui.ev + EV_Y);
	i64 h = hit(x, y);
	if (kind == 1) { /* appui */
		if (y < HDR || h < 0)
			return;
		const char *args = "";
		if (h != HIT_MORE) { /* Notes, sur cette note */
			*pathbuf = 0;
			str_cat(str_cat(pathbuf, s_dir), g_cards[h].pin->file);
			args = pathbuf;
		}
		ksys(SYS_SPAWN, (long)p_notes, (long)args, 0, 0, 0, 0);
		return;
	}
	if (kind == 2 || h == g_hot)
		return;
	g_hot = h;
	draw();
}

/* ---- dessin */

/* carte : sa hauteur, d'après son texte */
static i64 card_measure(struct card *c)
{
	i64 h = ui_text_wrap((long)c->body, 0, 0, CWD - 2 * CPAD, 0, (long)ui.font, 13, 1);
	if (h > LINES * LINE_H)
		h = LINES * LINE_H;
	if (h < LINE_H)
		h = LINE_H;
	return h + CBAND + 2 * CPAD + TITLE_H;
}

static void draw_card(struct card *c, i64 i)
{
	i64 y = c->y, h = c->h;
	u32 paper = t_paper[c->pin->col], dot = t_dot[c->pin->col];
	ui_rbox(CX + 1, y + 3, CWD, h, CR + 1, 0xC8000000, 0xC8000000); /* l'ombre */
	if (g_hot == i) /* survolée : un contour */
		ui_rbox(CX - 1, y - 1, CWD + 2, h + 2, CR + 1, 0x00FFFFFF, 0x00FFFFFF);
	ui_rbox(CX, y, CWD, 20, CR, dot, dot);
	ui_rbox(CX, y + CBAND, CWD, h - CBAND, CR, paper, paper); /* le papier */
	const char *t = *c->title ? c->title : lang_tr("Nouvelle note");
	ui_text_fit((long)t, CX + CPAD, y + CBAND + CPAD + 11, NOTE_INK, 13, (long)ui.font_b, CWD - 2 * CPAD);
	ui_clip(CX, y + CBAND + CPAD + TITLE_H, CWD, h - (CBAND + 2 * CPAD + TITLE_H)); /* le corps, coupé */
	u32 ink = (u32)ui_mix(paper, NOTE_INK, 215);
	ui_text_wrap((long)c->body, CX + CPAD, y + CBAND + CPAD + TITLE_H, CWD - 2 * CPAD, ink, (long)ui.font, 13, 0);
	ui_clip(0, 0, 0, 0);
}

static void text13(const char *s, long x, long y, u32 col, const u8 *font, long maxw)
{
	ui_text_fit((long)s, x, y, col, 13, (long)font, maxw);
}

static void draw(void)
{
	u32 glass = (token(TJ_SURFACE) & 0xFFFFFF) | ALPHA;
	ui_fill(0, 0, W_, g_h, CLEAR); /* tout transparent */
	ui_rbox(0, 0, W_, HDR, 10, glass, glass); /* le titre */
	ui_rbox(14, 9, 16, 16, 2, t_paper[0], t_paper[0]); /* une petite note jaune */
	ui_fill(17, 14, 8, 1, 0x00A08850);
	ui_fill(17, 17, 6, 1, 0x00A08850);
	long x = ui_text((long)lang_tr(s_title), 38, 23, token(TJ_TEXT1), 14, (long)ui.font_b, 0);
	if (g_npin) {
		char *e = textbuf;
		*e++ = '(';
		e = fmt_dec((u64)g_npin, e);
		*e++ = ')';
		*e = 0;
		ui_text((long)textbuf, x + 6, 23, token(TJ_TEXT2), 13, (long)ui.font, 0);
	}
	i64 y = TOP;
	g_shown = 0;
	g_moreY = -1;
	for (i64 i = 0; i < g_ncard; i++) {
		struct card *c = &g_cards[i];
		i64 h = card_measure(c);
		c->y = y;
		c->h = h;
		if (y + h > g_h - MORE_H && i) /* (plus de place : « +N autres ») */
			break;
		draw_card(c, i);
		y += h + CGAP;
		g_shown = i + 1;
	}
	if (!g_npin) { /* rien d'épinglé */
		ui_rbox(CX, TOP, CWD, EMPTY_H, 8, glass, glass);
		text13(lang_tr("Aucune note \xC3\xA9pingl\xC3\xA9" "e"), CX + 14, TOP + 26, token(TJ_TEXT1), ui.font_b, CWD - 28);
		text13(lang_tr("Cliquer pour ouvrir Notes"), CX + 14, TOP + 46, token(TJ_TEXT2), ui.font, CWD - 28);
	} else if (g_shown < g_npin) { /* « +N autres dans Notes » */
		y -= 4;
		g_moreY = y;
		ui_rbox(CX, y, CWD, MORE_H, 6, glass, glass);
		char *e = textbuf;
		*e++ = '+';
		e = fmt_dec((u64)(g_npin - g_shown), e);
		*e = 0;
		str_cat(e, lang_tr(" autres dans Notes"));
		text13(textbuf, CX + 8, y + 16, token(TJ_TEXT1), ui.font, CWD - 16);
	}
	ui_update_all();
}

static void refresh(void)
{
	if (reload())
		draw();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (cfg_off()) /* (caché par Notes) */
		return 0;
	g_svc = korin_sys2(SYS_SVC_REGISTER, "stickies", 0);
	if (g_svc < 0)
		return 0;
	ui_init((long)&ui);
	ui_screen((long)scr); /* en haut à droite, jusqu'au panel */
	g_x = scr[0] - W_ - APPLET_X;
	g_h = scr[1] - APPLET_Y - APPLET_BAS;
	reload();
	if (ui_window_ex(g_x, APPLET_Y, W_, g_h, (long)lang_tr(s_title),
			 WF_FRAMELESS | WF_BACK | WF_NOFOCUS | WF_NOLIST | WF_ALPHA) < 0)
		return 0;
	draw();
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, g_svc, msg, 0); /* Notes : relire, s'arrêter */
		if (req > 0) {
			u64 op = *(u64 *)msg;
			*(u64 *)msg = 0;
			korin_sys2(SYS_SVC_REPLY, req, msg);
			if (op == OP_QUIT)
				return 0;
			refresh();
		}
		long e = ui_event(1000);
		if (e == WEV_THEME) /* autre thème : redessinées */
			draw();
		else if (e == WEV_MOUSE)
			on_mouse();
		else if (e == WEV_CLOSE)
			return 0;
		else if (!e && ++g_ticks >= 30) {
			g_ticks = 0;
			if (cfg_off())
				return 0;
			refresh();
		}
	}
}
