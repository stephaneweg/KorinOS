/* uk_theme.c -- UIKit : le thème du bureau (/etc/theme.txt, les mêmes clés
 * qu'Onyx), les jetons TJ_*, les nuances de couleur, les boîtes arrondies
 * (ui_rbox et ses masques de coins). Port ligne à ligne de uikit_theme.inc et
 * uikit_rmask.inc (src/kits/uikit/PORT.md).
 *
 * /etc/theme.txt (« clé = valeur », # : commentaire) :
 *	theme    = Givre | Onyx Noir
 *	active   = 0xRRGGBB            (cadre de la fenêtre active)
 *	inactive = 0xRRGGBB            (cadres des autres)
 *	window   = 0xRRGGBB            (fond des fenêtres)
 *	accent   = 0xRRGGBB ou un nom de uk_accents (focus, sélection)
 *	menubar  = auto | 0xRRGGBB     (auto : comme window)
 *	dock     = 0xRRGGBB
 *	outline  = none | dark | black
 *	density  = normal | touch      (touch : contrôles, lignes et menus de 36)
 * Un thème nommé donne sa palette ; les clés écrites ensuite la corrigent.
 * Sans fichier, ou nom inconnu : Givre. Le dessin passe par les jetons. */
#include "uk.h"

/* ---- données (aucun pointeur) -------------------------------------------- */

/* thèmes nommés : nom (16 o), cadre, style (uk_named, UK_NAMED_SIZE = 32) */
struct uk_named_ent {
	char name[16];
	u64 frame, style;
};
static const struct uk_named_ent uk_named[3] = {
	{"givre", 0x00E9EDF2, UI_STYLE_FLAT},
	{"onyx noir", 0x001F2228, UI_STYLE_FLAT},
	{"", 0, 0}, /* dd 0 : fin (seul le premier octet compte) */
};

/* palette : fenêtre, accent, cadres inactifs, dock (uk_palettes, dd) */
static const u64 uk_palettes[4] = {0x00F5F6F8, 0x002563D8, 0x00F5F6F8, 0x00F5F6F8};

/* jetons des thèmes Givre et Onyx Noir (TJ_*, dans l'ordre ; DESIGN-ELEGANT 3.1) */
static const u32 uk_jet_givre[TJ_TABLE / 4] = {
	0x00F5F6F8, 0x00FFFFFF, 0x00ECEEF2, 0x00FFFFFF, 0x00FFFFFF, 0x00E2E5EA,
	0x00B8BFC9, 0x0014171C, 0x004C5563, 0x005F6877, 0x002563D8, 0x00E4ECFB,
	0x001D4FB3, 0x002563D8, 0x00D93A3F, 0x001E8A5A, 0x00C4861C, 0x00F7F8FA,
	0x00F5F6F8, 0x00E9EDF2, 0x00F5F6F8, 0xC4000000, 0, 0x00FFFFFF,
};
static const u32 uk_jet_noir[TJ_TABLE / 4] = {
	0x0015171B, 0x001B1E23, 0x00101216, 0x00262A31, 0x00262A31, 0x002A2E36,
	0x00525966, 0x00ECEEF2, 0x00B0B6C0, 0x008B929D, 0x002F72DC, 0x001C2F4D,
	0x002560C0, 0x007DB4F2, 0x00D93A3F, 0x0046A758, 0x00F1B33B, 0x0015171B,
	0x001B1E23, 0x001F2228, 0x0015171B, 0x40000000, 1, 0x00262A31,
};

/* jetons d'après la table (TJ_HOVER, TJ_AXIS_X, TJ_AXIS_Y, TJ_AXIS_Z), Givre puis Onyx Noir : le survol, et les
 * axes des vues 3D, qui ne suivent pas l'accent (docs/DESIGN-CAO.md §10.2) */
static const u32 uk_jet_more[2][4] = {
	{0x00E9ECF1, 0x00D93A3F, 0x001E8A5A, 0x002563D8},
	{0x0023272E, 0x00E5534B, 0x0046A758, 0x004C8DF0},
};

/* couleurs d'accent : nom (16 o) dans theme.txt, Givre, Onyx Noir
 * (uk_accents, UK_ACC_SIZE = 24) ; une entrée vide finit la table */
struct uk_accent_ent {
	char name[16];
	u32 col[2];
};
static const struct uk_accent_ent uk_accents[9] = {
	{"bleu", {0x002563D8, 0x002F72DC}},
	{"orange", {0x00CF3F0A, 0x00D04510}},
	{"vert", {0x005FBF12, 0x00A6E22E}},
	{"jaune", {0x00F2B705, 0x00FFD02E}},
	{"violet", {0x007340E0, 0x007E52E6}},
	{"fuchsia", {0x00C2188F, 0x00D12A9C}},
	{"sarcelle", {0x000B7F7A, 0x000D827C}},
	{"graphite", {0x003E4A5B, 0x00A4AEBD}},
	{"", {0, 0}},
};

static const char uk_k_theme[] = "theme";
static const char uk_k_active[] = "active";
static const char uk_k_inactive[] = "inactive";
static const char uk_k_window[] = "window";
static const char uk_k_accent[] = "accent";
static const char uk_k_menubar[] = "menubar";
static const char uk_k_dock[] = "dock";
static const char uk_k_outline[] = "outline";
static const char uk_k_button[] = "button";
static const char uk_k_field[] = "field";
static const char uk_k_echelle[] = "echelle";
static const char uk_k_density[] = "density";
static const char uk_k_densite[] = "densite";
static const char uk_v_touch[] = "touch";
static const char uk_v_tactile[] = "tactile";
static const char uk_v_auto[] = "auto";
static const char uk_v_none[] = "none";
static const char uk_v_black[] = "black";
static const char uk_bus_name[] = "bus";             /* BUS_SVC_NAME */
static const char uk_theme_topic[] = "korin.theme";  /* BUS_TOPIC_THEME */

/* uikit_rmask.inc (généré par tools/kits/rmask.py) : couverture des coins
 * arrondis de ui_rbox, rayons 1 à 16. Coin haut-gauche, r x r octets :
 * 0 dehors, 255 dedans, sinon couverture. */
static const u16 uk_rm_off[17] = {0, 0, 1, 5, 14, 30, 55, 91, 140, 204, 285, 385, 506, 650, 819, 1015, 1240};
static const u8 uk_rm[1496] = {
	202,
	81, 234,
	234, 255,
	6, 146, 241,
	146, 255, 255,
	241, 255, 255,
	0, 42, 178, 245,
	42, 240, 255, 255,
	178, 255, 255, 255,
	245, 255, 255, 255,
	0, 0, 81, 193, 247,
	0, 140, 255, 255, 255,
	81, 255, 255, 255, 255,
	193, 255, 255, 255, 255,
	247, 255, 255, 255, 255,
	0, 0, 8, 114, 205, 249,
	0, 28, 212, 255, 255, 255,
	8, 212, 255, 255, 255, 255,
	114, 255, 255, 255, 255, 255,
	205, 255, 255, 255, 255, 255,
	249, 255, 255, 255, 255, 255,
	0, 0, 0, 25, 135, 212, 250,
	0, 0, 92, 243, 255, 255, 255,
	0, 92, 254, 255, 255, 255, 255,
	25, 243, 255, 255, 255, 255, 255,
	135, 255, 255, 255, 255, 255, 255,
	212, 255, 255, 255, 255, 255, 255,
	250, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 48, 151, 218, 250,
	0, 0, 10, 153, 254, 255, 255, 255,
	0, 10, 192, 255, 255, 255, 255, 255,
	0, 153, 255, 255, 255, 255, 255, 255,
	48, 254, 255, 255, 255, 255, 255, 255,
	151, 255, 255, 255, 255, 255, 255, 255,
	218, 255, 255, 255, 255, 255, 255, 255,
	250, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 1, 73, 163, 223, 251,
	0, 0, 0, 42, 200, 255, 255, 255, 255,
	0, 0, 66, 243, 255, 255, 255, 255, 255,
	0, 42, 243, 255, 255, 255, 255, 255, 255,
	1, 200, 255, 255, 255, 255, 255, 255, 255,
	73, 255, 255, 255, 255, 255, 255, 255, 255,
	163, 255, 255, 255, 255, 255, 255, 255, 255,
	223, 255, 255, 255, 255, 255, 255, 255, 255,
	251, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 8, 92, 173, 225, 252,
	0, 0, 0, 0, 88, 229, 255, 255, 255, 255,
	0, 0, 3, 151, 255, 255, 255, 255, 255, 255,
	0, 0, 151, 255, 255, 255, 255, 255, 255, 255,
	0, 88, 255, 255, 255, 255, 255, 255, 255, 255,
	8, 229, 255, 255, 255, 255, 255, 255, 255, 255,
	92, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	173, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	225, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	252, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 19, 109, 181, 228, 252,
	0, 0, 0, 0, 9, 132, 245, 255, 255, 255, 255,
	0, 0, 0, 35, 212, 255, 255, 255, 255, 255, 255,
	0, 0, 35, 227, 255, 255, 255, 255, 255, 255, 255,
	0, 9, 212, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 132, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	19, 245, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	109, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	181, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	228, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	252, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 0, 33, 122, 186, 230, 253,
	0, 0, 0, 0, 0, 32, 167, 253, 255, 255, 255, 255,
	0, 0, 0, 0, 92, 243, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 120, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 92, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 32, 243, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 167, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	33, 253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	122, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	186, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	230, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 0, 0, 49, 131, 192, 232, 253,
	0, 0, 0, 0, 0, 0, 65, 198, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 10, 156, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 21, 202, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 10, 202, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 156, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 65, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 198, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	49, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	131, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	192, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	232, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	253, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 0, 0, 2, 66, 140, 196, 233, 254,
	0, 0, 0, 0, 0, 0, 4, 102, 220, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 42, 205, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 75, 243, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 75, 249, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 42, 243, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 4, 205, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 102, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	2, 220, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	66, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	140, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	196, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	233, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 77, 148, 201, 234, 254,
	0, 0, 0, 0, 0, 0, 0, 16, 132, 235, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 90, 235, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 5, 153, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 5, 177, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 153, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 90, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 16, 235, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 132, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	7, 235, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	77, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	148, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	201, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	234, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 90, 155, 205, 235, 255,
	0, 0, 0, 0, 0, 0, 0, 0, 36, 156, 246, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 0, 10, 140, 251, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 0, 37, 212, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 0, 51, 234, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 0, 37, 234, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 10, 212, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 0, 140, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 36, 251, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	0, 156, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	15, 246, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	90, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	155, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	205, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	235, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
};

/* uk_tv : valeurs lues, par décalage (+0 thème (index), +8 style, +16 active,
 * +24 inactive, +32 window, +40 accent, +48 menubar, +56 dock, +64 outline,
 * +72 thème effectif, +80 button, +88 field, +96 échelle, +104 densité) */
#define TV(o) uk.tv[(o) / 8]

static void theme_parse(long text);
static void theme_apply(void);
static long tokens_calc(void);

/* ---- thème, jetons -------------------------------------------------------- */

/* ui_theme : -> thème du bureau (TH_*, lecture seule) */
long ui_theme(void)
{
	return UKA(theme);
}

/* ui_theme_text : x0 = texte d'un theme.txt (modifié ; 0 : relire
 * /etc/theme.txt) : le thème et les jetons de l'application deviennent
 * ceux-ci. Rend (comme l'asm) l'encre de l'accent, dernier calcul de
 * uk_jetons_calc. */
long ui_theme_text(long text)
{
	long i;
	if (!text)
		return uk_theme_load();
	for (i = 0; i < UK_TV_N; i++) /* valeurs lues : aucune */
		uk.tv[i] = UK_NONE;
	theme_parse(text);
	theme_apply();
	return tokens_calc();
}

/* uk_theme_load : thème par défaut, puis /etc/theme.txt */
long uk_theme_load(void)
{
	long i, buf, fd, n;
	uk_kits();
	for (i = 0; i < UK_TV_N; i++) /* valeurs lues : aucune */
		uk.tv[i] = UK_NONE;
	/* comme l'asm : le tampon de 2 Ko n'est jamais rendu (un par relecture) */
	buf = uk_malloc(2048);
	if (buf) {
		fd = SYS2(SYS_OPEN, uk_conf_path("theme.txt", 0), O_READ); /* personnel, sinon /etc */
		if (fd >= 0) {
			n = SYS3(SYS_READ, fd, buf, 2047);
			SYS1(SYS_CLOSE, fd);
			if (n > 0) {
				STB(buf, n, 0);
				theme_parse(buf);
			}
		}
	}
	theme_apply();
	return tokens_calc();
}

/* uk_theme_watch : abonnement à korin.theme (busd, docs/BUS.md) : un appel au
 * service, sans BusKit ; ui_event relira le thème. Sans busd : rien. Le
 * canal reste ouvert (comme l'asm). -> canal (< 0) ou réponse du service. */
long uk_theme_watch(void)
{
	long ch, r, d, s;
	u8 c;
	ch = SYS2(SYS_SVC_OPEN, S(uk_bus_name), 0);
	if (ch < 0)
		return ch;
	STX(uk.msg, 0, BUS_OP_SUBSCRIBE); /* op, a = 0 (pas d'état courant) */
	STX(uk.msg, 8, 0);
	d = UKA(msg) + BUS_DATA;
	s = S(uk_theme_topic);
	do {
		c = (u8)LDB(s, 0);
		s++;
		STB(d, 0, c);
		d++;
	} while (c);
	r = SYS2(SYS_SVC_CALL, ch, UKA(msg)); /* -> numéro d'abonnement */
	if (r > 0)
		uk.theme_sub = r;
	return r;
}

/* uk_retheme : korin.theme publié (ui_event) : le thème relu, puis chaque
 * fenêtre redessinée ; la fenêtre courante le reste. Fenêtre à cadre avec des
 * widgets ou une fonction ui_on_resize : fond FACE, cadre, puis la fonction
 * (rappel x0 = largeur, x1 = hauteur), sinon les widgets. Sans : le cadre
 * seul. Sans cadre : les widgets. (Valeur rendue : sans usage, 0.) */
long uk_retheme(void)
{
	long cur, b, id, w;
	uk_theme_load();
	uk.scale = -1; /* (l'échelle dépend du thème) */
	cur = uk_win();
	for (b = uk.wins; b; b = LDX(b, WB_NEXT)) {
		id = LDX(b, WB_ID);
		if (!id)
			continue;
		ui_window_use(id);
		if (!uk.chr_a)
			goto wid; /* sans cadre */
		if (uk.resize_fn)
			goto full;
		for (w = uk.wfirst; w; w = LDX(w, UW_NEXT)) /* des widgets ici ? */
			if (LDX(w, UW_WIN) == id)
				goto full;
		uk_draw_chrome();
		ui_update_all();
		continue;
	full:
		uk.clip_on = 0; /* le fond, tout entier */
		ui_fill(0, 0, UVX(UV_W), UVX(UV_H), JET(TJ_FACE));
		uk_draw_chrome();
		if (uk.resize_fn) {
			UK_CB2(uk.resize_fn, UVX(UV_W), UVX(UV_H));
			continue;
		}
	wid:
		ui_w_draw(0);
		ui_update_all();
	}
	if (cur)
		ui_window_use(cur);
	return 0;
}

/* uk_theme_apply : valeurs lues (uk_tv) -> thème (uk_theme) */
static void theme_apply(void)
{
	long th = UKA(theme), t, a;
	uk.mt_ok = 0; /* (métriques à recalculer : échelle, densité) */
	t = TV(0);
	if (t < 0) /* sans thème : Givre (style plat) */
		t = UK_GIVRE;
	STX(th, TH_STYLE, uk_named[t].style);
	STX(th, TH_ACTIVE, uk_named[t].frame);
	TV(72) = t; /* (thème effectif, pour les jetons) */
	STX(th, TH_WINDOW, uk_palettes[0]);
	STX(th, TH_ACCENT, uk_palettes[1]);
	STX(th, TH_INACTIVE, uk_palettes[2]);
	STX(th, TH_DOCK, uk_palettes[3]);
	STX(th, TH_OUTLINE, 1); /* contour sombre */
	a = TV(40); /* accent nommé : sa valeur dans ce thème */
	if (a >= 0 && (a & (1L << 30))) {
		a &= 0xFF;
		/* comme l'asm : uk_tv garde ensuite la couleur, plus le nom */
		TV(40) = LDW(S(uk_accents) + a * UK_ACC_SIZE + 16, t * 4);
	}
	if (TV(16) >= 0) /* clés écrites */
		STX(th, TH_ACTIVE, TV(16));
	if (TV(24) >= 0)
		STX(th, TH_INACTIVE, TV(24));
	if (TV(32) >= 0)
		STX(th, TH_WINDOW, TV(32));
	if (TV(40) >= 0)
		STX(th, TH_ACCENT, TV(40));
	if (TV(56) >= 0)
		STX(th, TH_DOCK, TV(56));
	if (TV(64) >= 0)
		STX(th, TH_OUTLINE, TV(64));
	a = TV(48); /* barre de menus : auto = fenêtre */
	if (a < 0)
		a = LDX(th, TH_WINDOW);
	STX(th, TH_MENUBAR, a);
}

/* uk_bright : couleur -> luminosité 0..255 */
static long bright(long c)
{
	u32 r = ((u32)c >> 16) & 0xFF, g = ((u32)c >> 8) & 0xFF, b = (u32)c & 0xFF;
	u32 v = r * 77;
	v = g * 151 + v;
	v = b * 28 + v;
	return (long)(v >> 8);
}

/* uk_accent_set : accent (w0) et ses nuances dans les jetons */
static void accent_set(long acc)
{
	long j = UKA(tokens);
	u32 a = (u32)acc;
	STW(j, TJ_ACCENT, a);
	if (!LDW(j, TJ_DARK)) {
		STW(j, TJ_ACCENT_LIGHT, ui_mix(0x00FFFFFF, a, 30)); /* Givre : clair presque blanc */
		long br = bright(a); /* vif : assombri s'il est lumineux */
		STW(j, TJ_ACCENT_VIVID, a);
		if (UGT(br, 140))
			STW(j, TJ_ACCENT_VIVID, ui_tone(a, 88));
	} else {
		STW(j, TJ_ACCENT_LIGHT, ui_mix(LDW(j, TJ_FACE), a, 64)); /* Noir : clair sombre */
		STW(j, TJ_ACCENT_VIVID, ui_mix(a, 0x00FFFFFF, 100)); /* vif éclairci */
	}
	STW(j, TJ_ACCENT_DARK, ui_tone(a, 104));
}

/* uk_jetons_calc : thème (uk_theme, uk_tv) -> jetons du style plat. Rend
 * (comme l'asm) l'encre de l'accent. */
static long tokens_calc(void)
{
	long j = UKA(tokens), v, i, ink;
	const u32 *tab = TV(72) == UK_GIVRE ? uk_jet_givre : uk_jet_noir;
	for (i = 0; ULT(i, TJ_TABLE / 4); i++) /* table du thème nommé */
		STW(j, i * 4, tab[i]);
	for (i = 0; ULT(i, 4); i++) /* survol, axes */
		STW(j, TJ_HOVER + i * 4, uk_jet_more[tab != uk_jet_givre][i]);
	if (TV(40) >= 0) /* clés écrites dans theme.txt */
		accent_set(TV(40));
	if ((v = TV(16)) >= 0)
		STW(j, TJ_TITLE_ACTIVE, v);
	if ((v = TV(24)) >= 0)
		STW(j, TJ_TITLE_INACTIVE, v);
	if ((v = TV(48)) >= 0) /* barre de menus, dock : leur transparence est gardée */
		STW(j, TJ_BAR, (u32)v | ((u32)LDW(j, TJ_BAR) & 0xFF000000));
	if ((v = TV(56)) >= 0)
		STW(j, TJ_DOCK, (u32)v | ((u32)LDW(j, TJ_DOCK) & 0xFF000000));
	if ((v = TV(80)) >= 0) { /* button : face des boutons */
		STW(j, TJ_RAISED, v);
		STW(j, TJ_RAISED_LOW, v);
	}
	if ((v = TV(88)) >= 0) /* field : champs, listes */
		STW(j, TJ_SURFACE, v);
	ink = ui_ink(LDW(j, TJ_ACCENT));
	STW(j, TJ_ACCENT_INK, ink);
	return ink;
}

/* ui_tokens : -> jetons du thème (TJ_*, 32 bits chacun, lecture seule) */
long ui_tokens(void)
{
	return UKA(tokens);
}

/* ui_scale : -> échelle : 0 grande (hauteur >= 720), 1 moyenne (>= 540),
 * 2 petite (ou largeur < 720) ; « echelle = » de theme.txt l'impose */
long ui_scale(void)
{
	long v, sw, sh;
	v = TV(96);
	if (v >= 0)
		return v;
	v = uk.scale;
	if (v >= 0)
		return v;
	if (ui_screen(UKA(scr)) < 0)
		return 0; /* pas de serveur : grande, non retenue */
	sw = LDX(uk.scr, 0);
	sh = LDX(uk.scr, 8);
	v = 2;
	if (!ULT(sw, 720) && !ULT(sh, 540)) {
		v = 1;
		if (!ULT(sh, 720))
			v = 0;
	}
	uk.scale = v;
	return v;
}

/* ---- lecture de theme.txt ------------------------------------------------- */

/* uk_skip_sp : chaîne -> après les espaces et tabulations */
static long skip_sp(long s)
{
	while (LDB(s, 0) == ' ' || LDB(s, 0) == 9)
		s++;
	return s;
}

/* uk_str_eq : chaînes -> 1 si égales */
long uk_str_eq(long a, long b)
{
	for (;;) {
		long c = LDB(a, 0), d = LDB(b, 0);
		a++;
		b++;
		if (c != d)
			return 0;
		if (!c)
			return 1;
	}
}

/* uk_parse_hex : « 0xRRGGBB », « #RRGGBB » ou « RRGGBB » -> couleur (< 0 :
 * invalide). Comme l'asm : chiffres sans limite (les derniers comptent). */
static long parse_hex(long s)
{
	long v = 0, n = 0;
	u32 c, d;
	c = (u32)LDB(s, 0);
	if (c == '#')
		s++;
	else if (c == '0' && LDB(s, 1) == 'x')
		s += 2;
	for (;;) {
		c = (u32)LDB(s, 0);
		s++;
		d = c - '0';
		if (d > 9) {
			d = c - 'a';
			if (d > 5)
				break;
			d += 10;
		}
		v = (long)((u64)v << 4) + (long)d;
		n++;
	}
	if (!n)
		return -1;
	return v & 0xFFFFFF;
}

/* uk_theme_line : ligne « clé = valeur » (terminée par 0, modifiée) */
static void theme_line(long l)
{
	long key, p, v, q, idx, n, e;
	u32 c;
	key = p = skip_sp(l);
	for (;;) { /* clé */
		c = (u32)LDB(p, 0);
		if (!c)
			return; /* pas de « = » */
		if (c == '=')
			goto keq;
		if (c <= ' ')
			break;
		STB(p, 0, c | 0x20); /* minuscules (ASCII) */
		p++;
	}
	STB(p, 0, 0); /* fin de clé, puis chercher « = » */
	p++;
	for (;;) {
		c = (u32)LDB(p, 0);
		if (!c)
			return;
		p++;
		if (c == '=')
			break;
	}
	goto val;
keq:
	STB(p, 0, 0);
	p++;
val:
	v = q = skip_sp(p); /* valeur : jusqu'à la fin de ligne (CR compris) */
	for (;;) {
		c = (u32)LDB(q, 0);
		if (c < ' ')
			break;
		c |= 0x20;
		if (c >= 'a' && c <= 'z') /* minuscules (lettres seulement) */
			STB(q, 0, c);
		q++;
	}
	STB(q, 0, 0);
	while (UGT(q, v) && LDB(q, -1) == ' ') { /* espaces de fin retirés */
		q--;
		STB(q, 0, 0);
	}
	/* quelle clé ? */
	if (uk_str_eq(key, S(uk_k_theme)))
		goto theme;
	if (uk_str_eq(key, S(uk_k_outline)))
		goto outline;
	idx = 16; /* couleurs : active.. dans l'ordre de uk_tv */
	if (uk_str_eq(key, S(uk_k_active)))
		goto colour;
	idx = 24;
	if (uk_str_eq(key, S(uk_k_inactive)))
		goto colour;
	idx = 32;
	if (uk_str_eq(key, S(uk_k_window)))
		goto colour;
	idx = 40;
	if (uk_str_eq(key, S(uk_k_accent)))
		goto colour;
	idx = 48;
	if (uk_str_eq(key, S(uk_k_menubar)))
		goto colour;
	idx = 56;
	if (uk_str_eq(key, S(uk_k_dock)))
		goto colour;
	idx = 80;
	if (uk_str_eq(key, S(uk_k_button)))
		goto colour;
	idx = 88;
	if (uk_str_eq(key, S(uk_k_field)))
		goto colour;
	if (uk_str_eq(key, S(uk_k_density)) || uk_str_eq(key, S(uk_k_densite))) {
		/* density = touch (tactile) | normal */
		n = uk_str_eq(v, S(uk_v_touch));
		if (!n)
			n = uk_str_eq(v, S(uk_v_tactile));
		TV(UK_TV_DENSITY) = n;
		return;
	}
	if (!uk_str_eq(key, S(uk_k_echelle)))
		return;
	c = (u32)LDB(v, 0); /* echelle = l | m | p (auto : absente) */
	if (c == 'l')
		n = 0;
	else if (c == 'm')
		n = 1;
	else if (c == 'p')
		n = 2;
	else
		return;
	TV(96) = n;
	return;
colour:
	if (uk_str_eq(v, S(uk_v_auto))) {
		TV(idx) = UK_AUTO;
		return;
	}
	if (idx == 40) { /* accent : un nom ? */
		e = S(uk_accents);
		for (n = 0; LDB(e, 0); n++, e += UK_ACC_SIZE)
			if (uk_str_eq(v, e)) {
				TV(40) = UK_ACC_NOM | n;
				return;
			}
		idx = 40;
	}
	n = parse_hex(v);
	if (n >= 0)
		TV(idx) = n;
	return;
outline:
	n = 0;
	if (!uk_str_eq(v, S(uk_v_none))) {
		n = 2;
		if (!uk_str_eq(v, S(uk_v_black)))
			n = 1;
	}
	TV(64) = n;
	return;
theme:
	/* thème nommé (les espaces du nom : « dark coffee » n'est pas repris) */
	e = S(uk_named);
	for (n = 0; LDB(e, 0); n++, e += UK_NAMED_SIZE)
		if (uk_str_eq(v, e)) {
			TV(0) = n;
			return;
		}
}

/* uk_theme_parse : texte du fichier (modifié) : remplit uk_tv */
static void theme_parse(long s)
{
	long e;
	u8 c;
	for (;;) {
		if (!LDB(s, 0))
			return;
		e = s; /* fin de ligne (# : commentaire) -> 0 */
		for (;;) {
			c = (u8)LDB(e, 0);
			if (!c || c == 10)
				break;
			if (c == '#')
				STB(e, 0, 0); /* la suite de la ligne est ignorée */
			e++;
		}
		c = (u8)LDB(e, 0);
		STB(e, 0, 0);
		theme_line(s);
		if (!c)
			return;
		s = e + 1;
	}
}

/* ---- couleurs (comme uikit/paint.cpp d'Onyx) ------------------------------ */

/* ui_tone : w0 = couleur, x1 = niveau 0..255 -> < 128 plus sombre, > 128
 * plus clair */
long ui_tone(long col, long n)
{
	u32 res = 0, sh, v;
	if (n < 0)
		n = 0;
	if (n > 255)
		n = 255;
	for (sh = 0; sh < 24; sh += 8) {
		v = ((u32)col >> sh) & 0xFF;
		if (ULT(n, 128))
			v = (v * (u32)n) >> 7; /* v * n / 128 */
		else /* v + (255 - v) * (n - 128) / 127 */
			v = v + (u32)UDIVW((255 - v) * ((u32)n - 128), 127);
		res |= v << sh;
	}
	return (long)res;
}

/* ui_mix : w0 = a, w1 = b, x2 = t (0..256) -> a * (256 - t) / 256 + b * t / 256 */
long ui_mix(long a, long b, long t)
{
	u32 res = 0, sh, s, v;
	if (t <= 0)
		return (long)((u32)a & 0xFFFFFF);
	if (t >= 256)
		return (long)((u32)b & 0xFFFFFF);
	s = 256 - (u32)t;
	for (sh = 0; sh < 24; sh += 8) {
		v = (((u32)a >> sh) & 0xFF) * s;
		v = (((u32)b >> sh) & 0xFF) * (u32)t + v;
		res |= (v >> 8) << sh;
	}
	return (long)res;
}

/* ui_ink : w0 = fond -> encre lisible dessus (presque noir, ou blanc) */
long ui_ink(long bg)
{
	return (u32)bright(bg) > 140 ? 0x00201C1A : 0x00FFFFFF;
}

/* uk_isqrt : n -> partie entière de la racine carrée (petits n) */
static long isqrt(long n)
{
	long r = 0;
	for (;;) {
		long t = r + 1;
		if (UGT(t * t, n))
			return r;
		r = t;
	}
}

/* ---- boîtes arrondies ----------------------------------------------------- */

/* uk_rb_px : x, y, couleur, couverture 1..254 : comme uk_blend_px ; sur un
 * pixel transparent (WF_ALPHA), la couleur reste et seule la transparence
 * baisse */
static void rb_px(long x, long y, long col, long cov)
{
	long W = UVX(UV_W), H = UVX(UV_H), p;
	u32 d, inv, a, o, sh, v;
	if (UGE(x, W) || UGE(y, H))
		return;
	if (uk.clip_on) {
		if (x < uk.clip[0] || y < uk.clip[1] || x >= uk.clip[2] || y >= uk.clip[3])
			return;
	}
	p = UVX(UV_SURF) + ((y * W + x) << 2);
	d = (u32)LDW(p, 0);
	inv = 255 - (u32)cov;
	a = d >> 24;
	if (a) { /* transparence : t * (1 - couverture) */
		a = (u32)UDIVW(a * inv, 255);
		STW(p, 0, ((u32)col & 0xFFFFFF) | (a << 24));
		return;
	}
	o = 0;
	for (sh = 0; sh < 24; sh += 8) {
		v = (((u32)col >> sh) & 0xFF) * (u32)cov;
		v = ((d >> sh) & 0xFF) * inv + v;
		v = (u32)UDIVW(v, 255);
		o |= v << sh;
	}
	STW(p, 0, o);
}

/* uk_rb_row : ligne j dans le coin (rayon r <= 16), couleur : pixels partiels
 * des deux coins de la ligne row de la boîte (x, y, largeur w) -> retrait de
 * la partie pleine */
static long rb_row(long j, long col, long x, long y, long w, long r, long row)
{
	long m = S(uk_rm) + uk_rm_off[r] + j * r, k, cov;
	u32 c = (u32)col; /* (ldr w2,[sp]) */
	for (k = 0; ULT(k, r); k++) {
		cov = LDB(m, k);
		if (cov == 255)
			break;
		if (!cov)
			continue;
		rb_px(x + k, y + row, c, cov);
		rb_px(x + w - 1 - k, y + row, c, cov);
	}
	return k;
}

/* ui_rbox : x, y, largeur, hauteur, rayon, w5 = couleur du haut, w6 = du bas :
 * boîte arrondie en dégradé vertical (coins anticrénelés jusqu'au rayon 16,
 * couleur opaque ; sinon un pixel de bord par ligne, sans anticrénelage).
 * Rend (comme l'asm) le x0 de la sortie : x si rien n'est dessiné, sinon le
 * retour du dernier ui_fill. */
long ui_rbox(long x, long y, long w, long h, long r, long ctop, long cbot)
{
	long top = (u32)ctop, bot = (u32)cbot, row, c, j, off, ret = x;
	/* (en 32 bits : un widget trop petit donne une taille négative en w,
	 *  sans signe en x) */
	if ((s32)h <= 0 || (s32)w <= 0)
		return ret;
	if (LSL(r, 1) > w) /* rayon <= moitié des côtés (comparaisons en 64 bits signés) */
		r = LSR(w, 1);
	if (LSL(r, 1) > h)
		r = LSR(h, 1);
	for (row = 0; ULT(row, h); row++) {
		c = top; /* couleur de la ligne */
		if (h > 1) {
			c = ui_mix(top, bot, UDIV(row << 8, h - 1));
			c = (u32)c | ((u32)top & 0xFF000000); /* (transparence du haut) */
		}
		off = 0; /* retrait aux coins */
		if (r) {
			j = row; /* ligne dans le coin */
			if (!ULT(row, r)) {
				j = h - 1 - row;
				if (UGE(j, r))
					goto fill;
			}
			if (((u32)c >> 24) == 0 && !UGT(r, 16)) {
				off = rb_row(j, c, x, y, w, r, row);
			} else { /* r - racine(4r² - (2r - 2j - 1)²) / 2 */
				long d = r << 1, a = d * d, b = d - (j << 1) - 1;
				b = b * b;
				a = isqrt(a - b);
				a = LSR(a + 1, 1); /* arrondi */
				off = r - a;
			}
		}
	fill:
		ret = ui_fill(x + off, y + row, w - (off << 1), 1, c);
	}
	return ret;
}
