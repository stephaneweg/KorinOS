/* uitest : essai différentiel d'UIKit (src/kits/uikit), sans bibliothèque,
 * pour AArch64 (Korin sur le Pi) et x86-64 (Korin sur PC).
 *
 * Appelle les fonctions du kit par sa table et n'écrit que ce qui ne dépend ni
 * des adresses, ni de l'heure, ni du pid : valeurs rendues, tailles, textes,
 * et sommes FNV-1a des pixels dessinés. Le kit en assembleur (build/lib/uikit.so)
 * et le kit en C (build/kits/<arch>/uikit.so) doivent donner des sorties
 * identiques, octet pour octet :
 *     /bin/uitest [-f]   (par telnet : tools/kits/run_pi.sh ; carte asm,
 *                             carte C, puis la partie en mémoire sur le PC)
 * Contrôles propres : « ECHEC ... », code de sortie = nombre d'échecs.
 *
 * Partie en mémoire (Pi et PC) : les variables du programme (macro ui_data
 * de uikit.inc) pointent sur une surface à soi de 320 x 240 et une fenêtre
 * fictive (0x7FFF0001 : les ui_update partent vers Elegant et échouent, sans
 * importance) ; les trois polices sont remplies par uitest lui-même depuis
 * des polices embarquées (Inter, Inter SemiBold, DejaVu Sans Mono, rasteriseur
 * uk_ttf.c lié au programme), sans repli : le kit dessine le même texte avec
 * ou sans fichiers. Le thème est fixé par ui_theme_text. Les dessins qui
 * liraient des fichiers (glyphes glyph-*, icônes) n'y prennent que des noms
 * absents (dessin de repli).
 *
 * uitest -f : en plus, après une ligne « === Pi (fichiers, fenetre) », les
 * vraies polices (ui_init), les icônes et glyphes de /, des images PNG de
 * /, le thème du fichier, l'association des fichiers, une vraie fenêtre
 * (widgets, événements simulés, menus, surface attachée, deuxième fenêtre).
 * Le PC n'a pas encore de service de fichiers ni de serveur de fenêtres : ne
 * comparer que ce qui précède cette ligne.
 *
 * Diagnostic : -t imprime FPCR / MXCSR et une somme après chaque texte de
 * l'essai du texte ; -n donne aux polices embarquées aucun cache de glyphes.
 * Trois sommes de texte (sans 40 px, gras 20 px, bords) diffèrent entre le Pi
 * sous QEMU 11.1 (asm et C d'accord) et le PC : le PC donne le calcul IEEE
 * exact, le même que l'hôte x86-64 natif et qemu-aarch64 10.2 avec les objets
 * du kit (essai du 12/11).
 *
 * Construction : src/kits/uikit/test/build.sh (dans WSL). */
#include <stddef.h>
#include <stdint.h>

/* src/inc/syscalls.inc */
#define SYS_EXIT 0x10
#define SYS_PRINT 0x60
#define SYS_LIB_OPEN 0x64

#define KIT_MAGIC 0x54494B41
/* AppKit (rangs de appkit.abi) */
#define AK_F_PRINT 25    /* (version 26) */
#define AK_F_KIT_LOAD 31 /* (version 32) */

/* rangs de uikit.abi (dans l'ordre de la table) */
enum {
	UI_INIT, UI_LOAD_FONT, UI_WINDOW, UI_FILL, UI_FRAME, UI_PIXEL, UI_TEXT, UI_TEXT_WIDTH, UI_BUTTON, UI_UPDATE,
	UI_UPDATE_ALL, UI_EVENT, UI_HIT, UI_TITLE, UI_CLOSE, UI_SCREEN, UI_WINDOW_EX, UI_THEME, UI_TONE, UI_MIX,
	UI_INK, UI_RBOX, UI_POPUP, UI_WINLIST, UI_RAISE, UI_JETONS, UI_ECHELLE, UI_ICONE, UI_POPUP_EX, UI_OMBRE,
	UI_POPUP_CLOSE, UI_THEME_TEXTE, UI_MENU, UI_MENU_GET, UI_MENU_COMMAND, UI_CLIP, UI_TEXT_FIT, UI_W_EVENT,
	UI_W_DRAW, UI_W_MOVE, UI_W_SHOW, UI_W_ENABLE, UI_W_FOCUS, UI_W_GET_FOCUS, UI_W_ON, UI_W_USER,
	UI_W_GET_VALUE, UI_W_SET_VALUE, UI_W_SET_RANGE, UI_W_DESTROY, UI_SCROLLBAR, UI_LIST, UI_LIST_COLUMN,
	UI_LIST_ADD, UI_LIST_CLEAR, UI_LIST_COUNT, UI_LIST_GET, UI_LIST_SET, UI_LIST_REMOVE, UI_LIST_SORT,
	UI_TEXT_PREFIX, UI_TEXT_HIT, UI_W_SET_TEXT, UI_W_GET_TEXT, UI_TEXTBOX, UI_TEXTBOX_HINT, UI_TEXTBOX_SELECT,
	UI_TEXTBOX_SELECTION, UI_CLIP_SET, UI_CLIP_GET, UI_TEXT_WRAP, UI_PUSHBUTTON, UI_LABEL, UI_MESSAGE, UI_ASK,
	UI_FILE_OPEN, UI_FILE_SAVE, UI_FOLDER_CHOOSE, UI_DIALOG_BEGIN, UI_DIALOG_BUTTON, UI_DIALOG_RUN,
	UI_DIALOG_CLOSE, UI_DIALOG_CANCEL, UI_DIALOG_END, UI_CHECKBOX, UI_RADIO, UI_SWITCH, UI_DROPDOWN,
	UI_COMBOBOX, UI_TEXTAREA, UI_TEXTAREA_CARET, UI_TEXTAREA_GOTO, UI_SLIDER, UI_PROGRESS, UI_NUMERIC, UI_KNOB,
	UI_VUMETER, UI_VUMETER_LEVEL, UI_LCD, UI_TEXTAREA_HIGHLIGHT, UI_TEXTAREA_KEYWORDS, UI_TEXTAREA_FONT,
	UI_TEXTAREA_EDIT, UI_TEXTAREA_FIND, UI_W_STYLE, UI_W_TEXT_LEN, UI_RESIZE, UI_MIN_SIZE, UI_ON_RESIZE,
	UI_SPLITTER, UI_PANEL, UI_GROUPBOX, UI_W_SET_PARENT, UI_SEGMENTED, UI_TABS, UI_TABS_PAGE, UI_TABSTRIP,
	UI_TABSTRIP_ADD, UI_TABSTRIP_CLOSING, UI_TOOLBAR, UI_TOOLBAR_ADD, UI_IMAGE_LOAD, UI_IMAGE_DECODE,
	UI_IMAGE_FREE, UI_IMAGE_SIZE, UI_IMAGE_DRAW, UI_IMAGEBOX, UI_IMAGEBOX_SET, UI_CURSOR, UI_W_CURSOR,
	UI_W_TIP, UI_W_IDLE, UI_DRAG_START, UI_DROP_GET, UI_TR, UI_TR_LOAD, UI_LANG, UI_MENU_STATE, UI_MENU_KEY,
	UI_FONT_ITALIC, UI_CALENDAR, UI_CALENDAR_TODAY, UI_DATEPICKER, UI_DATEPICKER_TODAY, UI_PALETTE,
	UI_COLORPICKER, UI_COLOR_DIALOG, UI_TREE, UI_TREE_ADD, UI_TREE_EXPAND, UI_TREE_GET, UI_TREE_CLEAR,
	UI_LIST_VIRTUAL, UI_LIST_SORTED, UI_LAYOUT, UI_CANVAS, UI_CANVAS_PIXELS, UI_CANVAS_BEGIN, UI_CANVAS_END,
	UI_VP_DISC, UI_VP_LINE, UI_VP_POLY, UI_PAD_HINTS, UI_OBJNAV, UI_POPUP_LOCAL, UI_FONT_GET, UI_SURFACE,
	UI_SURFACE_PIXELS, UI_SURFACE_DAMAGE, UI_SURFACE_DAMAGE_LIST, UI_SURFACE_MOVE, UI_SURFACE_FREE,
	UI_MAXIMIZE, UI_FILE_FILTER, UI_FRAME_REQUEST, UI_VIEW, UI_VIEW_ON, UI_VIEW_DIRTY, UI_VIEW_FLUSH,
	UI_LIST_CELL, UI_DATE_POPUP, UI_DIALOG_AT, UI_WINDOW_USE, UI_WINDOW_CLOSE, UI_WINDOW_COUNT, UI_W_WINDOW,
	UI_DESK, UI_WIN_DETAILS, UI_TIP_AT, UI_DIALOG_TIMER, UI_TEXTAREA_INSERT, UI_WINDOW_DRAG, UI_CLIP_PUT,
	UI_CLIP_TAKE, UI_CLIP_SET_IMAGE, UI_CLIP_GET_IMAGE, UI_CLIP_SET_FILES, UI_CLIP_GET_FILES,
	UI_CLIP_DONE_CUT, UI_CALENDAR_SHOW, UI_TEXTAREA_PAPER, UI_GLYPH, UI_SURFACE_SOURCE, UI_POINTER_LOCK,
	UI_ASSOC_APP, UI_ASSOC_OPEN, UI_ASSOC_SET, UI_OPEN_WITH, UI_ARG, UI_SCREEN_GRAB, UI_MINIMIZE, UI_METRIC,
	UI_ANCHOR, UI_DESIGN_SIZE, UI_COUNT_FN
};

static const char *const fn_names[UI_COUNT_FN] = {
	"ui_init", "ui_load_font", "ui_window", "ui_fill", "ui_frame", "ui_pixel", "ui_text", "ui_text_width",
	"ui_button", "ui_update", "ui_update_all", "ui_event", "ui_hit", "ui_title", "ui_close", "ui_screen",
	"ui_window_ex", "ui_theme", "ui_tone", "ui_mix", "ui_ink", "ui_rbox", "ui_popup", "ui_winlist", "ui_raise",
	"ui_tokens", "ui_scale", "ui_icon", "ui_popup_ex", "ui_shadow", "ui_popup_close", "ui_theme_text",
	"ui_menu", "ui_menu_get", "ui_menu_command", "ui_clip", "ui_text_fit", "ui_w_event", "ui_w_draw",
	"ui_w_move", "ui_w_show", "ui_w_enable", "ui_w_focus", "ui_w_get_focus", "ui_w_on", "ui_w_user",
	"ui_w_get_value", "ui_w_set_value", "ui_w_set_range", "ui_w_destroy", "ui_scrollbar", "ui_list",
	"ui_list_column", "ui_list_add", "ui_list_clear", "ui_list_count", "ui_list_get", "ui_list_set",
	"ui_list_remove", "ui_list_sort", "ui_text_prefix", "ui_text_hit", "ui_w_set_text", "ui_w_get_text",
	"ui_textbox", "ui_textbox_hint", "ui_textbox_select", "ui_textbox_selection", "ui_clip_set", "ui_clip_get",
	"ui_text_wrap", "ui_pushbutton", "ui_label", "ui_message", "ui_ask", "ui_file_open", "ui_file_save",
	"ui_folder_choose", "ui_dialog_begin", "ui_dialog_button", "ui_dialog_run", "ui_dialog_close",
	"ui_dialog_cancel", "ui_dialog_end", "ui_checkbox", "ui_radio", "ui_switch", "ui_dropdown", "ui_combobox",
	"ui_textarea", "ui_textarea_caret", "ui_textarea_goto", "ui_slider", "ui_progress", "ui_numeric",
	"ui_knob", "ui_vumeter", "ui_vumeter_level", "ui_lcd", "ui_textarea_highlight", "ui_textarea_keywords",
	"ui_textarea_font", "ui_textarea_edit", "ui_textarea_find", "ui_w_style", "ui_w_text_len", "ui_resize",
	"ui_min_size", "ui_on_resize", "ui_splitter", "ui_panel", "ui_groupbox", "ui_w_set_parent",
	"ui_segmented", "ui_tabs", "ui_tabs_page", "ui_tabstrip", "ui_tabstrip_add", "ui_tabstrip_closing",
	"ui_toolbar", "ui_toolbar_add", "ui_image_load", "ui_image_decode", "ui_image_free", "ui_image_size",
	"ui_image_draw", "ui_imagebox", "ui_imagebox_set", "ui_cursor", "ui_w_cursor", "ui_w_tip", "ui_w_idle",
	"ui_drag_start", "ui_drop_get", "ui_tr", "ui_tr_load", "ui_lang", "ui_menu_state", "ui_menu_key",
	"ui_font_italic", "ui_calendar", "ui_calendar_today", "ui_datepicker", "ui_datepicker_today",
	"ui_palette", "ui_colorpicker", "ui_color_dialog", "ui_tree", "ui_tree_add", "ui_tree_expand",
	"ui_tree_get", "ui_tree_clear", "ui_list_virtual", "ui_list_sorted", "ui_layout", "ui_canvas",
	"ui_canvas_pixels", "ui_canvas_begin", "ui_canvas_end", "ui_vp_disc", "ui_vp_line", "ui_vp_poly",
	"ui_pad_hints", "ui_objnav", "ui_popup_local", "ui_font_get", "ui_surface", "ui_surface_pixels",
	"ui_surface_damage", "ui_surface_damage_list", "ui_surface_move", "ui_surface_free", "ui_maximize",
	"ui_file_filter", "ui_frame_request", "ui_view", "ui_view_on", "ui_view_dirty", "ui_view_flush",
	"ui_list_cell", "ui_date_popup", "ui_dialog_at", "ui_window_use", "ui_window_close", "ui_window_count",
	"ui_w_window", "ui_desk", "ui_win_details", "ui_tip_at", "ui_dialog_timer", "ui_textarea_insert",
	"ui_window_drag", "ui_clip_put", "ui_clip_take", "ui_clip_set_image", "ui_clip_get_image",
	"ui_clip_set_files", "ui_clip_get_files", "ui_clip_done_cut", "ui_calendar_show", "ui_textarea_paper",
	"ui_glyph", "ui_surface_source", "ui_pointer_lock", "ui_assoc_app", "ui_assoc_open", "ui_assoc_set",
	"ui_open_with", "ui_arg", "ui_screen_grab", "ui_minimize", "ui_metric", "ui_anchor", "ui_design_size",
	"ui_surface_overlay",
};

/* ---- constantes (uikit.inc, uk_consts.h) */

#define UI_FONT_SIZE 320
#define UI_SCRATCH 0x20000
#define UI_CACHE 0x10000
#define F_FALLBACK 312
#define TJ_SIZE 120
#define TH_SIZE 64
#define TJ_FACE 0
#define TJ_TEXT1 28
#define TJ_ACCENT 40

#define EV_TYPE 0
#define EV_KIND 4
#define EV_X 8
#define EV_Y 12
#define EV_CODE 16
#define EV_CHAR 20
#define EV_MODS 24
#define EV_WIN 28
#define WEV_KEY 1
#define WEV_MOUSE 2
#define WEV_WHEEL 4
#define WEV_FOCUS 5
#define WEV_RESIZE 8

#define KEY_ENTER 40
#define KEY_ESC 41
#define KEY_BACKSPACE 42
#define KEY_TAB 43
#define KEY_SPACE 44
#define KEY_HOME 74
#define KEY_PGUP 75
#define KEY_DELETE 76
#define KEY_END 77
#define KEY_PGDN 78
#define KEY_RIGHT 79
#define KEY_LEFT 80
#define KEY_DOWN 81
#define KEY_UP 82
#define M_CTRL 0x01
#define M_SHIFT 0x02
#define M_ALT 0x04

/* blocs du kit (lus seulement pour nommer un widget interne et voir une place) */
#define UW_TYPE 8
#define UW_X 16
#define UW_Y 20
#define UW_W 24
#define UW_H 28
#define UW_PARENT 232

#define UN_CHANGE 1
#define UN_ACTIVATE 2
#define UFN_SANS 0
#define ULY_VSTACK 0
#define ULY_HSTACK 1
#define ULY_GRID 2
#define UPN_SURFACE 1
#define UTS_CLOSE 1
#define UTS_PLUS 2
#define UIM_STRETCH 1
#define UIM_NOSCALE 2
#define UTB_TOGGLE 1
#define UTB_SEP 2
#define USB_HORIZONTAL 1
#define UL_HEADER 1
#define UL_NOSORT 2
#define UL_STRIPES 8
#define UL_GRID 16
#define UL_SIDEBAR 32
#define UL_HSCROLL 64
#define UL_USERSORT 128
#define ULC_RIGHT 1
#define UTF_PASSWORD 1
#define UTF_READONLY 2
#define UTF_MONO 4
#define UTF_RIGHT 256
#define UTF_ERROR 512
#define UTA_LINENUM 8
#define UTA_VSCROLL 16
#define UTA_HSCROLL 32
#define UTA_CODE 64
#define UTA_TABS 128
#define UTA_FORMTAB 1024
#define UTA_BASIC 2048
#define UTA_CS 4096
#define UTA_XML 8192
#define UTE_UNDO 1
#define UTE_REDO 2
#define UTE_ALL 6
#define UB_DEFAULT 1
#define UB_GHOST 2
#define ULB_BOLD 1
#define ULB_DIM 2
#define ULB_WRAP 4
#define ULB_RIGHT 8
#define ULB_CENTER 16
#define ULB_TITLE 32
#define UMS_DISABLED 1
#define UMS_CHECKED 2
#define UA_LEFT 1
#define UA_TOP 2
#define UA_RIGHT 4
#define UA_BOTTOM 8
#define UA_FILL 15
#define UC_HAND 2
#define UVW_FOCUS 1

#define FAKE_WIN 0x7FFF0001L
#define SW 320
#define SH 240

/* ---- appels système (comme src/kits/common/korin_kit.h) */

#if defined(__aarch64__)
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}
#elif defined(__x86_64__)
static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	long ret;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}
#else
#error "architecture non prise en charge"
#endif
#define sys2(n, a, b) sys6((n), (long)(a), (long)(b), 0, 0, 0, 0)

/* le compilateur (et uk_ttf.c) peuvent les appeler */
void *memset(void *d, int c, size_t n)
{
	volatile unsigned char *a = d;
	while (n--)
		*a++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	volatile unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b)
		while (n--)
			*a++ = *b++;
	else
		while (n--)
			a[n] = b[n];
	return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++)
		if (*a != *b)
			return *a - *b;
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

static long slen(const char *s)
{
	return (long)strlen(s);
}

/* ---- le rasteriseur TrueType du kit en C, lié au programme (uk_ttf.c) */

long ttf_open(long font, long data, long len, long scratch, long scratch_len);
long ttf_set_cache(long font, long buf, long size);

/* polices embarquées (chemins depuis la racine du dépôt : build.sh y va) */
__asm__(".section .rodata\n"
	".balign 16\n"
	"font_sans: .incbin \"build/sdroot/fonts/Inter-Regular.ttf\"\n"
	"font_sans_end:\n"
	".zero 0x10000\n" /* zéros après la police : une lecture au-delà de sa fin voit la même chose sur le Pi et sur le PC */
	".balign 16\n"
	"font_bold: .incbin \"build/sdroot/fonts/Inter-SemiBold.ttf\"\n"
	"font_bold_end:\n"
	".zero 0x10000\n" /* zéros après la police : une lecture au-delà de sa fin voit la même chose sur le Pi et sur le PC */
	".balign 16\n"
	"font_mono: .incbin \"build/sdroot/fonts/DejaVuSansMono.ttf\"\n"
	"font_mono_end:\n"
	".zero 0x10000\n" /* zéros après la police : une lecture au-delà de sa fin voit la même chose sur le Pi et sur le PC */
	/* petits PNG de genres différents (compressés par d'autres outils) */
	".balign 16\n"
	"png_rgba: .incbin \"build/sdroot/apps/supertux.app/data/images/objects/bullets/fire_tail-0.png\"\n"
	"png_rgba_end:\n"
	".balign 16\n"
	"png_pal: .incbin \"build/sdroot/apps/supertux.app/data/images/creatures/flame/flame-fade-3.png\"\n"
	"png_pal_end:\n"
	".balign 16\n"
	"png_rgb: .incbin \"build/sdroot/apps/supertux.app/data/images/engine/editor/gradient.png\"\n"
	"png_rgb_end:\n"
	".balign 16\n"
	"png_ga: .incbin "
	"\"build/sdroot/apps/supertux.app/data/images/background/misc/distortion/source/distortion_area_rough.png\"\n"
	"png_ga_end:\n"
	".text\n");
extern const unsigned char font_sans[], font_sans_end[], font_bold[], font_bold_end[], font_mono[],
	font_mono_end[];
extern const unsigned char png_rgba[], png_rgba_end[], png_pal[], png_pal_end[], png_rgb[], png_rgb_end[],
	png_ga[], png_ga_end[];

/* ---- sortie : AppKit (sortie standard, se redirige) s'il est là, sinon la
 * console du noyau */

typedef long (*fn_print)(const char *, long);
static fn_print f_print;
static char pb[4096];
static long pn;

static void flush(void)
{
	if (!pn)
		return;
	if (!f_print || f_print(pb, pn) < 0)
		sys2(SYS_PRINT, pb, pn);
	pn = 0;
}

static void pc(char c)
{
	if (pn == (long)sizeof pb)
		flush();
	pb[pn++] = c;
}

static void ps(const char *s)
{
	while (*s)
		pc(*s++);
}

static void pu(uint64_t v)
{
	char b[24];
	int i = 0;
	do
		b[i++] = (char)('0' + v % 10);
	while (v /= 10);
	while (i)
		pc(b[--i]);
}

static void pd(long v)
{
	if (v < 0) {
		pc('-');
		pu(-(uint64_t)v);
	} else {
		pu((uint64_t)v);
	}
}

static void px(uint64_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		pc("0123456789abcdef"[(v >> (i * 4)) & 15]);
}

/* octets entre guillemets, échappés */
static void pq(const void *p, long n)
{
	const unsigned char *s = p;
	pc('"');
	for (long i = 0; i < n; i++) {
		unsigned c = s[i];
		if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') {
			pc((char)c);
		} else {
			pc('\\');
			pc('x');
			px(c, 2);
		}
	}
	pc('"');
}

static void pz(const char *s) /* chaîne (0 : « nul ») */
{
	if (!s)
		ps("nul");
	else
		pq(s, slen(s));
}

static void nl(void)
{
	pc('\n');
}

static void kv(const char *k, long v)
{
	pc(' ');
	ps(k);
	pc('=');
	pd(v);
}

static void kx(const char *k, uint32_t h)
{
	pc(' ');
	ps(k);
	pc('=');
	px(h, 8);
}

static void ks(const char *k, const char *s)
{
	pc(' ');
	ps(k);
	pc('=');
	pz(s);
}

static uint32_t fnv(const void *p, long n)
{
	const unsigned char *s = p;
	uint32_t h = 2166136261u;
	for (long i = 0; i < n; i++) {
		h ^= s[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t fnv_mix(uint32_t h, uint64_t v)
{
	for (int i = 0; i < 8; i++) {
		h ^= (uint8_t)(v >> (8 * i));
		h *= 16777619u;
	}
	return h;
}

/* ---- contrôles */

static long checks, fails;

static void ck(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		ps("ECHEC ");
		ps(what);
		nl();
	}
}

/* ---- les variables du programme (macro ui_data de uikit.inc) */

static struct ui_vars {
	long win;
	uint32_t *surf;
	long w, h;
	uint8_t ev[32];
	uint8_t font[UI_FONT_SIZE], font_b[UI_FONT_SIZE], font_m[UI_FONT_SIZE];
} V __attribute__((aligned(16)));

static uint32_t surface[SW * SH] __attribute__((aligned(16)));
static uint8_t scratch[3][UI_SCRATCH + UI_CACHE] __attribute__((aligned(16)));
static uint8_t font4[UI_FONT_SIZE] __attribute__((aligned(16))); /* ui_load_font (-f) */

#define FS ((long)V.font)
#define FB ((long)V.font_b)
#define FM ((long)V.font_m)

/* ---- le kit */

static long kit;
static long kit_count;
static long called[UI_COUNT_FN];
static long calls;

typedef long (*fn8)(long, long, long, long, long, long, long, long);
typedef struct {
	long r0, r1, r2, r3;
} R4;

static long kfn(int i)
{
	const uint32_t *t = (const uint32_t *)kit;
	calls++;
	called[i]++;
	return kit + t[2 + i];
}

static long call(int i, long a, long b, long c, long d, long e, long f, long g, long h)
{
	fn8 fn = (fn8)kfn(i);
	return fn(a, b, c, d, e, f, g, h);
}

/* fonctions qui rendent plusieurs registres (ret de kit.conf) : x0..x3 sur
 * AArch64 ; sur x86-64, le kit en C rend une structure de 4 long (en mémoire) */
static R4 callr(int i, long a, long b, long c, long d, long e, long f, long g, long h)
{
	R4 r;
	long fn = kfn(i); /* (avant les variables de registre : un appel les écraserait) */
#if defined(__aarch64__)
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	register long x6 __asm__("x6") = g;
	register long x7 __asm__("x7") = h;
	register long x9 __asm__("x9") = fn;
	__asm__ volatile("blr x9"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7), "+r"(x9)
			 :
			 : "x8", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory", "cc");
	r.r0 = x0;
	r.r1 = x1;
	r.r2 = x2;
	r.r3 = x3;
#else
	typedef R4 (*fr)(long, long, long, long, long, long, long, long);
	r = ((fr)fn)(a, b, c, d, e, f, g, h);
#endif
	return r;
}

#define L(x) ((long)(x))
#define C0(i) call(i, 0, 0, 0, 0, 0, 0, 0, 0)
#define C1(i, a) call(i, L(a), 0, 0, 0, 0, 0, 0, 0)
#define C2(i, a, b) call(i, L(a), L(b), 0, 0, 0, 0, 0, 0)
#define C3(i, a, b, c) call(i, L(a), L(b), L(c), 0, 0, 0, 0, 0)
#define C4(i, a, b, c, d) call(i, L(a), L(b), L(c), L(d), 0, 0, 0, 0)
#define C5(i, a, b, c, d, e) call(i, L(a), L(b), L(c), L(d), L(e), 0, 0, 0)
#define C6(i, a, b, c, d, e, f) call(i, L(a), L(b), L(c), L(d), L(e), L(f), 0, 0)
#define C7(i, a, b, c, d, e, f, g) call(i, L(a), L(b), L(c), L(d), L(e), L(f), L(g), 0)
#define C8(i, a, b, c, d, e, f, g, h) call(i, L(a), L(b), L(c), L(d), L(e), L(f), L(g), L(h))
#define R1(i, a) callr(i, L(a), 0, 0, 0, 0, 0, 0, 0)
#define R2(i, a, b) callr(i, L(a), L(b), 0, 0, 0, 0, 0, 0)
#define R4f(i, a, b, c, d) callr(i, L(a), L(b), L(c), L(d), 0, 0, 0, 0)

static long open_kit(const char *name, long min)
{
	long t = sys2(SYS_LIB_OPEN, name, min);
	if (t == -2) { /* pas encore en mémoire : AppKit le lit (comme kits_open) */
		long a = sys2(SYS_LIB_OPEN, "appkit", 32);
		if (a >= 0) {
			const uint32_t *at = (const uint32_t *)a;
			long (*load)(const char *) = (long (*)(const char *))(a + at[2 + AK_F_KIT_LOAD]);
			if (load(name) == 0)
				t = sys2(SYS_LIB_OPEN, name, min);
		}
	}
	return t;
}

/* ---- surface et dessins */

static uint32_t shash(void) /* les pixels de la surface courante (V) */
{
	uint32_t h = 2166136261u;
	const uint32_t *p = V.surf;
	long n = V.w * V.h;
	if (!p || n <= 0)
		return 0;
	for (long i = 0; i < n; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

static void use_mem_surface(void)
{
	V.win = FAKE_WIN;
	V.surf = surface;
	V.w = SW;
	V.h = SH;
}

static void clear(uint32_t c)
{
	for (long i = 0; i < SW * SH; i++)
		surface[i] = c;
}

static void sum(const char *what) /* « what sum=... » */
{
	ps(what);
	kx("sum", shash());
	nl();
}

static long tok(long off)
{
	return *(const uint32_t *)(C0(UI_JETONS) + off);
}

/* ---- widgets : indices pour la sortie */

#define NW 96
static long wt[NW];
static int nwt;

static long addw(const char *what, long w)
{
	ck(w != 0, what);
	if (nwt < NW)
		wt[nwt++] = w;
	return w;
}

static void pw(long w) /* indice du widget, « 0 », ou le type d'un widget interne */
{
	if (!w) {
		pc('0');
		return;
	}
	for (int i = 0; i < nwt; i++)
		if (wt[i] == w) {
			pc('#');
			pd(i);
			return;
		}
	ps("int(t");
	pd(*(const uint32_t *)(w + UW_TYPE));
	long p = *(const long *)(w + UW_PARENT);
	ps(",p");
	if (!p) {
		pc('0');
	} else {
		int k = -1;
		for (int i = 0; i < nwt; i++)
			if (wt[i] == p)
				k = i;
		if (k >= 0) {
			pc('#');
			pd(k);
		} else {
			pc('?');
		}
	}
	pc(')');
}

static void kw(const char *k, long w)
{
	pc(' ');
	ps(k);
	pc('=');
	pw(w);
}

static void destroy_all(void) /* dans l'ordre inverse : les enfants avant les parents */
{
	while (nwt > 0)
		C1(UI_W_DESTROY, wt[--nwt]);
	nwt = 0;
}

static void rect_of(long w) /* place d'un widget (bloc du kit) */
{
	ps(" rect=");
	pd(*(const int32_t *)(w + UW_X));
	pc(',');
	pd(*(const int32_t *)(w + UW_Y));
	pc(',');
	pd(*(const int32_t *)(w + UW_W));
	pc(',');
	pd(*(const int32_t *)(w + UW_H));
}

/* ---- événements simulés (32 octets EV_*, dans ui_ev) */

static void ev_set(long type, long kind, long x, long y, long code, long ch, long mods)
{
	int32_t *e = (int32_t *)V.ev;
	e[EV_TYPE / 4] = (int32_t)type;
	e[EV_KIND / 4] = (int32_t)kind;
	e[EV_X / 4] = (int32_t)x;
	e[EV_Y / 4] = (int32_t)y;
	e[EV_CODE / 4] = (int32_t)code;
	e[EV_CHAR / 4] = (int32_t)ch;
	e[EV_MODS / 4] = (int32_t)mods;
	e[EV_WIN / 4] = (int32_t)V.win;
}

static int ev_quiet; /* 1 : seulement les événements pris ; 2 : rien */

static R4 wev(const char *what)
{
	R4 r = R1(UI_W_EVENT, 0);
	if (!ev_quiet || (ev_quiet == 1 && (r.r2 || r.r1))) {
		ps("  ev ");
		ps(what);
		kw("w", r.r0);
		kv("code", r.r1);
		kv("pris", r.r2);
		kx("sum", shash());
		nl();
	}
	return r;
}

static long lastx = -100, lasty = -100;
static long reset_w; /* étiquette où un appui efface la mémoire du double-clic */
static long reset_x, reset_y;

static void move(long x, long y)
{
	ev_set(WEV_MOUSE, 0, x, y, 0, 0, 0);
	wev("bouge");
}

static void press(long x, long y, long buttons)
{
	ev_set(WEV_MOUSE, 1, x, y, buttons, 0, 0);
	wev("appui");
	lastx = x;
	lasty = y;
}

static void release(long x, long y, long buttons)
{
	ev_set(WEV_MOUSE, 2, x, y, buttons, 0, 0);
	wev("relache");
}

/* un appui ailleurs (sur une étiquette) : le clic suivant ne sera pas un double-clic */
static void reset_click(void)
{
	if (!reset_w)
		return;
	int q = ev_quiet;
	ev_quiet = 1;
	ev_set(WEV_MOUSE, 1, reset_x, reset_y, 1, 0, 0);
	R1(UI_W_EVENT, 0);
	ev_set(WEV_MOUSE, 2, reset_x, reset_y, 1, 0, 0);
	R1(UI_W_EVENT, 0);
	ev_quiet = q;
	lastx = reset_x;
	lasty = reset_y;
}

static void click(long x, long y)
{
	long dx = x - lastx, dy = y - lasty;
	if (dx < 0)
		dx = -dx;
	if (dy < 0)
		dy = -dy;
	if (dx < 8 && dy < 8)
		reset_click();
	ps(" clic ");
	pd(x);
	pc(',');
	pd(y);
	nl();
	press(x, y, 1);
	release(x, y, 1);
}

static void rclick(long x, long y) /* bouton droit */
{
	reset_click();
	ps(" clic droit ");
	pd(x);
	pc(',');
	pd(y);
	nl();
	press(x, y, 3);
	release(x, y, 3);
}

static void dblclick(long x, long y) /* exprès : deux appuis tout de suite */
{
	reset_click();
	ps(" double-clic ");
	pd(x);
	pc(',');
	pd(y);
	nl();
	press(x, y, 1);
	release(x, y, 1);
	press(x, y, 1);
	release(x, y, 1);
}

static void drag(long x0, long y0, long x1, long y1)
{
	reset_click();
	ps(" glisse ");
	pd(x0);
	pc(',');
	pd(y0);
	ps(" -> ");
	pd(x1);
	pc(',');
	pd(y1);
	nl();
	press(x0, y0, 1);
	ev_set(WEV_MOUSE, 0, (x0 + x1) / 2, (y0 + y1) / 2, 1, 0, 0);
	wev("bouge");
	ev_set(WEV_MOUSE, 0, x1, y1, 1, 0, 0);
	wev("bouge");
	release(x1, y1, 1);
}

static void wheel(long x, long y, long steps)
{
	ev_set(WEV_MOUSE, 0, x, y, 0, 0, 0);
	R1(UI_W_EVENT, 0);
	ev_set(WEV_WHEEL, 0, 0, steps, 0, 0, 0);
	ps(" molette ");
	pd(steps);
	nl();
	wev("molette");
}

static const char *key_name(long code)
{
	switch (code) {
	case KEY_ENTER: return "Entree";
	case KEY_ESC: return "Echap";
	case KEY_BACKSPACE: return "Retour";
	case KEY_TAB: return "Tab";
	case KEY_SPACE: return "Espace";
	case KEY_HOME: return "Debut";
	case KEY_END: return "Fin";
	case KEY_PGUP: return "PgHaut";
	case KEY_PGDN: return "PgBas";
	case KEY_DELETE: return "Suppr";
	case KEY_RIGHT: return "Droite";
	case KEY_LEFT: return "Gauche";
	case KEY_DOWN: return "Bas";
	case KEY_UP: return "Haut";
	}
	return "touche";
}

static R4 key(long code, long ch, long mods)
{
	char what[40];
	const char *n = key_name(code);
	long k = 0;
	if (mods & M_CTRL)
		for (const char *p = "Ctrl+"; *p; p++)
			what[k++] = *p;
	if (mods & M_SHIFT)
		for (const char *p = "Maj+"; *p; p++)
			what[k++] = *p;
	if (mods & M_ALT)
		for (const char *p = "Alt+"; *p; p++)
			what[k++] = *p;
	while (*n && k < 30)
		what[k++] = *n++;
	if (ch >= 0x20 && ch < 0x7F) {
		what[k++] = ':';
		what[k++] = (char)ch;
	}
	what[k] = 0;
	ev_set(WEV_KEY, 1, 0, 0, code, ch, mods);
	return wev(what);
}

static long hid_of(char c)
{
	if (c >= 'a' && c <= 'z')
		return 4 + (c - 'a');
	if (c >= 'A' && c <= 'Z')
		return 4 + (c - 'A');
	if (c >= '1' && c <= '9')
		return 0x1E + (c - '1');
	if (c == '0')
		return 0x27;
	if (c == ' ')
		return KEY_SPACE;
	if (c == '\n')
		return KEY_ENTER;
	return 0x38; /* (une touche de ponctuation) */
}

static void type(const char *s) /* des touches, sans sortie sauf la dernière */
{
	int q = ev_quiet;
	ev_quiet = 2;
	ps(" tape ");
	pz(s);
	nl();
	for (; *s; s++) {
		long mods = (*s >= 'A' && *s <= 'Z') ? M_SHIFT : 0;
		long ch = *s == '\n' ? 10 : (unsigned char)*s;
		if (!s[1])
			ev_quiet = q;
		key(hid_of(*s), ch, mods);
	}
	ev_quiet = q;
}

/* ---- rappels du programme (appelés par le kit) */

static long cb_count;
static uint32_t cb_hash;

static void cb_reset(void)
{
	cb_count = 0;
	cb_hash = 2166136261u;
}

static void cb_report(const char *what)
{
	ps("  rappels ");
	ps(what);
	kv("n", cb_count);
	kx("h", cb_hash);
	nl();
}

static long widx_raw(long w) /* indice (ou -1) : pour les sommes des rappels */
{
	for (int i = 0; i < nwt; i++)
		if (wt[i] == w)
			return i;
	return -1;
}

/* ui_w_on : x0 = widget, x1 = code */
static long cb_notify(long w, long code)
{
	ps("  rappel w_on");
	kw("w", w);
	kv("code", code);
	kv("user", C1(UI_W_USER, w));
	nl();
	return 0;
}

/* ui_textarea_keywords : x0 = mot, x1 = longueur -> 1 mot-clé */
static long cb_keyword(long word, long len)
{
	const char *s = (const char *)word;
	cb_count++;
	cb_hash = fnv_mix(cb_hash, (uint64_t)len);
	if (len > 0 && len < 64)
		for (long i = 0; i < len; i++)
			cb_hash = fnv_mix(cb_hash, (unsigned char)s[i]);
	static const char *const kws[] = {"print", "for", "next", "public", "class", "void", "if", "then"};
	for (unsigned k = 0; k < sizeof kws / sizeof kws[0]; k++) {
		const char *t = kws[k];
		long n = slen(t), i;
		if (n != len)
			continue;
		for (i = 0; i < n; i++)
			if ((s[i] | 0x20) != t[i])
				break;
		if (i == n)
			return 1;
	}
	return 0;
}

/* ui_list_virtual : x0 = liste, x1 = ligne -> texte */
static char vrow[64];
static long cb_vrow(long l, long row)
{
	cb_count++;
	cb_hash = fnv_mix(cb_hash, (uint64_t)widx_raw(l));
	cb_hash = fnv_mix(cb_hash, (uint64_t)row);
	char *d = vrow;
	const char *p = "lig ";
	while (*p)
		*d++ = *p++;
	long v = row, k = 0;
	char t[24];
	do
		t[k++] = (char)('0' + v % 10);
	while ((v /= 10) && k < 20);
	while (k)
		*d++ = t[--k];
	*d++ = '\t';
	*d++ = (char)('a' + (row * 7) % 26);
	*d++ = (char)('a' + (row * 3) % 26);
	*d++ = '\t';
	*d++ = (char)('0' + (row % 10));
	*d = 0;
	return (long)vrow;
}

/* ui_list_cell : liste, ligne, colonne, x, y, l, h, choisie -> 1 dessinée */
static long cb_cell(long l, long row, long col, long x, long y, long w, long h, long sel)
{
	cb_count++;
	uint64_t v[8] = {(uint64_t)widx_raw(l), (uint64_t)row, (uint64_t)col, (uint64_t)x,
			 (uint64_t)y, (uint64_t)w, (uint64_t)h, (uint64_t)sel};
	for (int i = 0; i < 8; i++)
		cb_hash = fnv_mix(cb_hash, v[i]);
	if (col != 1)
		return 0;
	C5(UI_FILL, x + 2, y + 2, w - 4, h - 4, sel ? 0x00C04020 : 0x0020A040 + (uint32_t)row * 0x101);
	return 1;
}

/* ui_view_on : dessin (vue, l, h), souris (vue, genre, x, y, infos), touche
 * (vue, code, caractère, mods), molette (vue, crans) */
static long cb_paint(long v, long w, long h)
{
	ps("  rappel dessin");
	kw("v", v);
	kv("l", w);
	kv("h", h);
	nl();
	C5(UI_FILL, 0, 0, w, h, 0x00203040);
	C7(UI_RBOX, 4, 4, w - 8, h - 8, 6, 0x00E0A030, 0x00A04010);
	C7(UI_TEXT, (long)"Vxq 42", 8, h / 2 + 4, 0x00FFFFFF, 13, FS, 0);
	C4(UI_VP_DISC, (w - 14) * 16, 14 * 16, 9 * 16 + 5, 0x0040C0F0);
	return 0;
}

static long cb_vmouse(long v, long kind, long x, long y, long info)
{
	ps("  rappel souris");
	kw("v", v);
	kv("genre", kind);
	kv("x", x);
	kv("y", y);
	ps(" infos=");
	px((uint64_t)info, 8);
	nl();
	return kind == 1 ? UN_CHANGE : 0;
}

static long cb_vkey(long v, long code, long ch, long mods)
{
	ps("  rappel touche");
	kw("v", v);
	kv("code", code);
	kv("car", ch);
	kv("mods", mods);
	nl();
	if (code == KEY_ESC)
		return -1;
	return code == KEY_ENTER ? UN_ACTIVATE : 0;
}

static long cb_vwheel(long v, long steps)
{
	ps("  rappel molette");
	kw("v", v);
	kv("crans", steps);
	nl();
	return UN_CHANGE;
}

/* ui_on_resize : x0 = largeur, x1 = hauteur */
static long cb_resize(long w, long h)
{
	ps("  rappel taille");
	kv("l", w);
	kv("h", h);
	nl();
	C2(UI_W_DRAW, 0, 0);
	return 0;
}

/* ---- polices du programme, sans fichiers */

static int no_cache; /* -n : polices embarquées sans cache de glyphes (diagnostic) */

static void fonts_embedded(void)
{
	struct {
		uint8_t *f;
		const unsigned char *d, *e;
	} t[3] = {{V.font, font_sans, font_sans_end}, {V.font_b, font_bold, font_bold_end},
		  {V.font_m, font_mono, font_mono_end}};
	for (int i = 0; i < 3; i++) {
		memset(t[i].f, 0, UI_FONT_SIZE);
		long r = ttf_open((long)t[i].f, (long)t[i].d, (long)(t[i].e - t[i].d), (long)scratch[i], UI_SCRATCH);
		ck(r != 0, "ttf_open d'une police embarquee");
		ttf_set_cache((long)t[i].f, (long)scratch[i] + UI_SCRATCH, no_cache ? 0 : UI_CACHE);
		*(long *)(t[i].f + F_FALLBACK) = 0;
	}
}

/* ---- thème */

static void theme_dump(const char *what)
{
	long j = C0(UI_JETONS), th = C0(UI_THEME);
	ps(what);
	kx("jetons", fnv((const void *)j, TJ_SIZE));
	kx("theme", fnv((const void *)th, TH_SIZE));
	kv("echelle", C0(UI_ECHELLE));
	ps(" metric=");
	for (int i = 0; i < 8; i++) {
		if (i)
			pc(',');
		pd(C1(UI_METRIC, i));
	}
	nl();
	ps("  jetons:");
	for (int i = 0; i < TJ_SIZE / 4; i++) {
		pc(' ');
		px(*(const uint32_t *)(j + i * 4), 8);
	}
	nl();
}

/* ui_theme_text écrit dans le texte (fins de ligne, « # ») : une copie inscriptible */
static char theme_buf[1024];
static void scpy(char *d, const char *s)
{
	while ((*d++ = *s++))
		;
}
static long theme_copy(const char *text)
{
	scpy(theme_buf, text);
	return C1(UI_THEME_TEXTE, theme_buf);
}

static void set_theme(const char *text)
{
	long r = theme_copy(text);
	ps("theme_texte ");
	pz(text);
	kv("r", r != 0);
	nl();
	theme_dump(" ");
}

static void test_theme(void)
{
	ps("== theme\n");
	set_theme("theme = Onyx Noir\nechelle = M\n");
	set_theme("theme = Givre\nechelle = L\naccent = orange\noutline = black\n");
	set_theme("theme = Onyx Noir\nechelle = P\naccent = sarcelle\noutline = dark\nwindow = #202428\n");
	set_theme("theme = givre\nechelle = M\naccent = violet\nactive = 1 2 3\nbutton = auto\nfield = auto\n");
	set_theme("# commentaire\n  theme   =   Inconnu  \necheLLe = M\naccent = rien\n");
	set_theme("theme = Givre\nechelle = M\ndensity = touch\n");
	set_theme("theme = Onyx Noir\nechelle = M\ndensite = tactile\naccent = vert\n");
}

/* ---- dessin de base */

static void test_basic(void)
{
	ps("== dessin de base\n");
	static const uint32_t cols[] = {0x00000000, 0x00FFFFFF, 0x00808080, 0x002563D8, 0x00D93A3F, 0x0014171C,
					0x00F5F6F8, 0x007F7F80, 0x00123456, 0xFF00FF00, 0x80FF8000};
	ps("tone/mix/ink:");
	for (unsigned i = 0; i < sizeof cols / sizeof cols[0]; i++) {
		static const long lv[] = {0, 1, 64, 116, 127, 128, 129, 140, 200, 255, 256, 300, -5};
		uint32_t h = 2166136261u;
		for (unsigned k = 0; k < sizeof lv / sizeof lv[0]; k++)
			h = fnv_mix(h, (uint32_t)C2(UI_TONE, cols[i], lv[k]));
		static const long mt[] = {0, 1, 100, 128, 200, 255, 256, 257, -1};
		for (unsigned k = 0; k < sizeof mt / sizeof mt[0]; k++)
			h = fnv_mix(h, (uint32_t)C3(UI_MIX, cols[i], cols[(i + 3) % 11], mt[k]));
		pc(' ');
		px((uint32_t)C2(UI_TONE, cols[i], 100), 8);
		pc('/');
		px((uint32_t)C3(UI_MIX, cols[i], cols[(i + 5) % 11], 77), 8);
		pc('/');
		px((uint32_t)C1(UI_INK, cols[i]), 8);
		pc('/');
		px(h, 8);
	}
	nl();

	clear(0x00102030);
	sum("efface");
	C5(UI_FILL, 10, 10, 50, 30, 0x00FF0000);
	C5(UI_FILL, -10, -10, 30, 25, 0x0000FF00);
	C5(UI_FILL, 300, 220, 50, 50, 0x000000FF);
	C5(UI_FILL, 100, 100, 0, 10, 0x00FFFFFF);
	C5(UI_FILL, 100, 100, -5, -5, 0x00FFFFFF);
	C5(UI_FILL, 0, 0, 400, 3, 0x80FFFF00);
	C5(UI_FILL, 0xFFFFFFF0L, 50, 40, 10, 0x00ABCDEF); /* (une coordonnée 32 bits « négative ») */
	sum("ui_fill");
	C5(UI_FRAME, 20, 60, 80, 40, 0x00FFFFFF);
	C5(UI_FRAME, -5, 200, 30, 60, 0x00FF00FF);
	C5(UI_FRAME, 150, 10, 1, 1, 0x00FFFF00);
	C5(UI_FRAME, 160, 10, 2, 2, 0x0000FFFF);
	C5(UI_FRAME, 170, 10, 0, 5, 0x00FFFFFF);
	sum("ui_frame");
	static const long pts[][2] = {{0, 0}, {319, 0}, {0, 239}, {319, 239}, {320, 10}, {10, 240},
				      {-1, 5}, {5, -1}, {160, 120}, {-1000, -1000}, {100000, 3}};
	for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++)
		C3(UI_PIXEL, pts[i][0], pts[i][1], 0x00FFFFFF - (uint32_t)i * 0x111111);
	sum("ui_pixel");

	clear(0x00F5F6F8);
	for (long r = 0; r <= 14; r++) {
		long x = 4 + (r % 5) * 62, y = 4 + (r / 5) * 50;
		C7(UI_RBOX, x, y, 40 + r, 30 + (r & 1), r, 0x002563D8, 0x002563D8);
		C7(UI_RBOX, x + 18, y + 20, 37 + r, 25 - (r & 3), r, 0x00E0A030, 0x00301060);
	}
	sum("ui_rbox rayons 0..14, degrades");
	clear(0x00F5F6F8);
	C7(UI_RBOX, -10, -10, 60, 50, 12, 0x00FF0000, 0x000000FF);
	C7(UI_RBOX, 290, 210, 60, 50, 9, 0x0000FF00, 0x0000FF00);
	C7(UI_RBOX, 100, 100, 7, 5, 14, 0x00000000, 0x00FFFFFF);
	C7(UI_RBOX, 120, 100, 1, 1, 3, 0x00FFFFFF, 0x00FFFFFF);
	C7(UI_RBOX, 130, 100, -8, 10, 4, 0x00FFFFFF, 0x00FFFFFF);
	C7(UI_RBOX, 140, 100, 10, -8, 4, 0x00FFFFFF, 0x00FFFFFF);
	C7(UI_RBOX, 150, 100, 31, 31, 40, 0x00102030, 0x00F0E0D0);
	C7(UI_RBOX, 200, 40, 61, 33, 8, 0xC4000000, 0xC4000000);
	C7(UI_RBOX, 200, 80, 61, 33, 8, 0x40FF0000, 0x8000FF00);
	sum("ui_rbox bords, tailles impaires, negatives, transparence");
	clear(0x00F5F6F8);
	C2(UI_OMBRE, 200, 150);
	sum("ui_shadow 200x150");
	C2(UI_OMBRE, 33, 17);
	sum("ui_shadow 33x17");

	clear(0x00FFFFFF);
	C4(UI_CLIP, 50, 40, 100, 80);
	C5(UI_FILL, 0, 0, 320, 240, 0x00FF0000);
	C7(UI_RBOX, 30, 30, 80, 60, 10, 0x000000FF, 0x000000FF);
	C7(UI_TEXT, (long)"Clip Wxyz 123", 20, 70, 0x00000000, 20, FS, 0);
	C3(UI_PIXEL, 49, 40, 0);
	C3(UI_PIXEL, 50, 40, 0);
	C3(UI_PIXEL, 149, 119, 0);
	C3(UI_PIXEL, 150, 119, 0);
	C4(UI_CLIP, 0, 0, 0, 0);
	C5(UI_FILL, 200, 200, 10, 10, 0x0000FF00);
	sum("ui_clip");
	C4(UI_CLIP, -20, -20, 60, 60);
	C5(UI_FILL, 0, 0, 320, 240, 0x00FFFF00);
	C4(UI_CLIP, 300, 200, 100, 100);
	C5(UI_FILL, 0, 0, 320, 240, 0x0000FFFF);
	C4(UI_CLIP, 10, 10, -5, 50);
	C5(UI_FILL, 0, 0, 20, 20, 0x00FF00FF);
	C4(UI_CLIP, 0, 0, 0, 0);
	sum("ui_clip bords");

	clear(0x00F5F6F8);
	C6(UI_BUTTON, 10, 50, 100, 32, (long)"Zorqa", 0);
	C6(UI_BUTTON, 120, 50, 100, 32, (long)"Zorqa", 1);
	C6(UI_BUTTON, 230, 50, 40, 20, (long)"Un texte trop long", 0);
	C6(UI_BUTTON, 10, 100, 60, 40, (long)"", 1);
	sum("ui_button");

	/* ui_hit lit le point de ui_ev */
	ev_set(WEV_MOUSE, 1, 15, 25, 1, 0, 0);
	ps("ui_hit:");
	kv("dedans", C4(UI_HIT, 10, 20, 10, 10));
	kv("bord", C4(UI_HIT, 15, 25, 1, 1));
	kv("dehors", C4(UI_HIT, 16, 25, 5, 5));
	kv("vide", C4(UI_HIT, 15, 25, 0, 0));
	nl();
}

/* ---- texte */

static const char *const txts[] = {
	"Hello, Korin!",
	"Wxyz gjpq 0123456789",
	"\xC3\xA9t\xC3\xA9 \xC3\xA0 l'\xC3\xA9" "cole \xE2\x80\x94 \xC2\xAB ok \xC2\xBB",
	"tab\tavant\tapres",
	"AVAWTo Ty LT ff fi",
	"\xE2\x82\xAC \xE2\x9C\x93 \xE2\x86\x92 \xE2\x96\xB6 \xF0\x9F\x98\x80 \xE4\xB8\xAD",
	"",
	"   ",
	"x\xC3(y \xE2\x82z \xFF\x80!", /* UTF-8 abîmé (le NUL final n'est jamais pris dans un caractère) */
};

static int dbg_text; /* -t : sommes texte par texte (diagnostic) */

static void test_text(void)
{
	static const long sizes[] = {9, 11, 13, 14, 16, 20, 28, 40};
	const long fonts[3] = {FS, FB, FM};
	const char *fname[3] = {"sans", "gras", "mono"};
	ps("== texte\n");
	for (int f = 0; f < 3; f++)
		for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
			long px_ = sizes[si];
			clear(0x00FFFFFF);
			uint32_t wh = 2166136261u;
			long y = 2, endx = 0;
			for (unsigned t = 0; t < sizeof txts / sizeof txts[0]; t++) {
				y += px_ + 2;
				endx = C7(UI_TEXT, (long)txts[t], 2, y, (t & 1) ? 0x002563D8 : 0x0014171C, px_, fonts[f], 0);
				if (dbg_text) {
					ps("  t");
					pd(t);
					kv("x", endx);
					kx("sum", shash());
					nl();
				}
				wh = fnv_mix(wh, (uint64_t)endx);
				wh = fnv_mix(wh, (uint64_t)C3(UI_TEXT_WIDTH, (long)txts[t], px_, fonts[f]));
			}
			ps("ui_text ");
			ps(fname[f]);
			kv("px", px_);
			kv("larg0", C3(UI_TEXT_WIDTH, (long)txts[0], px_, fonts[f]));
			kx("largs", wh);
			kx("sum", shash());
			nl();
		}
	clear(0x00FFFFFF);
	long r1 = C7(UI_TEXT, (long)"Largeur maximum depassee", 10, 30, 0, 16, FS, 60);
	long r2 = C7(UI_TEXT, (long)"Negatif", -15, 60, 0, 20, FB, 0);
	long r3 = C7(UI_TEXT, (long)"Negatif max", -15, 90, 0, 20, FB, 50);
	long r4 = C7(UI_TEXT, (long)"Bas", 290, 245, 0, 28, FS, 0);
	long r5 = C7(UI_TEXT, (long)"Haut", 100, 5, 0, 28, FM, 0);
	long r6 = C7(UI_TEXT, (long)"Droite qui deborde", 260, 120, 0, 16, FS, 0);
	long r7 = C7(UI_TEXT, (long)"Transparent?", 10, 150, 0x80FF0000, 20, FS, 0);
	ps("ui_text bords:");
	kv("r1", r1);
	kv("r2", r2);
	kv("r3", r3);
	kv("r4", r4);
	kv("r5", r5);
	kv("r6", r6);
	kv("r7", r7);
	kx("sum", shash());
	nl();

	clear(0x00FFFFFF);
	ps("ui_text_fit:");
	static const long maxw[] = {0, 1, 10, 30, 60, 90, 120, 400};
	for (unsigned i = 0; i < sizeof maxw / sizeof maxw[0]; i++) {
		long r = C7(UI_TEXT_FIT, (long)"Un long texte a couper ici", 4, 14 + 18 * (long)i, 0x0014171C, 14, FS,
			    maxw[i]);
		kv("r", r);
	}
	kv("utf8", C7(UI_TEXT_FIT, (long)txts[2], 150, 30, 0, 16, FB, 70));
	kx("sum", shash());
	nl();

	ps("ui_text_prefix:");
	for (long n = 0; n <= 14; n += 2)
		kv("n", C4(UI_TEXT_PREFIX, (long)txts[2], n, 16, FS));
	kv("tab", C4(UI_TEXT_PREFIX, (long)txts[3], 6, 14, FM));
	kv("trop", C4(UI_TEXT_PREFIX, (long)"abc", 50, 14, FS));
	nl();
	ps("ui_text_hit:");
	static const long hx[] = {-10, 0, 1, 3, 7, 15, 30, 55, 80, 200, 10000};
	for (unsigned i = 0; i < sizeof hx / sizeof hx[0]; i++) {
		R4 r = R4f(UI_TEXT_HIT, (long)"Wxyz \xC3\xA9t\xC3\xA9 fin", 16, FS, hx[i]);
		ps(" (");
		pd(r.r0);
		pc(',');
		pd(r.r1);
		pc(')');
	}
	R4 r = R4f(UI_TEXT_HIT, (long)"", 14, FB, 5);
	ps(" vide=(");
	pd(r.r0);
	pc(',');
	pd(r.r1);
	ps(")\n");

	static const char wraptext[] = "Le texte coupe en lignes aux espaces, avec un motextremementlongquidepasse "
				       "et\nune nouvelle ligne.\n\nPuis une ligne vide avant.";
	static const long ww[] = {1, 40, 90, 150, 300};
	for (unsigned i = 0; i < sizeof ww / sizeof ww[0]; i++) {
		clear(0x00FFFFFF);
		long m = C8(UI_TEXT_WRAP, (long)wraptext, 5, 5, ww[i], 0x0014171C, FS, 13, 1);
		long d = C8(UI_TEXT_WRAP, (long)wraptext, 5, 5, ww[i], 0x0014171C, FS, 13, 0);
		long m2 = C8(UI_TEXT_WRAP, (long)wraptext, 160, 5, ww[i], 0x002563D8, FB, 16, 1);
		ps("ui_text_wrap");
		kv("larg", ww[i]);
		kv("mesure", m);
		kv("dessin", d);
		kv("gras16", m2);
		kx("sum", shash());
		nl();
	}
	clear(0x00FFFFFF);
	ps("ui_text_wrap vide:");
	kv("h", C8(UI_TEXT_WRAP, (long)"", 0, 0, 100, 0, FS, 13, 0));
	kv("h2", C8(UI_TEXT_WRAP, (long)"\n\n", 0, 0, 100, 0, FM, 11, 1));
	nl();
}

/* ---- glyphes de repli, dessins anticrénelés, indications, navigation */

static void test_vector(void)
{
	ps("== glyphes (repli), vp, pad_hints, objnav\n");
	clear(0x00F5F6F8);
	static const long sides[] = {0, 1, 8, 16, 18, 24, 31, 40, 64, 300};
	for (unsigned i = 0; i < sizeof sides / sizeof sides[0]; i++)
		C5(UI_GLYPH, (long)"glyph-uitest-absent", 4 + (long)i * 30, 10, sides[i], 0x002563D8);
	C5(UI_GLYPH, (long)"", 10, 120, 20, 0x00FF0000);
	C5(UI_GLYPH, (long)"glyph-un-nom-beaucoup-trop-long-pour-la-table-des-icones-du-kit-xxxx", 40, 120, 20,
	   0x00FF0000);
	C5(UI_GLYPH, (long)"glyph-uitest-absent", -5, 200, 24, 0xFF00FF00);
	sum("ui_glyph (noms absents : repli)");

	clear(0x00FFFFFF);
	static const long rad[] = {0, 1, 8, 15, 16, 17, 24, 40, 80, 160, 333};
	for (unsigned i = 0; i < sizeof rad / sizeof rad[0]; i++)
		C4(UI_VP_DISC, (20 + (long)i * 27) * 16 + (long)i, 30 * 16 + 7, rad[i] * 8, 0x002563D8);
	C4(UI_VP_DISC, 160 * 16, 120 * 16, 50 * 16 + 3, 0x00D93A3F);
	C4(UI_VP_DISC, -5 * 16, -5 * 16, 20 * 16, 0x0000A000);
	C4(UI_VP_DISC, 318 * 16, 238 * 16, 9 * 16, 0x00A000A0);
	sum("ui_vp_disc");
	clear(0x00FFFFFF);
	static const long ln[][5] = {{10, 10, 300, 10, 16},   {10, 20, 300, 60, 8},   {10, 200, 300, 40, 32},
				     {160, 30, 160, 220, 24}, {50, 50, 50, 50, 40},   {-20, 100, 340, 130, 12},
				     {20, 230, 25, 235, 1},   {100, 100, 101, 180, 5}, {300, 200, 10, 210, 64}};
	for (unsigned i = 0; i < sizeof ln / sizeof ln[0]; i++)
		C6(UI_VP_LINE, ln[i][0] * 16 + 3, ln[i][1] * 16 + 9, ln[i][2] * 16, ln[i][3] * 16 + 1, ln[i][4],
		   0x00000000 + (uint32_t)i * 0x1C1408);
	sum("ui_vp_line");
	clear(0x00FFFFFF);
	static const int32_t tri[] = {20 * 16, 20 * 16, 150 * 16 + 5, 40 * 16, 60 * 16, 140 * 16 + 11};
	static const int32_t star[] = {240 * 16, 20 * 16,  255 * 16, 70 * 16, 305 * 16, 75 * 16, 265 * 16, 105 * 16,
				       280 * 16, 155 * 16, 240 * 16, 125 * 16, 200 * 16, 155 * 16, 215 * 16, 105 * 16,
				       175 * 16, 75 * 16,  225 * 16, 70 * 16};
	static const int32_t neg[] = {-30 * 16, 150 * 16, 80 * 16, 170 * 16, 40 * 16, 260 * 16};
	static const int32_t flat[] = {10 * 16, 220 * 16, 200 * 16, 220 * 16, 100 * 16, 220 * 16};
	C3(UI_VP_POLY, (long)tri, 3, 0x002563D8);
	C3(UI_VP_POLY, (long)star, 10, 0x00E0A030);
	C3(UI_VP_POLY, (long)neg, 3, 0x0000A060);
	C3(UI_VP_POLY, (long)flat, 3, 0x00FF0000);
	C3(UI_VP_POLY, (long)tri, 2, 0x00FF0000);
	C3(UI_VP_POLY, (long)tri, 0, 0x00FF0000);
	sum("ui_vp_poly");

	clear(0x00F5F6F8);
	static const char *const hints[] = {"A\tchoisir\nB\tretour", "+\tbouger\nStart\tpause\nL\tprec\nR\tsuiv",
					    "X\tun\nY\tdeux\nSelect\ttrois", "", "A\t", "LongBouton\tquoi"};
	ps("ui_pad_hints:");
	for (unsigned i = 0; i < sizeof hints / sizeof hints[0]; i++) {
		long m = C5(UI_PAD_HINTS, (long)hints[i], 5, 5 + (long)i * 34, 30, 1);
		long d = C5(UI_PAD_HINTS, (long)hints[i], 5, 5 + (long)i * 34, 30, 0);
		kv("m", m);
		kv("d", d);
	}
	kx("sum", shash());
	nl();

	static const int32_t rects[] = {10, 10, 40, 20,  100, 10, 40, 20, 190, 12, 40, 20, 10, 100, 40, 20,
					100, 95, 40, 30, 190, 100, 60, 20, 50, 200, 200, 20, 0, 0, 0, 0};
	ps("ui_objnav:");
	for (int cur = 0; cur < 9; cur++)
		for (int dir = 0; dir < 5; dir++)
			kv("", C4(UI_OBJNAV, (long)rects, 8, cur, dir));
	kv("n0", C4(UI_OBJNAV, (long)rects, 0, 0, 1));
	kv("neg", C4(UI_OBJNAV, (long)rects, 8, -1, 1));
	nl();
}

/* ---- divers sans dessin */

static char argbuf[6][64];

static void test_misc(void)
{
	ps("== divers\n");
	static const char *const args[] = {"simple", "\"avec espaces\" reste", "\"echap \\\" et \\\\ fin\"", "\"",
					   "", "\"non ferme"};
	ps("ui_arg:");
	for (int i = 0; i < 6; i++) {
		long n = slen(args[i]);
		memcpy(argbuf[i], args[i], (size_t)n + 1);
		long r = C1(UI_ARG, argbuf[i]);
		pc(' ');
		if (r == (long)argbuf[i])
			pz((const char *)r);
		else
			ps("autre");
	}
	kv("nul", C1(UI_ARG, 0));
	nl();
	ps("ui_tr:");
	static const char *const trs[] = {"Zqwx", "uitest sans traduction", ""};
	for (int i = 0; i < 3; i++) {
		long r = C1(UI_TR, trs[i]);
		pc(' ');
		pz((const char *)r);
		if (r == (long)trs[i])
			ps("(meme)");
	}
	kv("nul", C1(UI_TR, 0));
	kv("tr_load", C1(UI_TR_LOAD, "/tmp/uitest-absent.txt"));
	nl();

	/* menus : texte, états, raccourcis (envoi à Elegant : échoue, sans importance) */
	static const char menus[] = "MZqfic\nI101\tZnouv\tCtrl+N\nI102\tZouvr\tCtrl+Maj+O\nI103\tZfin\tAlt+F4\n"
				    "-\nMZedi\nI201\tZann\tCtrl+Z\nI202\tZtout\tF3\nI203\tZchif\tCtrl+5\nI204\tZz\tCtrl+0\n";
	C1(UI_MENU, menus);
	static const long keys[][2] = {{0x11, 0},      {0x11, M_CTRL}, {0x12, M_CTRL | M_SHIFT}, {0x12, M_CTRL},
				       {0x3D, M_ALT},  {0x3C, 0},      {0x1D, M_CTRL},           {0x22, M_CTRL},
				       {0x27, M_CTRL}, {0x3D, 0}};
	ps("ui_menu_key:");
	for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
		ev_set(WEV_KEY, 1, 0, 0, keys[i][0], 0, keys[i][1]);
		kv("", C1(UI_MENU_KEY, 0));
	}
	C2(UI_MENU_STATE, 101, UMS_DISABLED);
	C2(UI_MENU_STATE, 202, UMS_CHECKED);
	ev_set(WEV_KEY, 1, 0, 0, 0x11, 0, M_CTRL);
	kv("grisee", C1(UI_MENU_KEY, 0));
	C2(UI_MENU_STATE, 101, 0);
	kv("rendue", C1(UI_MENU_KEY, (long)V.ev));
	ev_set(WEV_KEY, 0, 0, 0, 0x11, 0, M_CTRL);
	kv("relache", C1(UI_MENU_KEY, 0));
	ev_set(WEV_MOUSE, 1, 0, 0, 0x11, 0, M_CTRL);
	kv("souris", C1(UI_MENU_KEY, 0));
	nl();
	C1(UI_MENU, 0);
	ev_set(WEV_KEY, 1, 0, 0, 0x11, 0, M_CTRL);
	ps("ui_menu_key sans menu:");
	kv("r", C1(UI_MENU_KEY, 0));
	nl();

	/* appels qui parlent au serveur ou ne font que garder une valeur : la valeur
	 * rendue n'est pas imprimée (erreur du serveur, ou E_NOSYS sur le PC) */
	C4(UI_UPDATE, 0, 0, 10, 10);
	C0(UI_UPDATE_ALL);
	C1(UI_CURSOR, UC_HAND);
	C1(UI_CURSOR, 0);
	C1(UI_FILE_FILTER, "*.txt;*.md");
	C1(UI_FILE_FILTER, 0);
	C3(UI_TIP_AT, 10, 10, 0);
	C2(UI_MIN_SIZE, 150, 100);
	ps("ui_window_count:");
	kv("n", C0(UI_WINDOW_COUNT));
	kv("use_inconnue", C1(UI_WINDOW_USE, 0x12345));
	kv("close_inconnue", C1(UI_WINDOW_CLOSE, 0x12345) < 0);
	kv("w_window0", C1(UI_W_WINDOW, 0));
	nl();
}

/* ---- images */

static uint8_t imgbuf[8192];

static void put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

static void put32be(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

/* BMP : bpp 1, 4, 8 (palette de 256), 24, 32 ; h < 0 : de haut en bas */
static long bmp_make(uint8_t *b, long w, long h, long bpp, long comp)
{
	long ah = h < 0 ? -h : h;
	long pitch = ((w * bpp + 31) >> 5) << 2;
	long pal = bpp <= 8 ? 256 * 4 : 0;
	long off = 14 + 40 + pal;
	long size = off + pitch * ah;
	memset(b, 0, (size_t)size);
	b[0] = 'B';
	b[1] = 'M';
	put32(b + 2, (uint32_t)size);
	put32(b + 10, (uint32_t)off);
	put32(b + 14, 40);
	put32(b + 18, (uint32_t)w);
	put32(b + 22, (uint32_t)h);
	put16(b + 26, 1);
	put16(b + 28, (uint32_t)bpp);
	put32(b + 30, (uint32_t)comp);
	for (long i = 0; i < 256 && pal; i++)
		put32(b + 54 + i * 4, (uint32_t)(i * 0x010307 + 0x204060) & 0x00FFFFFF);
	for (long y = 0; y < ah; y++)
		for (long x = 0; x < pitch; x++)
			b[off + y * pitch + x] = (uint8_t)(x * 37 + y * 101 + (x * y) % 13);
	if (bpp == 32)
		for (long y = 0; y < ah; y++)
			for (long x = 0; x < w; x++)
				b[off + y * pitch + x * 4 + 3] = (uint8_t)((x + y) & 1 ? 0 : 0x80);
	return size;
}

static uint32_t crc_tab[256];

static void crc_init(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++)
			c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crc_tab[i] = c;
	}
}

static uint32_t crc32(const uint8_t *p, long n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (long i = 0; i < n; i++)
		c = crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static long png_chunk(uint8_t *b, long at, const char *type, const uint8_t *data, long n)
{
	put32be(b + at, (uint32_t)n);
	memcpy(b + at + 4, type, 4);
	if (n)
		memcpy(b + at + 8, data, (size_t)n);
	put32be(b + at + 8 + n, crc32(b + at + 4, n + 4));
	return at + 12 + n;
}

/* PNG à blocs deflate stockés : lignes brutes (octet de filtre compris) */
static long png_make(uint8_t *b, long w, long h, long depth, long color, long interlace, const uint8_t *raw,
		     long rawn, const uint8_t *plte, long npl, const uint8_t *trns, long ntr)
{
	static uint8_t z[4096];
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
	uint8_t ihdr[13];
	long at = 8, zn = 0;
	memcpy(b, sig, 8);
	put32be(ihdr, (uint32_t)w);
	put32be(ihdr + 4, (uint32_t)h);
	ihdr[8] = (uint8_t)depth;
	ihdr[9] = (uint8_t)color;
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = (uint8_t)interlace;
	at = png_chunk(b, at, "IHDR", ihdr, 13);
	if (npl)
		at = png_chunk(b, at, "PLTE", plte, npl);
	if (ntr)
		at = png_chunk(b, at, "tRNS", trns, ntr);
	z[zn++] = 0x78;
	z[zn++] = 0x01;
	z[zn++] = 1; /* dernier bloc, stocké */
	z[zn++] = (uint8_t)rawn;
	z[zn++] = (uint8_t)(rawn >> 8);
	z[zn++] = (uint8_t)~rawn;
	z[zn++] = (uint8_t)(~rawn >> 8);
	memcpy(z + zn, raw, (size_t)rawn);
	zn += rawn;
	uint32_t a = 1, s2 = 0;
	for (long i = 0; i < rawn; i++) {
		a = (a + raw[i]) % 65521;
		s2 = (s2 + a) % 65521;
	}
	put32be(z + zn, s2 << 16 | a);
	zn += 4;
	at = png_chunk(b, at, "IDAT", z, zn);
	at = png_chunk(b, at, "IEND", 0, 0);
	return at;
}

static long img_report(const char *what, long img)
{
	ps("image ");
	ps(what);
	if (!img) {
		ps(" : 0\n");
		return 0;
	}
	R4 r = R1(UI_IMAGE_SIZE, img);
	kv("l", r.r0);
	kv("h", r.r1);
	if (r.r0 > 0 && r.r1 > 0 && r.r0 * r.r1 <= 1 << 20)
		kx("pix", fnv((const void *)r.r2, r.r0 * r.r1 * 4));
	nl();
	return img;
}

static long imgs[16];
static int nimg;

static long keep_img(long i)
{
	if (i && nimg < 16)
		imgs[nimg++] = i;
	return i;
}

static void test_images(void)
{
	ps("== images\n");
	crc_init();
	/* BMP */
	static const long bm[][4] = {{7, 5, 24, 0}, {4, -3, 32, 0}, {5, 4, 8, 0}, {9, 3, 4, 0},
				     {17, 2, 1, 0}, {3, 3, 32, 3}, {3, 3, 16, 0}, {3, 3, 24, 1}};
	for (unsigned i = 0; i < sizeof bm / sizeof bm[0]; i++) {
		long n = bmp_make(imgbuf, bm[i][0], bm[i][1], bm[i][2], bm[i][3]);
		char what[32] = "bmp ";
		what[4] = (char)('0' + i);
		what[5] = 0;
		keep_img(img_report(what, C2(UI_IMAGE_DECODE, imgbuf, n)));
	}
	long n = bmp_make(imgbuf, 7, 5, 24, 0);
	img_report("bmp tronque", C2(UI_IMAGE_DECODE, imgbuf, n - 10));
	img_report("bmp trop court", C2(UI_IMAGE_DECODE, imgbuf, 20));
	put32(imgbuf + 18, 0);
	img_report("bmp largeur 0", C2(UI_IMAGE_DECODE, imgbuf, n));
	img_report("inconnu", C2(UI_IMAGE_DECODE, "GIF89a et d'autres octets...", 28));

	/* PNG faits ici (deflate stocké) : filtres 0-4, profondeurs, tRNS */
	uint8_t raw[512];
	long k = 0;
	for (long y = 0; y < 5; y++) { /* gris 8 bits 6 x 5, un filtre par ligne */
		raw[k++] = (uint8_t)y;
		for (long x = 0; x < 6; x++)
			raw[k++] = (uint8_t)(x * 40 + y * 13);
	}
	n = png_make(imgbuf, 6, 5, 8, 0, 0, raw, k, 0, 0, 0, 0);
	keep_img(img_report("png gris8 filtres", C2(UI_IMAGE_DECODE, imgbuf, n)));
	k = 0;
	for (long y = 0; y < 3; y++) { /* gris 1 bit 11 x 3 */
		raw[k++] = 0;
		raw[k++] = (uint8_t)(0xA5 ^ (y * 0x33));
		raw[k++] = (uint8_t)(0xE0 >> y);
	}
	n = png_make(imgbuf, 11, 3, 1, 0, 0, raw, k, 0, 0, 0, 0);
	keep_img(img_report("png gris1", C2(UI_IMAGE_DECODE, imgbuf, n)));
	k = 0;
	for (long y = 0; y < 2; y++) { /* gris 16 bits 3 x 2, tRNS */
		raw[k++] = y ? 2 : 0;
		for (long x = 0; x < 3; x++) {
			raw[k++] = (uint8_t)(x * 90 + y);
			raw[k++] = (uint8_t)(x * 7);
		}
	}
	static const uint8_t tr16[2] = {0, 0};
	n = png_make(imgbuf, 3, 2, 16, 0, 0, raw, k, 0, 0, tr16, 2);
	keep_img(img_report("png gris16 trns", C2(UI_IMAGE_DECODE, imgbuf, n)));
	k = 0;
	for (long y = 0; y < 2; y++) { /* RVB 16 bits 2 x 2 */
		raw[k++] = 1;
		for (long x = 0; x < 12; x++)
			raw[k++] = (uint8_t)(x * 21 + y * 5);
	}
	n = png_make(imgbuf, 2, 2, 16, 2, 0, raw, k, 0, 0, 0, 0);
	keep_img(img_report("png rvb16", C2(UI_IMAGE_DECODE, imgbuf, n)));
	k = 0;
	for (long y = 0; y < 4; y++) { /* palette 2 bits 7 x 4, tRNS de palette */
		raw[k++] = y == 3 ? 4 : 0;
		raw[k++] = (uint8_t)(0x1B + y * 0x40);
		raw[k++] = (uint8_t)(0xE4 >> (y & 1));
	}
	static const uint8_t pl[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 250, 250, 10};
	static const uint8_t tp[3] = {255, 128, 0};
	n = png_make(imgbuf, 7, 4, 2, 3, 0, raw, k, pl, 12, tp, 3);
	keep_img(img_report("png palette2 trns", C2(UI_IMAGE_DECODE, imgbuf, n)));
	k = 0;
	for (long y = 0; y < 3; y++) { /* gris + alpha 8 bits 4 x 3, filtre Paeth */
		raw[k++] = 4;
		for (long x = 0; x < 8; x++)
			raw[k++] = (uint8_t)(x * 31 + y * 17);
	}
	n = png_make(imgbuf, 4, 3, 8, 4, 0, raw, k, 0, 0, 0, 0);
	keep_img(img_report("png gris_alpha", C2(UI_IMAGE_DECODE, imgbuf, n)));
	k = 0;
	for (long y = 0; y < 3; y++) { /* RVB 8 bits, tRNS couleur */
		raw[k++] = 3;
		for (long x = 0; x < 9; x++)
			raw[k++] = (uint8_t)(x * 29 + y * 3);
	}
	static const uint8_t trgb[6] = {0, 0, 0, 29, 0, 58};
	n = png_make(imgbuf, 3, 3, 8, 2, 0, raw, k, 0, 0, trgb, 6);
	keep_img(img_report("png rvb8 trns", C2(UI_IMAGE_DECODE, imgbuf, n)));
	n = png_make(imgbuf, 6, 5, 8, 0, 1, raw, 31, 0, 0, 0, 0);
	img_report("png entrelace", C2(UI_IMAGE_DECODE, imgbuf, n));
	n = png_make(imgbuf, 6, 5, 8, 0, 0, raw, 12, 0, 0, 0, 0);
	img_report("png donnees courtes", C2(UI_IMAGE_DECODE, imgbuf, n));
	img_report("png tronque", C2(UI_IMAGE_DECODE, png_rgb, 60));

	/* PNG embarqués (Huffman dynamique) */
	keep_img(img_report("png rvba", C2(UI_IMAGE_DECODE, png_rgba, png_rgba_end - png_rgba)));
	keep_img(img_report("png palette", C2(UI_IMAGE_DECODE, png_pal, png_pal_end - png_pal)));
	keep_img(img_report("png rvb", C2(UI_IMAGE_DECODE, png_rgb, png_rgb_end - png_rgb)));
	long ga = img_report("png gris_alpha 512", C2(UI_IMAGE_DECODE, png_ga, png_ga_end - png_ga));

	/* dessins : taille réelle, mise à l'échelle, alpha, bords */
	clear(0x00808080);
	long x = 2, y = 2;
	for (int i = 0; i < nimg; i++) {
		C5(UI_IMAGE_DRAW, imgs[i], x, y, 0, 0);
		x += 34;
		if (x > 290) {
			x = 2;
			y += 34;
		}
	}
	sum("ui_image_draw taille reelle");
	clear(0x00F5F6F8);
	x = 2;
	y = 2;
	for (int i = 0; i < nimg; i++) {
		C5(UI_IMAGE_DRAW, imgs[i], x, y, 45 - i, 23 + i * 2);
		x += 50;
		if (x > 280) {
			x = 2;
			y += 50;
		}
	}
	sum("ui_image_draw a l'echelle");
	clear(0x00F5F6F8);
	if (ga) {
		C5(UI_IMAGE_DRAW, ga, 0, 0, 320, 240);
		sum("ui_image_draw 512 -> 320x240");
		C5(UI_IMAGE_DRAW, ga, -100, -50, 0, 0);
		sum("ui_image_draw 512 decale");
		C5(UI_IMAGE_DRAW, ga, 10, 10, 7, 3);
		C5(UI_IMAGE_DRAW, ga, 300, 220, 64, 64);
		sum("ui_image_draw 512 petit, bord");
	}
	if (nimg > 0) {
		C4(UI_CLIP, 20, 20, 40, 30);
		C5(UI_IMAGE_DRAW, imgs[nimg - 1], 0, 0, 100, 100);
		C4(UI_CLIP, 0, 0, 0, 0);
		C5(UI_IMAGE_DRAW, imgs[0], 200, 100, -10, 20);
		C5(UI_IMAGE_DRAW, 0, 200, 100, 10, 20);
		sum("ui_image_draw decoupe, negatif, image 0");
	}
	C1(UI_IMAGE_FREE, ga);
	C1(UI_IMAGE_FREE, 0);
}

static void free_images(void)
{
	for (int i = 0; i < nimg; i++)
		C1(UI_IMAGE_FREE, imgs[i]);
	nimg = 0;
}

/* ---- widgets */

static void wv(const char *what, long w) /* valeur et texte d'un widget */
{
	ps("  ");
	ps(what);
	kw("w", w);
	kv("val", C1(UI_W_GET_VALUE, w));
	long t = C1(UI_W_GET_TEXT, w);
	kv("tlen", C1(UI_W_TEXT_LEN, w));
	ks("texte", (const char *)t);
	nl();
}

static void group_begin(const char *what)
{
	ps("-- ");
	ps(what);
	nl();
	clear(tok(TJ_FACE));
	nwt = 0;
	lastx = lasty = -100;
	reset_w = addw("reset", C6(UI_LABEL, 300, 226, 18, 12, (long)"r", 0));
	reset_x = 305;
	reset_y = 230;
}

static void draw_all(const char *what)
{
	clear(tok(TJ_FACE));
	C1(UI_W_DRAW, 0);
	ps(" ui_w_draw ");
	ps(what);
	kx("sum", shash());
	nl();
}

static void tabs_walk(int n)
{
	for (int i = 0; i < n; i++)
		key(KEY_TAB, 9, 0);
	key(KEY_TAB, 9, M_SHIFT);
	ps("  focus");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
}

static void test_w_buttons(void)
{
	group_begin("boutons, choix");
	long lb = addw("label", C6(UI_LABEL, 10, 40, 140, 20, (long)"Zqlab etiquette", 0));
	long lb2 = addw("label2", C6(UI_LABEL, 160, 40, 150, 34, (long)"Zqlab coupee en lignes etroites", ULB_WRAP | ULB_DIM));
	long lb3 = addw("label3", C6(UI_LABEL, 160, 76, 150, 18, (long)"Titre", ULB_TITLE | ULB_CENTER));
	long b1 = addw("bouton", C6(UI_PUSHBUTTON, 10, 64, 0, 0, (long)"Zqok", 0));
	long b2 = addw("bouton2", C6(UI_PUSHBUTTON, 100, 64, 50, 0, (long)"Zqdef", UB_DEFAULT));
	long b3 = addw("bouton3", C6(UI_PUSHBUTTON, 10, 100, 70, 26, (long)"Zqgh", UB_GHOST));
	long cb = addw("case", C6(UI_CHECKBOX, 90, 100, 0, 0, (long)"Zqcase", 0));
	long r1 = addw("radio1", C6(UI_RADIO, 10, 130, 0, 0, (long)"Zqr1", 1));
	long r2 = addw("radio2", C6(UI_RADIO, 90, 130, 0, 0, (long)"Zqr2", 1));
	long sw = addw("switch", C6(UI_SWITCH, 170, 100, 0, 0, (long)"Zqsw", 1));
	long dd = addw("dropdown", C5(UI_DROPDOWN, 10, 160, 130, 0, (long)"Zqun|Zqdeux|Zqtrois"));
	long co = addw("combobox", C5(UI_COMBOBOX, 150, 160, 150, 0, (long)"Zqa|Zqbb|Zqccc"));
	long sg = addw("segmented", C5(UI_SEGMENTED, 10, 196, 0, 0, (long)"Zqg|Zqm|Zqd"));
	long sg2 = addw("segmented2", C5(UI_SEGMENTED, 170, 196, 120, 24, (long)"A|B|C|D"));
	rect_of(b1);
	rect_of(cb);
	rect_of(r1);
	rect_of(sg);
	nl();
	C3(UI_W_ON, b1, (long)cb_notify, 11);
	C3(UI_W_ON, cb, (long)cb_notify, 22);
	C3(UI_W_ON, dd, (long)cb_notify, 33);
	draw_all("boutons");
	C2(UI_W_SET_VALUE, cb, 1);
	C2(UI_W_SET_VALUE, r2, 1);
	C2(UI_W_SET_VALUE, dd, 2);
	C2(UI_W_SET_VALUE, sg, 1);
	C2(UI_W_SET_TEXT, lb, (long)"Zqnouveau texte");
	C2(UI_W_SET_TEXT, b3, (long)"Zqlong libelle");
	C2(UI_W_ENABLE, b2, 0);
	C2(UI_W_SHOW, lb3, 0);
	C2(UI_W_STYLE, lb2, ULB_BOLD | ULB_RIGHT | ULB_WRAP);
	C5(UI_W_MOVE, sg2, 170, 200, 140, 20);
	C2(UI_W_TIP, b1, (long)"Zqbulle");
	C2(UI_W_CURSOR, b1, UC_HAND);
	C2(UI_W_CURSOR, b1, -1);
	draw_all("apres set_value, set_text, enable, show, style, move");
	for (int i = 1; i < nwt; i++) {
		char what[16] = "w#";
		what[2] = (char)('0' + i / 10);
		what[3] = (char)('0' + i % 10);
		what[4] = 0;
		wv(what, wt[i]);
	}
	click(30, 74);
	click(125, 74); /* grisé */
	C2(UI_W_ENABLE, b2, 1);
	click(125, 74);
	click(100, 110);
	click(100, 110);
	click(20, 140);
	click(100, 140);
	click(190, 110);
	click(40, 206);
	click(80, 206);
	click(250, 210);
	move(30, 170);
	move(200, 50);
	move(200, 170);
	move(5, 230);
	wv("case", cb);
	wv("radio1", r1);
	wv("radio2", r2);
	wv("switch", sw);
	wv("segmented", sg);
	wv("segmented2", sg2);
	/* clavier : focus, Tab, flèches, Espace */
	C1(UI_W_FOCUS, dd);
	ps("  focus");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
	key(KEY_DOWN, 0, 0);
	key(KEY_DOWN, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	wv("dropdown", dd);
	C1(UI_W_FOCUS, cb);
	key(KEY_SPACE, ' ', 0);
	key(KEY_ENTER, 13, 0);
	wv("case", cb);
	C1(UI_W_FOCUS, b1);
	key(KEY_SPACE, ' ', 0);
	key(KEY_ENTER, 13, 0);
	key(KEY_ESC, 27, 0);
	C1(UI_W_FOCUS, sg);
	key(KEY_RIGHT, 0, 0);
	key(KEY_RIGHT, 0, 0);
	key(KEY_RIGHT, 0, 0);
	key(KEY_LEFT, 0, 0);
	wv("segmented", sg);
	C1(UI_W_FOCUS, sw);
	key(KEY_SPACE, ' ', 0);
	wv("switch", sw);
	C1(UI_W_FOCUS, co);
	ps("  focus combobox");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
	type("Zx");
	key(KEY_DOWN, 0, 0);
	wv("combobox", co);
	C2(UI_W_SET_TEXT, co, (long)"Zqlibre");
	wv("combobox", co);
	tabs_walk(6);
	ev_set(WEV_FOCUS, 0, 0, 0, 0, 0, 0);
	wev("focus perdu");
	ev_set(WEV_FOCUS, 1, 0, 0, 0, 0, 0);
	wev("focus rendu");
	draw_all("fin");
	ps("  user");
	kv("b1", C1(UI_W_USER, b1));
	kv("cb", C1(UI_W_USER, cb));
	kv("lb", C1(UI_W_USER, lb));
	kv("fenetre", C1(UI_W_WINDOW, b1) == V.win);
	nl();
	destroy_all();
	ps("  focus apres destruction");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
}

static const char code_asm[] = "; essai\n\tmov\tx0, #1\t// un\nboucle:\tadd\tx1, x1, x0\n\tb.ne\tboucle\n"
			       "\tadr\tx2, texte\ntexte:\tdb \"chaine\", 0\n";
static const char code_bas[] = "10 PRINT \"Bonjour\"\n20 FOR I = 1 TO 10 ' boucle\n30 NEXT I\nREM fin\n40 GOTO 10\n";
static const char code_cs[] = "public class Essai {\n  // commentaire\n  public void Run() { if (x > 1) return; }\n"
			      "  string s = \"texte\"; /* bloc */\n}\n";
static const char code_xml[] = "<?xml version=\"1.0\"?>\n<racine a=\"1\">\n  <!-- note -->\n  <b>texte &amp; x</b>\n"
			       "</racine>\n";

static void caret(const char *what, long w)
{
	R4 r = R1(UI_TEXTAREA_CARET, w);
	R4 s = R1(UI_TEXTBOX_SELECTION, w);
	ps("  ");
	ps(what);
	kv("ligne", r.r0);
	kv("col", r.r1);
	kv("sel0", s.r0);
	kv("sel1", s.r1);
	kv("tlen", C1(UI_W_TEXT_LEN, w));
	kx("texte", fnv((const void *)C1(UI_W_GET_TEXT, w), C1(UI_W_TEXT_LEN, w)));
	nl();
}

static void test_w_text(void)
{
	group_begin("champs de texte");
	long t1 = addw("textbox", C5(UI_TEXTBOX, 10, 40, 140, 0, 0));
	long t2 = addw("textbox mdp", C5(UI_TEXTBOX, 160, 40, 150, 0, UTF_PASSWORD));
	long t3 = addw("textbox lecture", C5(UI_TEXTBOX, 10, 80, 140, 0, UTF_READONLY | UTF_RIGHT));
	long t4 = addw("textbox mono", C5(UI_TEXTBOX, 160, 80, 150, 30, UTF_MONO));
	C2(UI_TEXTBOX_HINT, t1, (long)"Zqindice");
	C2(UI_W_SET_TEXT, t3, (long)"12345,67");
	C2(UI_W_SET_TEXT, t4, (long)"mono 0123");
	C3(UI_W_ON, t1, (long)cb_notify, 7);
	draw_all("champs");
	C1(UI_W_FOCUS, t1);
	type("Bonjour le monde");
	caret("t1", t1);
	key(KEY_HOME, 0, 0);
	key(KEY_RIGHT, 0, M_SHIFT);
	key(KEY_RIGHT, 0, M_SHIFT);
	key(KEY_RIGHT, 0, M_CTRL);
	key(KEY_RIGHT, 0, M_CTRL | M_SHIFT);
	caret("t1 selection", t1);
	key(KEY_BACKSPACE, 8, 0);
	key(KEY_END, 0, 0);
	key(KEY_LEFT, 0, M_CTRL);
	key(KEY_DELETE, 0, 0);
	key('a' - 'a' + 4, 'a', M_CTRL);
	caret("t1 tout", t1);
	type("x");
	caret("t1 remplace", t1);
	C3(UI_TEXTBOX_SELECT, t1, 0, -1);
	caret("t1 select 0,-1", t1);
	C3(UI_TEXTBOX_SELECT, t1, 1, 0);
	caret("t1 select 1,0", t1);
	C2(UI_W_SET_TEXT, t1, (long)"un texte bien plus long que le champ ne peut montrer");
	key(KEY_END, 0, 0);
	caret("t1 fin", t1);
	click(20, 50);
	caret("t1 clic", t1);
	drag(30, 50, 90, 52);
	caret("t1 glisse", t1);
	dblclick(60, 50);
	caret("t1 double-clic", t1);
	C1(UI_W_FOCUS, t2);
	type("secret");
	caret("t2", t2);
	C1(UI_W_FOCUS, t3);
	type("z");
	key(KEY_BACKSPACE, 8, 0);
	caret("t3 lecture seule", t3);
	C2(UI_W_STYLE, t3, UTF_ERROR);
	draw_all("champs remplis");
	tabs_walk(4);
	destroy_all();

	group_begin("texte multiligne");
	long a1 = addw("textarea", C5(UI_TEXTAREA, 5, 40, 310, 180, UTA_CODE | UTA_LINENUM | UTA_VSCROLL | UTA_HSCROLL));
	C2(UI_W_SET_TEXT, a1, (long)code_asm);
	draw_all("asm");
	caret("a1", a1);
	C2(UI_TEXTAREA_HIGHLIGHT, a1, 2);
	draw_all("ligne eclairee");
	C2(UI_TEXTAREA_HIGHLIGHT, a1, -1);
	C2(UI_TEXTAREA_GOTO, a1, 30);
	caret("goto 30", a1);
	ps("  find:");
	kv("mov", C2(UI_TEXTAREA_FIND, a1, (long)"MOV"));
	kv("absent", C2(UI_TEXTAREA_FIND, a1, (long)"zzzz"));
	kv("texte", C2(UI_TEXTAREA_FIND, a1, (long)"texte"));
	nl();
	caret("apres find", a1);
	ps("  insert");
	kv("r", C2(UI_TEXTAREA_INSERT, a1, (long)"[insere]\nligne\t2"));
	nl();
	caret("apres insert", a1);
	C1(UI_W_FOCUS, a1);
	type("abc\ndef");
	key(KEY_TAB, 9, 0);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_END, 0, M_SHIFT);
	key(KEY_PGDN, 0, 0);
	key(KEY_HOME, 0, M_CTRL);
	key(KEY_DOWN, 0, M_SHIFT);
	caret("touches", a1);
	ps("  edit");
	kv("undo", C2(UI_TEXTAREA_EDIT, a1, UTE_UNDO));
	kv("undo2", C2(UI_TEXTAREA_EDIT, a1, UTE_UNDO));
	nl();
	caret("undo", a1);
	ps("  edit");
	kv("redo", C2(UI_TEXTAREA_EDIT, a1, UTE_REDO));
	nl();
	caret("redo", a1);
	key(0x1D, 0, M_CTRL);
	key(0x1C, 0, M_CTRL);
	ps("  edit");
	kv("all", C2(UI_TEXTAREA_EDIT, a1, UTE_ALL));
	nl();
	caret("all", a1);
	click(100, 100);
	caret("clic", a1);
	drag(60, 60, 200, 130);
	caret("glisse", a1);
	wheel(150, 120, 2);
	wheel(150, 120, -1);
	draw_all("apres edition");
	C2(UI_TEXTAREA_FONT, a1, 18);
	draw_all("police 18");
	C2(UI_TEXTAREA_FONT, a1, 0);
	C3(UI_TEXTAREA_PAPER, a1, 0x00FFF8E0, 0x00402000);
	draw_all("papier");
	C3(UI_TEXTAREA_PAPER, a1, -1, 0);
	C2(UI_TEXTAREA_KEYWORDS, a1, (long)cb_keyword);
	C2(UI_W_STYLE, a1, UTA_CODE | UTA_BASIC | UTA_LINENUM);
	C2(UI_W_SET_TEXT, a1, (long)code_bas);
	cb_reset();
	draw_all("BASIC");
	cb_report("mots-cles BASIC");
	C2(UI_W_STYLE, a1, UTA_CODE | UTA_CS | UTA_VSCROLL);
	C2(UI_W_SET_TEXT, a1, (long)code_cs);
	cb_reset();
	draw_all("C#");
	cb_report("mots-cles C#");
	C2(UI_TEXTAREA_KEYWORDS, a1, 0);
	C2(UI_W_STYLE, a1, UTA_CODE | UTA_XML | UTA_LINENUM);
	C2(UI_W_SET_TEXT, a1, (long)code_xml);
	draw_all("XML");
	C2(UI_W_STYLE, a1, UTF_MONO | UTA_TABS | UTA_FORMTAB);
	C2(UI_W_SET_TEXT, a1, (long)"sans code\tmono\nligne 2");
	draw_all("mono");
	C1(UI_W_FOCUS, a1);
	key(KEY_TAB, 9, 0);
	ps("  focus");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
	C2(UI_W_STYLE, a1, UTF_READONLY);
	C1(UI_W_FOCUS, a1);
	type("q");
	caret("lecture seule", a1);
	ps("  edit lecture");
	kv("undo", C2(UI_TEXTAREA_EDIT, a1, UTE_UNDO));
	nl();
	destroy_all();
}

static void test_w_values(void)
{
	group_begin("valeurs");
	long sl = addw("slider", C7(UI_SLIDER, 10, 40, 140, 24, 0, 100, 30));
	long sl2 = addw("slider v", C7(UI_SLIDER, 290, 40, 24, 120, -50, 50, 0));
	long pg = addw("progress", C6(UI_PROGRESS, 160, 44, 120, 12, 200, 75));
	long nu = addw("numeric", C7(UI_NUMERIC, 10, 72, 100, 0, -5, 5, 2));
	long kn = addw("knob", C7(UI_KNOB, 120, 66, 48, 48, 0, 127, 64));
	long vu = addw("vumeter", C5(UI_VUMETER, 180, 66, 100, 14, 100));
	long vu2 = addw("vumeter v", C5(UI_VUMETER, 180, 86, 14, 60, 1000));
	long lc = addw("lcd", C5(UI_LCD, 10, 120, 160, 40, (long)"-12:34.5"));
	long sv = addw("scroll v", C5(UI_SCROLLBAR, 270, 40, 12, 160, 0));
	long sh = addw("scroll h", C5(UI_SCROLLBAR, 10, 206, 250, 12, USB_HORIZONTAL));
	long sp = addw("splitter", C6(UI_SPLITTER, 200, 120, 6, 80, 180, 260));
	C5(UI_W_SET_RANGE, sv, 0, 500, 100, 10);
	C5(UI_W_SET_RANGE, sh, 0, 50, 49, 1);
	C2(UI_W_SET_VALUE, sv, 120);
	C2(UI_VUMETER_LEVEL, vu, 70);
	C2(UI_VUMETER_LEVEL, vu, 40);
	C2(UI_VUMETER_LEVEL, vu2, 999);
	C2(UI_VUMETER_LEVEL, vu2, 1200);
	draw_all("valeurs");
	for (int i = 1; i < nwt; i++) {
		char what[8] = "w#";
		what[2] = (char)('0' + i / 10);
		what[3] = (char)('0' + i % 10);
		what[4] = 0;
		wv(what, wt[i]);
	}
	click(120, 52);
	drag(40, 52, 140, 52);
	drag(140, 52, -40, 52);
	drag(301, 100, 301, 45);
	wheel(80, 52, 3);
	wheel(80, 52, -1);
	click(95, 80);
	click(20, 80);
	wheel(60, 82, 2);
	drag(144, 90, 144, 60);
	wheel(144, 90, -5);
	click(276, 190);
	drag(276, 80, 276, 150);
	wheel(276, 100, 4);
	click(250, 212);
	drag(203, 160, 240, 160);
	drag(203, 160, 300, 160);
	wv("slider", sl);
	wv("slider v", sl2);
	wv("numeric", nu);
	wv("knob", kn);
	wv("scroll v", sv);
	wv("scroll h", sh);
	wv("splitter", sp);
	C1(UI_W_FOCUS, sl);
	key(KEY_RIGHT, 0, 0);
	key(KEY_RIGHT, 0, 0);
	key(KEY_END, 0, 0);
	key(KEY_PGDN, 0, 0);
	key(KEY_HOME, 0, 0);
	key(KEY_LEFT, 0, 0);
	wv("slider", sl);
	C1(UI_W_FOCUS, nu);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_UP, 0, 0);
	type("3");
	key(KEY_DOWN, 0, 0);
	wv("numeric", nu);
	C1(UI_W_FOCUS, kn);
	key(KEY_UP, 0, 0);
	key(KEY_PGUP, 0, 0);
	wv("knob", kn);
	C2(UI_W_SET_VALUE, pg, 250);
	C2(UI_W_SET_VALUE, sl, -10);
	C2(UI_W_SET_TEXT, lc, (long)"88:88");
	C5(UI_W_SET_RANGE, sl, 10, 20, 0, 5);
	wv("slider borne", sl);
	draw_all("fin");
	tabs_walk(5);
	destroy_all();
}

static void test_w_containers(void)
{
	group_begin("conteneurs");
	long pn = addw("panel", C5(UI_PANEL, 5, 40, 150, 90, UPN_SURFACE));
	long gb = addw("groupbox", C5(UI_GROUPBOX, 160, 40, 155, 90, (long)"Zqgroupe"));
	long c1 = addw("enfant1", C6(UI_PUSHBUTTON, 0, 0, 0, 0, (long)"Zqa", 0));
	long c2 = addw("enfant2", C6(UI_CHECKBOX, 0, 0, 0, 0, (long)"Zqb", 0));
	long c3 = addw("enfant3", C6(UI_LABEL, 0, 0, 60, 18, (long)"Zqc", 0));
	long g1 = addw("genfant1", C6(UI_PUSHBUTTON, 0, 0, 60, 0, (long)"Zq1", 0));
	long g2 = addw("genfant2", C6(UI_PUSHBUTTON, 0, 0, 60, 0, (long)"Zq2", 0));
	long g3 = addw("genfant3", C6(UI_SWITCH, 0, 0, 0, 0, (long)"Zq3", 0));
	C2(UI_W_SET_PARENT, c1, pn);
	C2(UI_W_SET_PARENT, c2, pn);
	C2(UI_W_SET_PARENT, c3, pn);
	C2(UI_W_SET_PARENT, g1, gb);
	C2(UI_W_SET_PARENT, g2, gb);
	C2(UI_W_SET_PARENT, g3, gb);
	ps("  layout");
	kv("v", C4(UI_LAYOUT, pn, ULY_VSTACK, 4, 0) != 0);
	kv("g", C4(UI_LAYOUT, gb, ULY_GRID, 6, 2) != 0);
	kv("nul", C4(UI_LAYOUT, 0, ULY_GRID, 6, 2));
	rect_of(c1);
	rect_of(c2);
	rect_of(c3);
	rect_of(g1);
	rect_of(g2);
	rect_of(g3);
	nl();
	draw_all("panneaux");
	C4(UI_LAYOUT, pn, ULY_HSTACK, 2, 0);
	ps("  layout h");
	rect_of(c1);
	rect_of(c2);
	rect_of(c3);
	nl();
	click(30, 60);
	C2(UI_W_SHOW, pn, 0);
	draw_all("panneau cache");
	click(30, 60);
	C2(UI_W_SHOW, pn, 1);
	C2(UI_W_ENABLE, gb, 0);
	draw_all("groupe grise");
	click(190, 70);
	C2(UI_W_ENABLE, gb, 1);
	C2(UI_W_SET_PARENT, c3, 0);
	C5(UI_W_MOVE, c3, 5, 132, 60, 16);
	draw_all("enfant sorti");
	destroy_all();

	group_begin("onglets");
	long tb = addw("tabs", C5(UI_TABS, 5, 40, 310, 100, (long)"Zqun|Zqdeux|Zqtrois"));
	long p0 = C2(UI_TABS_PAGE, tb, 0);
	long p1 = C2(UI_TABS_PAGE, tb, 1);
	ps("  pages");
	kv("p0", p0 != 0);
	kv("p1", p1 != 0);
	kv("p3", C2(UI_TABS_PAGE, tb, 3));
	nl();
	long q0 = addw("page0 bouton", C6(UI_PUSHBUTTON, 20, 90, 0, 0, (long)"Zqp0", 0));
	long q1 = addw("page1 case", C6(UI_CHECKBOX, 20, 90, 0, 0, (long)"Zqp1", 0));
	C2(UI_W_SET_PARENT, q0, p0);
	C2(UI_W_SET_PARENT, q1, p1);
	long ts = addw("tabstrip", C5(UI_TABSTRIP, 5, 146, 310, 0, UTS_CLOSE | UTS_PLUS));
	ps("  tabstrip_add");
	kv("a", C3(UI_TABSTRIP_ADD, ts, (long)"Zqdoc1", 100));
	kv("b", C3(UI_TABSTRIP_ADD, ts, (long)"Zqdocument deux", 200));
	kv("c", C3(UI_TABSTRIP_ADD, ts, (long)"Zq3", 300));
	nl();
	rect_of(ts);
	nl();
	long bar = addw("toolbar", C4(UI_TOOLBAR, 5, 190, 310, 0));
	long t1 = addw("outil1", C4(UI_TOOLBAR_ADD, bar, (long)"A", 0, 0));
	long t2 = addw("outil2", C4(UI_TOOLBAR_ADD, bar, (long)"Bx", (long)"Zqlib", UTB_TOGGLE));
	long t3 = addw("outil3", C4(UI_TOOLBAR_ADD, bar, 0, (long)"Zqseul", UTB_SEP));
	long t4 = addw("outil4", C4(UI_TOOLBAR_ADD, bar, (long)"\xC3\xA9", 0, UTB_TOGGLE | UTB_SEP));
	rect_of(t1);
	rect_of(t2);
	rect_of(t3);
	rect_of(t4);
	nl();
	draw_all("onglets");
	wv("tabs", tb);
	click(120, 50);
	wv("tabs", tb);
	click(220, 50);
	wv("tabs", tb);
	click(40, 100);
	click(30, 50);
	click(40, 100);
	C1(UI_W_FOCUS, tb);
	key(KEY_RIGHT, 0, 0);
	key(KEY_RIGHT, 0, 0);
	key(KEY_RIGHT, 0, 0);
	key(KEY_LEFT, 0, 0);
	wv("tabs", tb);
	C2(UI_W_SET_VALUE, tb, 0);
	draw_all("onglet 0");
	long tx = *(const int32_t *)(ts + UW_X), ty = *(const int32_t *)(ts + UW_Y);
	long th = *(const int32_t *)(ts + UW_H);
	for (long x = tx + 10; x < tx + 300; x += 23)
		click(x, ty + th / 2);
	wv("tabstrip", ts);
	ps("  closing");
	kv("r", C1(UI_TABSTRIP_CLOSING, ts));
	nl();
	C2(UI_W_SET_VALUE, ts, 1);
	wv("tabstrip", ts);
	long bx = *(const int32_t *)(t2 + UW_X) + 5, by = *(const int32_t *)(t2 + UW_Y) + 5;
	click(bx, by);
	click(bx + 9, by);
	wv("outil2", t2);
	click(*(const int32_t *)(t1 + UW_X) + 5, by);
	click(*(const int32_t *)(t4 + UW_X) + 5, by);
	wv("outil4", t4);
	C2(UI_W_STYLE, bar, 1);
	draw_all("fin");
	destroy_all();
}

static void test_w_pick(void)
{
	group_begin("image, palette, couleurs, date");
	long img = 0;
	{
		long n = bmp_make(imgbuf, 9, 7, 24, 0);
		img = C2(UI_IMAGE_DECODE, imgbuf, n);
	}
	long pal = addw("palette", C5(UI_PALETTE, 10, 40, 0, 0, 0x002563D8));
	long ib = addw("imagebox", C6(UI_IMAGEBOX, 195, 40, 60, 40, img, 0));
	long ib2 = addw("imagebox stretch", C6(UI_IMAGEBOX, 260, 40, 55, 40, img, UIM_STRETCH));
	long ib3 = addw("imagebox noscale", C6(UI_IMAGEBOX, 195, 90, 30, 20, img, UIM_NOSCALE));
	long cp = addw("colorpicker", C5(UI_COLORPICKER, 195, 160, 0, 0, 0x00D93A3F));
	long dp = addw("datepicker", C5(UI_DATEPICKER, 10, 170, 0, 0, (2024L << 16) | (2 << 8) | 29));
	rect_of(pal);
	rect_of(cp);
	rect_of(dp);
	nl();
	C2(UI_DATEPICKER_TODAY, dp, (2024L << 16) | (3 << 8) | 1);
	draw_all("palette, images, couleurs");
	wv("palette", pal);
	wv("colorpicker", cp);
	wv("datepicker", dp);
	/* (ni la pastille ni le champ de date : leur clic ouvre un dialogue) */
	for (long y = 45; y < 150; y += 33)
		for (long x = 15; x < 186; x += 37)
			click(x, y);
	wv("palette", pal);
	C1(UI_W_FOCUS, pal);
	key(KEY_RIGHT, 0, 0);
	key(KEY_DOWN, 0, 0);
	key(KEY_LEFT, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_END, 0, 0);
	wv("palette", pal);
	C2(UI_W_SET_VALUE, cp, 0x0012AB34);
	C2(UI_W_SET_VALUE, pal, 0x00FFFFFF);
	C2(UI_W_SET_VALUE, dp, (2000L << 16) | (1 << 8) | 1);
	wv("colorpicker", cp);
	wv("datepicker", dp);
	long img2 = keep_img(C2(UI_IMAGE_DECODE, png_pal, png_pal_end - png_pal));
	C2(UI_IMAGEBOX_SET, ib, img2);
	C2(UI_IMAGEBOX_SET, ib3, 0);
	C5(UI_W_MOVE, ib2, 260, 40, 55, 70);
	draw_all("fin");
	tabs_walk(6);
	destroy_all();
	C1(UI_IMAGE_FREE, img);

	group_begin("calendrier");
	long cal = addw("calendar", C5(UI_CALENDAR, 40, 40, 0, 196, (2026L << 16) | (11 << 8) | 12));
	rect_of(cal);
	nl();
	C2(UI_CALENDAR_TODAY, cal, (2026L << 16) | (11 << 8) | 3);
	draw_all("calendrier");
	wv("calendar", cal);
	for (long y = 40 + 52 + 12; y < 236; y += 24)
		for (long x = 40 + 17; x < 278; x += 51)
			click(x, y);
	wv("calendar", cal);
	click(55, 55);
	wv("calendar mois precedent", cal);
	click(263, 55);
	click(263, 64);
	wv("calendar mois suivant", cal);
	C1(UI_W_FOCUS, cal);
	key(KEY_RIGHT, 0, 0);
	key(KEY_DOWN, 0, 0);
	key(KEY_PGDN, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_PGUP, 0, 0);
	key(KEY_LEFT, 0, 0);
	key(KEY_HOME, 0, 0);
	key(KEY_END, 0, 0);
	wv("calendar", cal);
	C2(UI_CALENDAR_SHOW, cal, (2025L << 16) | (1 << 8) | 31);
	wv("calendar show", cal);
	draw_all("calendrier janvier 2025");
	C2(UI_CALENDAR_SHOW, cal, (2024L << 16) | (2 << 8) | 29);
	C2(UI_CALENDAR_TODAY, cal, 0);
	draw_all("calendrier fevrier 2024");
	C2(UI_CALENDAR_SHOW, cal, (2023L << 16) | (12 << 8) | 31);
	draw_all("calendrier decembre 2023");
	destroy_all();
}

static void list_dump(const char *what, long l)
{
	long n = C1(UI_LIST_COUNT, l);
	R4 so = R1(UI_LIST_SORTED, l);
	ps("  ");
	ps(what);
	kv("n", n);
	kv("val", C1(UI_W_GET_VALUE, l));
	kv("tri", so.r0);
	kv("dec", so.r1);
	nl();
	for (long i = 0; i < n && i < 12; i++) {
		R4 r = R2(UI_LIST_GET, l, i);
		ps("   ");
		pd(i);
		pc(' ');
		pz((const char *)r.r0);
		kv("d", r.r1);
		nl();
	}
	R4 r = R2(UI_LIST_GET, l, n + 5);
	ps("   hors");
	kv("t", r.r0 != 0);
	nl();
}

static void test_w_lists(void)
{
	group_begin("listes");
	long l = addw("list", C5(UI_LIST, 5, 40, 200, 120, UL_HEADER | UL_STRIPES | UL_GRID));
	ps("  colonnes");
	kv("c0", C4(UI_LIST_COLUMN, l, (long)"Zqnom", 90, 0));
	kv("c1", C4(UI_LIST_COLUMN, l, (long)"Zqtaille", 50, ULC_RIGHT));
	kv("c2", C4(UI_LIST_COLUMN, l, (long)"Zqtype", 60, 0));
	nl();
	static const char *const rows[] = {"delta\t40\tz", "alpha\t1000\ta", "Charlie\t3\tm", "bravo\t20\tm",
					   "\xC3\xA9" "cho\t-5\tb",      "foxtrot\t20\ta", "golf\t7\tz",  "hotel\t0\tq",
					   "india\t12\tk",     "juliet\t9\tk"};
	ps("  add:");
	for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
		kv("", C3(UI_LIST_ADD, l, (long)rows[i], 100 + (long)i));
	nl();
	C3(UI_W_ON, l, (long)cb_notify, 44);
	draw_all("liste");
	list_dump("liste", l);
	C3(UI_LIST_SORT, l, 0, 0);
	list_dump("tri nom", l);
	C3(UI_LIST_SORT, l, 1, 1);
	list_dump("tri taille dec", l);
	C3(UI_LIST_SORT, l, 2, 0);
	list_dump("tri type", l);
	draw_all("triee");
	click(30, 48);
	list_dump("clic en-tete 0", l);
	click(30, 48 + 30);
	click(120, 48);
	list_dump("clic en-tete 1", l);
	click(124, 48);
	list_dump("clic en-tete 1 bis", l);
	click(40, 90);
	click(40, 110);
	dblclick(40, 130);
	rclick(40, 70);
	list_dump("clics", l);
	C1(UI_W_FOCUS, l);
	key(KEY_DOWN, 0, 0);
	key(KEY_DOWN, 0, 0);
	key(KEY_END, 0, 0);
	key(KEY_PGUP, 0, 0);
	key(KEY_HOME, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_ENTER, 13, 0);
	type("g");
	wv("list", l);
	wheel(60, 100, 2);
	wheel(60, 100, -9);
	drag(203, 60, 203, 140);
	C3(UI_LIST_SET, l, 2, (long)"remplace\t1\tr");
	C2(UI_LIST_REMOVE, l, 0);
	C2(UI_LIST_REMOVE, l, 50);
	list_dump("set, remove", l);
	C2(UI_W_STYLE, l, UL_HEADER | UL_NOSORT | UL_HSCROLL);
	click(30, 48);
	list_dump("nosort", l);
	C2(UI_W_STYLE, l, UL_HEADER | UL_USERSORT);
	click(124, 48);
	list_dump("usersort", l);
	cb_reset();
	C2(UI_LIST_CELL, l, (long)cb_cell);
	draw_all("cellules");
	cb_report("cellules");
	C2(UI_LIST_CELL, l, 0);
	C1(UI_LIST_CLEAR, l);
	list_dump("clear", l);
	draw_all("vide");

	long lv = addw("list virtuelle", C5(UI_LIST, 210, 40, 105, 180, UL_HEADER));
	C4(UI_LIST_COLUMN, lv, (long)"Zqv", 50, 0);
	C4(UI_LIST_COLUMN, lv, (long)"Zqw", 30, 0);
	C4(UI_LIST_COLUMN, lv, (long)"Zqx", 20, ULC_RIGHT);
	cb_reset();
	C3(UI_LIST_VIRTUAL, lv, 1000, (long)cb_vrow);
	draw_all("virtuelle");
	cb_report("virtuelle");
	cb_reset();
	click(250, 100);
	C1(UI_W_FOCUS, lv);
	key(KEY_PGDN, 0, 0);
	key(KEY_END, 0, 0);
	click(240, 48);
	cb_report("virtuelle apres clics");
	list_dump("virtuelle", lv);
	cb_reset();
	C3(UI_LIST_VIRTUAL, lv, 3, (long)cb_vrow);
	draw_all("virtuelle 3");
	cb_report("virtuelle 3");

	long side = addw("list sidebar", C5(UI_LIST, 5, 165, 200, 70, UL_SIDEBAR));
	C3(UI_LIST_ADD, side, (long)"#Zqsection", 0);
	C3(UI_LIST_ADD, side, (long)"Zqun", 1);
	C3(UI_LIST_ADD, side, (long)"Zqdeux", 2);
	C3(UI_LIST_ADD, side, (long)"#Zqautre", 0);
	C3(UI_LIST_ADD, side, (long)"Zqtrois", 3);
	draw_all("sidebar");
	click(40, 190);
	click(40, 205);
	wv("sidebar", side);
	destroy_all();

	group_begin("arbre");
	long t = addw("tree", C4(UI_TREE, 5, 40, 200, 180));
	long n0 = C4(UI_TREE_ADD, t, -1, (long)"Zqracine", 10);
	long n1 = C4(UI_TREE_ADD, t, n0, (long)"Zqenfant 1", 11);
	long n2 = C4(UI_TREE_ADD, t, n0, (long)"Zqenfant 2", 12);
	long n3 = C4(UI_TREE_ADD, t, n1, (long)"Zqpetit-enfant", 13);
	long n4 = C4(UI_TREE_ADD, t, -1, (long)"Zqautre racine", 20);
	long n5 = C4(UI_TREE_ADD, t, n4, (long)"Zqfeuille", 21);
	long n6 = C4(UI_TREE_ADD, t, 999, (long)"Zqparent inconnu", 30);
	ps("  ids");
	kv("", n0);
	kv("", n1);
	kv("", n2);
	kv("", n3);
	kv("", n4);
	kv("", n5);
	kv("", n6);
	nl();
	C3(UI_W_ON, t, (long)cb_notify, 55);
	draw_all("arbre replie");
	C3(UI_TREE_EXPAND, t, n0, 1);
	C3(UI_TREE_EXPAND, t, n1, 1);
	draw_all("arbre deplie");
	ps("  get:");
	for (long i = -1; i < 9; i++) {
		R4 r = R2(UI_TREE_GET, t, i);
		pc(' ');
		pz((const char *)r.r0);
		pc('/');
		pd(r.r1);
	}
	nl();
	wv("tree", t);
	click(30, 50);
	click(15, 50);
	click(60, 70);
	click(60, 90);
	dblclick(60, 110);
	wv("tree", t);
	C1(UI_W_FOCUS, t);
	key(KEY_DOWN, 0, 0);
	key(KEY_DOWN, 0, 0);
	key(KEY_RIGHT, 0, 0);
	key(KEY_LEFT, 0, 0);
	key(KEY_LEFT, 0, 0);
	key(KEY_UP, 0, 0);
	key(KEY_END, 0, 0);
	key(KEY_HOME, 0, 0);
	key(KEY_ENTER, 13, 0);
	wv("tree", t);
	wheel(60, 100, 1);
	C3(UI_TREE_EXPAND, t, n0, 0);
	draw_all("arbre replie a nouveau");
	C2(UI_W_SET_VALUE, t, n5);
	draw_all("valeur n5");
	C1(UI_TREE_CLEAR, t);
	draw_all("arbre vide");
	R4 r = R2(UI_TREE_GET, t, n0);
	ps("  apres clear");
	kv("t", r.r0 != 0);
	kv("d", r.r1);
	nl();
	destroy_all();
}

static void test_w_canvas(void)
{
	group_begin("canevas, vues");
	long cv = addw("canvas", C4(UI_CANVAS, 5, 40, 100, 80));
	R4 p = R1(UI_CANVAS_PIXELS, cv);
	ps("  canvas_pixels");
	kv("l", p.r1);
	kv("h", p.r2);
	if (p.r0)
		kx("pix", fnv((const void *)p.r0, p.r1 * p.r2 * 4));
	nl();
	C1(UI_CANVAS_BEGIN, cv);
	ps("  dans le canevas");
	kv("w", V.w);
	kv("h", V.h);
	nl();
	C5(UI_FILL, 0, 0, 100, 80, 0x00102040);
	C4(UI_VP_DISC, 50 * 16, 40 * 16, 30 * 16, 0x00F0C030);
	C7(UI_TEXT, (long)"Cv", 10, 30, 0x00FFFFFF, 20, FB, 0);
	C5(UI_FILL, 90, 70, 40, 40, 0x00FF0000);
	C0(UI_CANVAS_END);
	ps("  apres");
	kv("w", V.w);
	kv("h", V.h);
	kv("surf", V.surf == surface);
	nl();
	p = R1(UI_CANVAS_PIXELS, cv);
	ps("  canvas_pixels");
	if (p.r0)
		kx("pix", fnv((const void *)p.r0, p.r1 * p.r2 * 4));
	nl();
	long vw = addw("view", C5(UI_VIEW, 110, 40, 120, 80, 0));
	long vf = addw("view focus", C5(UI_VIEW, 235, 40, 80, 80, UVW_FOCUS));
	C5(UI_VIEW_ON, vw, (long)cb_paint, (long)cb_vmouse, (long)cb_vkey, (long)cb_vwheel);
	C5(UI_VIEW_ON, vf, (long)cb_paint, (long)cb_vmouse, (long)cb_vkey, 0);
	ps("  view_on nul");
	kv("r", C5(UI_VIEW_ON, 0, 0, 0, 0, 0));
	nl();
	draw_all("canevas, vues");
	C0(UI_VIEW_FLUSH);
	sum(" view_flush (rien)");
	C1(UI_VIEW_DIRTY, vw);
	C0(UI_VIEW_FLUSH);
	sum(" view_flush");
	click(20, 60);
	click(150, 70);
	drag(130, 50, 200, 100);
	wheel(150, 70, -2);
	click(260, 60);
	key(KEY_ENTER, 13, 0);
	key(KEY_ESC, 27, 0);
	key(0x04, 'a', M_CTRL | M_ALT);
	key(KEY_TAB, 9, 0);
	ps("  focus");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
	C5(UI_W_MOVE, vw, 110, 130, 60, 30);
	draw_all("vue deplacee");
	C2(UI_W_SHOW, vf, 0);
	C1(UI_VIEW_DIRTY, vf);
	C0(UI_VIEW_FLUSH);
	sum(" vue cachee");
	destroy_all();
	ps("  focus");
	kw("w", C0(UI_W_GET_FOCUS));
	nl();
}

static void test_w_design(void)
{
	group_begin("ancres");
	long a = addw("a", C6(UI_PUSHBUTTON, 10, 40, 80, 0, (long)"Zqa", 0));
	long b = addw("b", C6(UI_PUSHBUTTON, 230, 40, 80, 0, (long)"Zqb", 0));
	long c = addw("c", C5(UI_TEXTBOX, 10, 80, 300, 0, 0));
	long d = addw("d", C6(UI_LABEL, 140, 200, 40, 20, (long)"Zqd", 0));
	C2(UI_DESIGN_SIZE, 320, 240);
	ps("  anchor");
	kv("a", C2(UI_ANCHOR, a, UA_LEFT | UA_TOP));
	kv("b", C2(UI_ANCHOR, b, UA_RIGHT | UA_TOP));
	kv("c", C2(UI_ANCHOR, c, UA_LEFT | UA_RIGHT | UA_TOP));
	kv("d", C2(UI_ANCHOR, d, 0));
	kv("nul", C2(UI_ANCHOR, 0, UA_FILL));
	nl();
	draw_all("ancres");
	ps(" ");
	rect_of(a);
	rect_of(b);
	rect_of(c);
	rect_of(d);
	nl();
	destroy_all();
	C2(UI_DESIGN_SIZE, 0, 0);
	C2(UI_DIALOG_AT, 0, 0);
}

/* ---- partie -f : fichiers de /, vraie fenêtre */

static char abuf[256];

static void test_files_window(void)
{
	ps("=== Pi (fichiers, fenetre)\n");
	long r = C1(UI_INIT, (long)&V);
	use_mem_surface();
	ps("ui_init");
	kv("r", r);
	nl();
	theme_dump("theme du fichier");
	ps("ui_theme_text(0)");
	kv("r", C1(UI_THEME_TEXTE, 0) != 0);
	nl();
	theme_copy("theme = Givre\nechelle = M\n");
	static const long sizes[] = {9, 11, 13, 14, 16, 20, 28, 40};
	const long fonts[3] = {FS, FB, FM};
	for (int f = 0; f < 3; f++) {
		clear(0x00FFFFFF);
		long y = 0;
		uint32_t wh = 2166136261u;
		for (unsigned si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
			y += sizes[si] + 2;
			wh = fnv_mix(wh, (uint64_t)C7(UI_TEXT, (long)txts[5], 2, y, 0, sizes[si], fonts[f], 0));
			wh = fnv_mix(wh, (uint64_t)C7(UI_TEXT, (long)txts[2], 160, y, 0, sizes[si], fonts[f], 0));
		}
		ps("vraies polices");
		kv("police", f);
		kx("largs", wh);
		kx("sum", shash());
		nl();
	}
	ps("ui_font_get:");
	for (int i = 0; i < 11; i++) {
		long f = C1(UI_FONT_GET, i);
		pc(' ');
		if (f == FS)
			ps("sans");
		else if (f == FB)
			ps("gras");
		else if (f == FM)
			ps("mono");
		else
			ps("autre");
		pc('/');
		pd(f ? C3(UI_TEXT_WIDTH, (long)"Police Wxyz", 16, f) : -1);
	}
	long it = C0(UI_FONT_ITALIC);
	kv("italique=get3", it == C1(UI_FONT_GET, 3));
	nl();
	clear(0x00FFFFFF);
	for (int i = 0; i < 10; i++) {
		long f = C1(UI_FONT_GET, i);
		C7(UI_TEXT, (long)"Fonte 0123 \xC3\xA9", 4, 18 + i * 22, 0, 18, f, 0);
	}
	sum("ui_font_get dessins");
	ps("ui_load_font:");
	kv("absent", C2(UI_LOAD_FONT, "/fonts/absent.ttf", font4));
	kv("dejavu", C2(UI_LOAD_FONT, "/fonts/DejaVuSans.ttf", font4));
	kv("larg", C3(UI_TEXT_WIDTH, (long)"DejaVu Wxyz", 20, (long)font4));
	nl();
	ps("ui_lang");
	ks("l", (const char *)C0(UI_LANG));
	ks("tr_file", (const char *)C1(UI_TR, "File"));
	ks("tr_zq", (const char *)C1(UI_TR, "Zqwx"));
	nl();

	clear(0x00F5F6F8);
	static const char *const apps[] = {"files", "editor", "terminal", "calculator", "clock", "zzz-absent", "a"};
	long x = 4;
	for (unsigned i = 0; i < sizeof apps / sizeof apps[0]; i++) {
		C4(UI_ICONE, apps[i], x, 4, 40);
		C4(UI_ICONE, apps[i], x, 50, 24);
		C4(UI_ICONE, apps[i], x, 80, 16);
		x += 44;
	}
	sum("ui_icon");
	clear(0x00F5F6F8);
	static const char *const glyphs[] = {"glyph-gear", "glyph-power", "glyph-lock", "glyph-tb-plus",
					     "glyph-tb-cut", "glyph-apps", "glyph-uitest-absent"};
	static const long gs[] = {16, 18, 24, 40};
	for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
		for (unsigned k = 0; k < 4; k++)
			C5(UI_GLYPH, glyphs[i], 4 + (long)i * 44, 4 + (long)k * 44, gs[k], 0x0014171C);
	sum("ui_glyph (fichiers)");
	static const char *const pngs[] = {"/apps/help.app/help/window.png", "/apps/files.app/help/main.png",
					   "/apps/files.app/icon.rgba", "/absent.png"};
	for (unsigned i = 0; i < 4; i++) {
		long img = img_report(pngs[i], C1(UI_IMAGE_LOAD, pngs[i]));
		if (img) {
			clear(0x00808080);
			C5(UI_IMAGE_DRAW, img, 0, 0, 320, 240);
			sum("  dessin 320x240");
			C1(UI_IMAGE_FREE, img);
		}
	}
	static const char *const assoc[] = {"a.txt", "/media/sd1/b.png", "c.bas", "d.zzz", "/apps", "e.md", "f.pdf",
					    "g"};
	ps("ui_assoc_app:\n");
	for (unsigned i = 0; i < sizeof assoc / sizeof assoc[0]; i++) {
		memset(abuf, 0, sizeof abuf);
		long a = C3(UI_ASSOC_APP, assoc[i], abuf, sizeof abuf);
		ps(" ");
		pz(assoc[i]);
		kv("r", a);
		ks("app", abuf);
		nl();
	}
	long scr[4] = {0, 0, 0, 0};
	r = C1(UI_SCREEN, scr);
	ps("ui_screen");
	kv("r", r);
	kv("l", scr[0]);
	kv("h", scr[1]);
	kv("bas", scr[2]);
	kv("haut", scr[3]);
	nl();

	/* une vraie fenêtre */
	long win = C3(UI_WINDOW, 320, 240, (long)"uitest");
	ps("ui_window");
	kv("ok", win > 0);
	kv("var", V.win == win);
	kv("w", V.w);
	kv("h", V.h);
	kv("n", C0(UI_WINDOW_COUNT));
	nl();
	if (win <= 0)
		return;
	C1(UI_TITLE, "uitest : fenetre");
	nwt = 0;
	reset_w = addw("reset", C6(UI_LABEL, 300, 226, 18, 12, (long)"r", 0));
	reset_x = 305;
	reset_y = 230;
	lastx = lasty = -100;
	long b1 = addw("bouton", C6(UI_PUSHBUTTON, 10, 40, 0, 0, (long)"Zqok", 0));
	long cb = addw("case", C6(UI_CHECKBOX, 120, 40, 0, 0, (long)"Zqcase", 0));
	long tb = addw("textbox", C5(UI_TEXTBOX, 10, 80, 200, 0, 0));
	long sl = addw("slider", C7(UI_SLIDER, 10, 120, 200, 24, 0, 10, 5));
	long l = addw("list", C5(UI_LIST, 10, 150, 200, 70, UL_HEADER));
	long bar = addw("toolbar", C4(UI_TOOLBAR, 220, 40, 95, 0));
	long t1 = addw("outil +", C4(UI_TOOLBAR_ADD, bar, (long)"+", 0, 0));
	addw("outil coupe", C4(UI_TOOLBAR_ADD, bar, (long)"\xE2\x9C\x82", 0, UTB_TOGGLE));
	C4(UI_LIST_COLUMN, l, (long)"Zqa", 100, 0);
	C3(UI_LIST_ADD, l, (long)"un\t1", 1);
	C3(UI_LIST_ADD, l, (long)"deux\t2", 2);
	C5(UI_FILL, 0, 0, V.w, V.h, tok(TJ_FACE));
	C1(UI_W_DRAW, 0);
	sum("fenetre widgets");
	C0(UI_UPDATE_ALL);
	click(30, 50);
	click(130, 50);
	C1(UI_W_FOCUS, tb);
	type("Fenetre");
	click(100, 132);
	click(*(const int32_t *)(t1 + UW_X) + 5, *(const int32_t *)(t1 + UW_Y) + 5);
	click(40, 180);
	wv("case", cb);
	wv("textbox", tb);
	wv("slider", sl);
	wv("list", l);
	kw("bouton", b1);
	ps(" w_window");
	kv("=win", C1(UI_W_WINDOW, b1) == win);
	nl();
	C1(UI_MENU, "MZqf\nI1\tZqa\tCtrl+N\n");
	C2(UI_MENU_STATE, 1, UMS_CHECKED);
	ev_set(WEV_KEY, 1, 0, 0, 0x11, 0, M_CTRL);
	ps("ui_menu_key");
	kv("r", C1(UI_MENU_KEY, 0));
	nl();
	C1(UI_MENU, 0);

	/* surface attachée */
	long sf = C5(UI_SURFACE, 220, 100, 64, 48, 0);
	ps("ui_surface");
	kv("ok", sf != 0);
	if (sf) {
		R4 p = R1(UI_SURFACE_PIXELS, sf);
		kv("pas", p.r1);
		kv("l", p.r2);
		kv("h", p.r3);
		if (p.r0)
			for (long y = 0; y < p.r3; y++)
				for (long xx = 0; xx < p.r2; xx++)
					((uint32_t *)(p.r0 + y * p.r1))[xx] = (uint32_t)(xx * 4 << 16 | y * 5 << 8 | 0x40);
		kv("damage", C5(UI_SURFACE_DAMAGE, sf, 0, 0, 0, 0) >= 0);
		static const uint16_t dl[8] = {0, 0, 10, 10, 20, 20, 5, 5};
		kv("damage_list", C3(UI_SURFACE_DAMAGE_LIST, sf, dl, 2) >= 0);
		kv("move", C5(UI_SURFACE_MOVE, sf, 230, 110, 64, 48));
		kv("source", C4(UI_SURFACE_SOURCE, sf, -1, 0, 0));
		kv("free", C1(UI_SURFACE_FREE, sf) >= 0);
	}
	nl();
	ps("ui_frame_request");
	kv("r", C0(UI_FRAME_REQUEST));
	nl();
	static uint32_t grab[16 * 16];
	ps("ui_screen_grab");
	kv("r", C3(UI_SCREEN_GRAB, grab, 16, 16));
	nl();
	C1(UI_POINTER_LOCK, 0);

	/* deuxième fenêtre, ui_window_use, ui_close */
	long w2 = C6(UI_WINDOW_EX, 40, 40, 200, 120, (long)"uitest 2", 0);
	ps("ui_window_ex");
	kv("ok", w2 > 0);
	kv("courante", V.win == w2);
	kv("w", V.w);
	kv("h", V.h);
	kv("n", C0(UI_WINDOW_COUNT));
	nl();
	if (w2 > 0) {
		long lb = addw("label 2", C6(UI_LABEL, 10, 40, 150, 20, (long)"Zqdeuxieme", 0));
		C5(UI_FILL, 0, 0, V.w, V.h, tok(TJ_FACE));
		C1(UI_W_DRAW, 0);
		sum("fenetre 2");
		long prev = C1(UI_WINDOW_USE, win);
		ps("ui_window_use");
		kv("avant=w2", prev == w2);
		kv("courante=win", V.win == win);
		kv("w", V.w);
		kv("label=w2", C1(UI_W_WINDOW, lb) == w2);
		nl();
		/* un événement de la fenêtre 2 : ui_w_event y va tout seul */
		ev_set(WEV_MOUSE, 0, 50, 50, 0, 0, 0);
		((int32_t *)V.ev)[EV_WIN / 4] = (int32_t)w2;
		wev("bouge (fenetre 2)");
		ps(" courante=win");
		kv("", V.win == win);
		nl();
		C1(UI_WINDOW_USE, w2);
		C0(UI_CLOSE);
		ps("ui_close");
		kv("n", C0(UI_WINDOW_COUNT));
		kv("courante=win", V.win == win);
		nl();
		nwt--; /* (ui_close laisse les widgets de la fenêtre : le label, dernier de wt) */
		C1(UI_W_DESTROY, lb);
	}

	/* nouvelle taille : ancres, rappel */
	C2(UI_DESIGN_SIZE, 320, 240);
	C2(UI_ANCHOR, sl, UA_LEFT | UA_RIGHT | UA_TOP);
	C2(UI_ANCHOR, l, UA_FILL);
	C2(UI_ANCHOR, bar, UA_RIGHT | UA_TOP);
	C1(UI_ON_RESIZE, (long)cb_resize);
	C2(UI_MIN_SIZE, 200, 150);
	ev_set(WEV_RESIZE, 0, 360, 270, 0, 0, 0);
	wev("nouvelle taille 360x270");
	ps(" apres");
	kv("w", V.w);
	kv("h", V.h);
	rect_of(sl);
	rect_of(l);
	rect_of(bar);
	nl();
	sum("fenetre agrandie");
	ps("ui_resize");
	kv("r", C2(UI_RESIZE, 100, 50) >= 0);
	kv("w", V.w);
	kv("h", V.h);
	nl();
	C1(UI_ON_RESIZE, 0);
	destroy_all();
	r = C1(UI_WINDOW_CLOSE, 0);
	ps("ui_window_close");
	kv("r", r);
	kv("n", C0(UI_WINDOW_COUNT));
	nl();
}

/* les fonctions appelées seulement par -f */
static int files_only(int i)
{
	switch (i) {
	case UI_INIT:
	case UI_LOAD_FONT:
	case UI_WINDOW:
	case UI_TITLE:
	case UI_CLOSE:
	case UI_SCREEN:
	case UI_WINDOW_EX:
	case UI_ICONE:
	case UI_RESIZE:
	case UI_ON_RESIZE:
	case UI_IMAGE_LOAD:
	case UI_LANG:
	case UI_FONT_ITALIC:
	case UI_FONT_GET:
	case UI_SURFACE:
	case UI_SURFACE_PIXELS:
	case UI_SURFACE_DAMAGE:
	case UI_SURFACE_DAMAGE_LIST:
	case UI_SURFACE_MOVE:
	case UI_SURFACE_FREE:
	case UI_SURFACE_SOURCE:
	case UI_FRAME_REQUEST:
	case UI_SCREEN_GRAB:
	case UI_POINTER_LOCK:
	case UI_ASSOC_APP:
		return 1;
	}
	return 0;
}

static int has_flag(const char *line, char f)
{
	if (!line)
		return 0;
	for (long i = 0; line[i]; i++)
		if (line[i] == '-' && line[i + 1] == f)
			return 1;
	return 0;
}

static void never(int want_files)
{
	for (int i = 0; i < UI_COUNT_FN; i++)
		if (!called[i] && (want_files || !files_only(i))) {
			ps("jamais appelee : ");
			ps(fn_names[i]);
			nl();
		}
}

static void run(const char *line)
{
	ps("uitest : UIKit\n");
	long a = sys2(SYS_LIB_OPEN, "appkit", 26);
	if (a >= 0) {
		const uint32_t *at = (const uint32_t *)a;
		f_print = (fn_print)(a + at[2 + AK_F_PRINT]);
	}
	kit = open_kit("uikit", UI_COUNT_FN);
	if (kit < 0) {
		ps("uikit absent : ");
		pd(kit);
		nl();
		flush();
		sys6(SYS_EXIT, 127, 0, 0, 0, 0, 0);
	}
	const uint32_t *t = (const uint32_t *)kit;
	kit_count = t[1];
	ps("table magic=");
	px(t[0], 8);
	ps(" count=");
	pd(kit_count);
	nl();
	if (t[0] != KIT_MAGIC || kit_count < UI_COUNT_FN) {
		ps("table invalide\n");
		flush();
		sys6(SYS_EXIT, 126, 0, 0, 0, 0, 0);
	}
	int files = has_flag(line, 'f');
	dbg_text = has_flag(line, 't');
	if (dbg_text) { /* l'état flottant du process (diagnostic) */
		unsigned long fs = 0;
#if defined(__aarch64__)
		__asm__ volatile("mrs %0, fpcr" : "=r"(fs));
		ps("fpcr=");
#else
		unsigned int m = 0;
		__asm__ volatile("stmxcsr %0" : "=m"(m));
		fs = m;
		ps("mxcsr=");
#endif
		px(fs, 8);
		nl();
	}
	no_cache = has_flag(line, 'n');

	/* ui_init (Pi : vraies polices ; PC : échec), puis la surface et les
	 * polices de uitest */
	use_mem_surface();
	C1(UI_INIT, (long)&V);
	use_mem_surface();
	fonts_embedded();

	test_theme();
	set_theme("theme = Givre\nechelle = M\n");
	test_basic();
	test_text();
	test_vector();
	test_misc();
	test_images();
	ps("== widgets (Givre)\n");
	test_w_buttons();
	test_w_text();
	test_w_values();
	test_w_containers();
	test_w_pick();
	test_w_lists();
	test_w_canvas();
	test_w_design();
	set_theme("theme = Onyx Noir\nechelle = P\naccent = fuchsia\n");
	ps("== widgets (Onyx Noir, P)\n");
	test_w_buttons();
	test_w_values();
	test_w_lists();
	set_theme("theme = Givre\nechelle = L\ndensity = touch\n");
	ps("== widgets (Givre, L, tactile)\n");
	test_w_buttons();
	test_w_text();
	set_theme("theme = Givre\nechelle = M\n");
	free_images();

	never(0);
	ps("fin : ");
	pd(checks);
	ps(" controles, ");
	pd(fails);
	ps(" echecs, ");
	pd(calls);
	ps(" appels\n");
	if (files) {
		test_files_window();
		never(1);
		ps("fin -f : ");
		pd(checks);
		ps(" controles, ");
		pd(fails);
		ps(" echecs\n");
	}
	flush();
	sys6(SYS_EXIT, fails > 125 ? 125 : fails, 0, 0, 0, 0, 0);
}

#if defined(__x86_64__)
__attribute__((force_align_arg_pointer, noreturn))
#else
__attribute__((noreturn))
#endif
void _start(const char *line) /* x0, rdi : la ligne d'arguments (0 : aucune) */
{
	run(line);
	for (;;)
		;
}
