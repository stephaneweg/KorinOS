/* uk.h -- UIKit en C (commun au Pi et au PC) : en-tête interne du kit.
 *
 * Port ligne à ligne de l'assembleur (ttf.asm, uikit.asm, uikit_*.inc) : mêmes
 * calculs, mêmes pixels, mêmes bizarreries (signalées « comme l'asm »).
 *
 * Conventions du port (src/kits/uikit/PORT.md) :
 *  - les fonctions de la table (ui_*) et les fonctions internes appelées d'un
 *    module à l'autre (uk_*, ttf_*) prennent des « long » (un registre x de
 *    l'asm chacun) et rendent un long ; celles qui rendent plusieurs registres
 *    rendent un ukr (r0..r3 = x0..x3) ;
 *  - les blocs du kit (widgets UW_*, fenêtres WB_*, images, polices F_*) sont
 *    lus et écrits par décalages, comme l'asm : LDX / LDW / LDSW / STX...
 *    (ldr w = zéro-étendu, ldrsw = signe-étendu : ne pas les confondre) ;
 *  - les variables de l'asm (segment inscriptible d'uikit.asm) sont les champs
 *    de la structure « uk », dans le même ordre ;
 *  - aucun pointeur dans des données initialisées (le chargeur de kits ne
 *    fait pas de relocations) : tables de décalages, pas de tables de
 *    pointeurs ni de chaînes ; les répartitions par type passent par switch.
 */
#ifndef UK_H
#define UK_H

#include "korin_kit.h"
#include "appkit_import.h"
#include "uk_consts.h"

/* Aucun registre flottant ni SIMD (les appelants en assembleur comptent sur
 * eux, l'asm n'y touchait pas) : sauf uk_ttf.c (UK_FP, calcul flottant de
 * ttf.asm), compilé à part (fp_files de kit.conf). */
#if defined(__aarch64__) && !defined(UK_FP)
#pragma GCC target("general-regs-only")
#endif

typedef unsigned char u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;

/* plusieurs registres rendus (x0..x3) */
typedef struct {
	long r0, r1, r2, r3;
} ukr;

/* ---- accès par décalage (non alignés permis, sans règle d'alias) -------- */

typedef u64 __attribute__((aligned(1), may_alias)) uk_u64u;
typedef s32 __attribute__((aligned(1), may_alias)) uk_s32u;
typedef u32 __attribute__((aligned(1), may_alias)) uk_u32u;
typedef u16 __attribute__((aligned(1), may_alias)) uk_u16u;
typedef s16 __attribute__((aligned(1), may_alias)) uk_s16u;

#define UKP(p, o) ((u8 *)(uintptr_t)(p) + (long)(o))
/* ldr x / ldr w (zéro-étendu) / ldrsw / ldrh / ldrsh / ldrb */
#define LDX(p, o) ((long)*(uk_u64u *)UKP(p, o))
#define LDW(p, o) ((long)*(uk_u32u *)UKP(p, o))
#define LDSW(p, o) ((long)*(uk_s32u *)UKP(p, o))
#define LDH(p, o) ((long)*(uk_u16u *)UKP(p, o))
#define LDSH(p, o) ((long)*(uk_s16u *)UKP(p, o))
#define LDB(p, o) ((long)*UKP(p, o))
#define LDSB(p, o) ((long)*(signed char *)UKP(p, o))
/* str x / str w / strh / strb */
#define STX(p, o, v) (*(uk_u64u *)UKP(p, o) = (u64)(long)(v))
#define STW(p, o, v) (*(uk_u32u *)UKP(p, o) = (u32)(long)(v))
#define STH(p, o, v) (*(uk_u16u *)UKP(p, o) = (u16)(long)(v))
#define STB(p, o, v) (*UKP(p, o) = (u8)(long)(v))

/* ---- arithmétique comme AArch64 ------------------------------------------ */

/* udiv / sdiv : division par 0 -> 0 ; sdiv(MIN, -1) -> MIN */
static inline long UDIV(long a, long b)
{
	return b ? (long)((u64)a / (u64)b) : 0;
}
static inline long SDIV(long a, long b)
{
	if (!b)
		return 0;
	if (b == -1)
		return (long)(0 - (u64)a);
	return a / b;
}
/* versions 32 bits (udiv w, sdiv w) : résultat zéro-étendu, comme un registre w */
static inline long UDIVW(long a, long b)
{
	return (u32)b ? (long)((u32)a / (u32)b) : 0;
}
static inline long SDIVW(long a, long b)
{
	s32 x = (s32)a, y = (s32)b;
	if (!y)
		return 0;
	if (y == -1)
		return (long)(u32)(0 - (u32)x);
	return (long)(u32)(x / y);
}
/* msub après udiv / sdiv : reste (b = 0 : a) */
static inline long UREM(long a, long b)
{
	return b ? (long)((u64)a % (u64)b) : a;
}
static inline long SREM(long a, long b)
{
	return a - SDIV(a, b) * b;
}
/* décalages par registre : la quantité est prise modulo 64 (32 pour w) */
#define LSL(a, n) ((long)((u64)(long)(a) << ((n) & 63)))
#define LSR(a, n) ((long)((u64)(long)(a) >> ((n) & 63)))
#define ASR(a, n) ((long)(a) >> ((n) & 63))
#define LSLW(a, n) ((long)(u32)((u32)(a) << ((n) & 31)))
#define LSRW(a, n) ((long)(u32)((u32)(a) >> ((n) & 31)))
#define ASRW(a, n) ((long)(u32)((s32)(a) >> ((n) & 31)))
/* comparaisons non signées (lo, ls, hi, hs) */
#define ULT(a, b) ((u64)(long)(a) < (u64)(long)(b))
#define ULE(a, b) ((u64)(long)(a) <= (u64)(long)(b))
#define UGT(a, b) ((u64)(long)(a) > (u64)(long)(b))
#define UGE(a, b) ((u64)(long)(a) >= (u64)(long)(b))
/* sxtw, uxtw, sxth... */
#define SXTW(a) ((long)(s32)(a))
#define UXTW(a) ((long)(u32)(a))
#define SXTH(a) ((long)(s16)(a))

/* ---- appels système (comme sys SYS_xxx de l'asm : svc 0 direct) --------- */

/* Comme la macro sys de l'asm avec FILES_APPKIT (défini par appkit.abi, que
 * le kit importe) : les appels de fichiers (SYS_OPEN..SYS_SPAWN_AS), SYS_PRINT
 * et SYS_FORK vont au client de fichiers d'AppKit, file_sys (x8 = numéro) ;
 * le noyau du Pi les refuse (E_NOSYS). Les autres : svc 0 direct. */
#if defined(__aarch64__)
static inline long uk_fsys(long n, long a, long b, long c, long d, long e, long f)
{
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	register long x8 __asm__("x8") = n;
	__asm__ volatile("bl file_sys"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x8)
			 :
			 : "x6", "x7", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x30", "memory",
			   "cc");
	return x0;
}
#else
/* x86-64 : le numéro est le 7e argument (x8_arg d'AppKit) : le même relais
 * d'import, déclaré avec sept arguments */
KIT_HIDDEN long uk_file_sys7(long a, long b, long c, long d, long e, long f, long n) __asm__("file_sys");
static inline long uk_fsys(long n, long a, long b, long c, long d, long e, long f)
{
	return uk_file_sys7(a, b, c, d, e, f, n);
}
#endif
static inline long uk_sys(long n, long a, long b, long c, long d, long e, long f)
{
	if ((n >= SYS_OPEN && n <= SYS_SPAWN_AS) || n == SYS_PRINT || n == SYS_FORK)
		return uk_fsys(n, a, b, c, d, e, f);
	return korin_sys6(n, a, b, c, d, e, f);
}
#define SYS0(n) uk_sys((n), 0, 0, 0, 0, 0, 0)
#define SYS1(n, a) uk_sys((n), (long)(a), 0, 0, 0, 0, 0)
#define SYS2(n, a, b) uk_sys((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define SYS3(n, a, b, c) uk_sys((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define SYS4(n, a, b, c, d) uk_sys((n), (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define SYS6(n, a, b, c, d, e, f) uk_sys((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f))

/* ---- variables du kit : le segment inscriptible d'uikit.asm, dans l'ordre */

struct uk_state {
	long kits_ok;            /* uk_kits_ok : AppKit ouvert */
	long vars;               /* uk_vars : variables du programme (ui_init) */
	/* fenêtre courante : dmg .. flags contigus (WB_DMG, 56 octets) */
	long dmg;                /* uk_dmg : en-tête WD_* (0 : aucun) */
	long buf;                /* uk_buf : tampon partagé de la surface */
	long chr_a, chr_i;       /* uk_chr_a, uk_chr_i : cadres actif, inactif */
	long ow, oh;             /* uk_ow, uk_oh : taille extérieure */
	long flags;              /* uk_flags : rôle de la fenêtre */
	long save[3];            /* uk_save : surface de dessin mise de côté */
	u8 theme[TH_SIZE] __attribute__((aligned(8)));     /* uk_theme */
	long tv[UK_TV_N];                                   /* uk_tv : clés de theme.txt */
	u8 tokens[TJ_SIZE] __attribute__((aligned(8)));    /* uk_jetons */
	long scale;              /* uk_ech : échelle (-1 : pas calculée) */
	long theme_sub;          /* uk_theme_sub */
	u8 scr[32] __attribute__((aligned(8)));            /* uk_scr */
	u8 pev[32] __attribute__((aligned(8)));            /* uk_pev : événement d'un menu */
	u8 title[128] __attribute__((aligned(8)));         /* uk_title */
	long ic_t[3];            /* uk_ic_t : icône : taille, x, y */
	long ic_list;            /* uk_ic_list : cache des icônes */
	u8 ic_path[64] __attribute__((aligned(8)));        /* uk_ic_path */
	u8 ic_hdr[ICF_HDR] __attribute__((aligned(8)));    /* uk_ic_hdr */
	u8 ic_letter[8] __attribute__((aligned(8)));       /* uk_ic_lettre */
	long pop_rh;             /* uk_pop_rh : menu, hauteur d'une ligne */
	long pop_fl;             /* uk_pop_fl : menu, drapeaux */
	u8 pop_name[32] __attribute__((aligned(8)));       /* uk_pop_nom */
	long pop_x;              /* uk_pop_x */
	long pop_key;            /* uk_pop_rac : raccourci de la ligne */
	u8 pop_label[128] __attribute__((aligned(8)));     /* uk_pop_lib */
	long pop_click[2];       /* uk_pop_clic */
	long pp[12];             /* uk_pp : menu gardé ouvert */
	u8 msg[WS_MSG] __attribute__((aligned(8)));        /* uk_msg */
	/* widgets */
	long clip[4];            /* uk_clip : x0, y0, x1, y1 */
	long clip_on;            /* uk_clip_on */
	long save_clip;          /* uk_save_clip */
	long save_rect[5];       /* uk_save_rect */
	long clip_sp;            /* uk_clip_sp */
	long clip_stack[5 * 8];  /* uk_clip_stack */
	long free_heads[32];     /* uk_free_heads */
	long wfirst, wlast;      /* uk_wfirst, uk_wlast */
	long focus, capture, hover; /* contigus (WB_FOCUS) */
	long click[4];           /* uk_click : widget, temps, x, y */
	long held[3];            /* uk_held : widget, x, y */
	u8 ev_held[32] __attribute__((aligned(8)));        /* uk_ev_held */
	u8 ev_next[32] __attribute__((aligned(8)));        /* uk_ev_next */
	long replay;             /* uk_replay */
	long undo_busy;          /* uk_undo_busy */
	long winact, minw, minh, resize_fn; /* contigus (WB_ACT) */
	long dsg_w, dsg_h;       /* uk_dsg_w, uk_dsg_h */
	long cur_shape;          /* uk_cur_shape */
	long tip_win[2];         /* uk_tip_win */
	long mx[2];              /* uk_mx */
	long hover_t;            /* uk_hover_t */
	long menu_tr_d[2];       /* uk_menu_tr_d */
	u8 tr_buf[UK_TR_ITEMS] __attribute__((aligned(8))); /* uk_tr_tampon */
	long menu_src;           /* uk_menu_src */
	u8 font_st[16] __attribute__((aligned(8)));        /* uk_font_st */
	long mbtn;               /* uk_mbtn */
	long pop_place[3];       /* uk_pop_place */
	long dlg_role;           /* uk_dlg_role */
	/* fenêtres du programme */
	long cur;                /* uk_cur */
	long wins;               /* uk_wins */
	long menu_p;             /* uk_menu_p */
	long win_dlg;            /* uk_win_dlg */
	long mwin;               /* uk_mwin */
	long mt_ok;              /* uk_mt_ok */
	long mt[8];              /* uk_mt : métriques (MT_*) */
	u8 fbuf[256] __attribute__((aligned(8)));          /* uk_fbuf */
	u8 fitbuf[416] __attribute__((aligned(8)));        /* uk_fitbuf */
	u8 pwbuf[UK_PW_MAX] __attribute__((aligned(8)));   /* uk_pwbuf */
	u8 pastebuf[UK_PASTE_MAX] __attribute__((aligned(8))); /* uk_pastebuf */
	u8 chbuf[8] __attribute__((aligned(8)));           /* uk_chbuf */
	u8 numbuf[32] __attribute__((aligned(8)));         /* uk_numbuf */
	u8 linebuf[UK_LINE_MAX] __attribute__((aligned(8))); /* uk_linebuf */
	u8 dirent[DIRENT_BYTES] __attribute__((aligned(8))); /* uk_dirent */
	u8 fdline[UK_PATH_MAX] __attribute__((aligned(8)));
	u8 fdpath[UK_PATH_MAX] __attribute__((aligned(8)));
	u8 fdtmp[UK_PATH_MAX] __attribute__((aligned(8)));
	u8 fdtmp2[UK_PATH_MAX] __attribute__((aligned(8)));
	u8 fdname[256] __attribute__((aligned(8)));
	u8 fd_filter[8] __attribute__((aligned(8)));
	long fdstat[2];
	u8 inf[INF_SIZE] __attribute__((aligned(8)));      /* uk_inf : inflate (PNG) */
	u8 menu_st[UK_MENU_IDS * 16] __attribute__((aligned(8))); /* uk_menu_st */
	u8 menubuf[WS_MENU_MAX + 8] __attribute__((aligned(8)));  /* uk_menubuf */
	u8 fonts[(UK_NFONTS - 3) * UI_FONT_SIZE] __attribute__((aligned(8))); /* uk_fonts */
	u8 fallback[UI_FONT_SIZE] __attribute__((aligned(8))); /* uk_repli */
	long fallback_state;     /* uk_repli_etat */
	u8 popbuf[UK_POP_MAX] __attribute__((aligned(8))); /* uk_popbuf */
	u8 win0[WB_SIZE] __attribute__((aligned(8)));      /* uk_win0 */
	long snap[2];            /* uk_snap */
	u8 fa_ext_b[16] __attribute__((aligned(8)));       /* association des fichiers */
	u8 fa_name[UK_FA_NAME] __attribute__((aligned(8)));
	u8 fa_def[UK_FA_NAME] __attribute__((aligned(8)));
	long fa_tab, fa_tn, fa_row;
	u8 fa_ent[DIRENT_BYTES] __attribute__((aligned(8)));
	u8 fa_path[UK_PATH_MAX] __attribute__((aligned(8)));
	u8 fa_arg[UK_PATH_MAX * 2] __attribute__((aligned(8)));
	u8 fa_buf[UK_FA_BUF] __attribute__((aligned(8)));
};

KIT_HIDDEN extern struct uk_state uk;

/* adresse d'un champ, comme « adr x0,uk_xxx » */
#define UKA(f) ((long)(uintptr_t)&uk.f)
/* variables du programme (ui_data, UV_*) : uk.vars + décalage */
#define UV(o) (uk.vars + (o))
#define UVX(o) LDX(uk.vars, o)
/* jeton du thème (macro jet de l'asm : 32 bits, zéro-étendu) */
#define JET(t) LDW(uk.tokens, t)

/* ---- chaînes d'uikit.asm (uk_core.c), communes aux modules -------------- */

#define UK_STR(n) KIT_HIDDEN extern const char n[];
UK_STR(ui_path_sans) UK_STR(ui_path_bold) UK_STR(ui_path_sans2) UK_STR(ui_path_bold2) UK_STR(ui_path_mono)
UK_STR(uk_s_ellipsis) UK_STR(uk_s_up) UK_STR(uk_s_down) UK_STR(uk_empty) UK_STR(uk_s_cross) UK_STR(uk_s_space)
UK_STR(uk_s_minus) UK_STR(uk_s_plus) UK_STR(uk_s_tab) UK_STR(uk_s_check) UK_STR(uk_s_chev) UK_STR(uk_s_ok)
UK_STR(uk_s_cancel) UK_STR(uk_s_yes) UK_STR(uk_s_no) UK_STR(uk_s_open) UK_STR(uk_s_save) UK_STR(uk_s_folder)
UK_STR(uk_s_sd0) UK_STR(uk_s_upar) UK_STR(uk_s_left) UK_STR(uk_s_right) UK_STR(uk_s_date) UK_STR(uk_s_pickdate)
UK_STR(uk_s_color) UK_STR(uk_s_places) UK_STR(uk_s_volumes) UK_STR(uk_s_homel) UK_STR(uk_s_home) UK_STR(uk_s_sd0c)
UK_STR(uk_s_placesf) UK_STR(uk_s_vfs) UK_STR(uk_s_allfiles) UK_STR(uk_s_items) UK_STR(uk_s_name) UK_STR(uk_s_size)
UK_STR(uk_s_namec) UK_STR(uk_s_bopen) UK_STR(uk_s_bsave) UK_STR(uk_s_bchoose) UK_STR(uk_s_ko) UK_STR(uk_s_mo)
UK_STR(uk_s_go) UK_STR(uk_s_oct) UK_STR(uk_s_dir) UK_STR(uk_s_replace)
UK_STR(uk_menu_title) /* uikit_frame.inc (uk_frame.c) : « menu » */
#define S(n) ((long)(uintptr_t)(n))
long uk_conf_path(const char *name, long write); /* uk_core.c : réglages personnels */

/* ---- AppKit (importé : fmt_dec, str_cat, lang_tr, lang_get, lang_load) -- */
#define TR(x) lang_tr((long)(x))

/* ======================================================================== */
/* Fonctions internes appelées d'un module à l'autre (noms de l'asm).        */
/* Les fonctions de la table des types (VT_*) ont toutes ces signatures :    */
/*   draw (w), mouse (w, genre, x, y, clics) -> code, key (w, code HID,      */
/*   caractère, mods) -> code / -1, wheel (w, crans) -> code,                */
/*   child (parent, enfant, code) -> code, layout (w).                       */
/* ======================================================================== */

#define UK_VT_DRAW(n) KIT_HIDDEN long n(long w);
#define UK_VT_MOUSE(n) KIT_HIDDEN long n(long w, long kind, long x, long y, long clicks);
#define UK_VT_KEY(n) KIT_HIDDEN long n(long w, long code, long ch, long mods);
#define UK_VT_WHEEL(n) KIT_HIDDEN long n(long w, long steps);
#define UK_VT_CHILD(n) KIT_HIDDEN long n(long w, long child, long code);
#define UK_VT_LAYOUT(n) KIT_HIDDEN long n(long w);

/* ttf.asm (uk_ttf.c) */
KIT_HIDDEN long ttf_open(long font, long data, long len, long scratch, long scratch_len);
KIT_HIDDEN long ttf_set_size(long font, long px);
KIT_HIDDEN ukr ttf_utf8_next(long s);                    /* r0 point de code, r1 suite */
KIT_HIDDEN long ttf_text_width(long font, long s);
/* x4 (mode) : 0 tout, 1 ce qui tient dans x3 (ui_text_fit) -> r0 largeur, r1 octets */
KIT_HIDDEN ukr ttf_text_pos(long font, long s, long max, long x, long mode);
KIT_HIDDEN long ttf_draw_text(long font, long s, long surf, long pitch, long x, long y, long color, long clip);
KIT_HIDDEN long ttf_set_cache(long font, long buf, long size);

/* uikit.asm (uk_core.c) */
KIT_HIDDEN void uk_kits(void);
KIT_HIDDEN long uk_call(long op);
KIT_HIDDEN long uk_title_copy(long dst, long src);
KIT_HIDDEN long uk_win(void);
KIT_HIDDEN long uk_menu_set(long text);
KIT_HIDDEN long uk_malloc(long size);

/* uikit_widgets.inc (uk_widgets.c) */
KIT_HIDDEN long uk_alloc(long size);
KIT_HIDDEN long uk_free(long p);
KIT_HIDDEN long uk_alloc_size(long p);
KIT_HIDDEN long uk_strlen(long s);
KIT_HIDDEN long uk_memcpy(long d, long s, long n);       /* -> d + n (fin), comme l'asm */
KIT_HIDDEN long uk_memcpy_up(long d, long s, long n);
KIT_HIDDEN long uk_strdup(long s);
KIT_HIDDEN long uk_met(void);                            /* -> adresse de uk.mt */
KIT_HIDDEN long uk_font_r(void);
KIT_HIDDEN long uk_font_m(void);
KIT_HIDDEN long uk_text_box(long s, long x, long y, long h, long color, long font, long max, long px);
KIT_HIDDEN long uk_clip_push(long x, long y, long w, long h);
KIT_HIDDEN long uk_clip_pop(void);
KIT_HIDDEN long uk_vt_has(long w, long entry);           /* uk_vt_fn : 1 si le type a cette entrée */
KIT_HIDDEN long uk_vt_flags(long w);
KIT_HIDDEN long uk_w_new(long type, long x, long y, long w, long h);
KIT_HIDDEN long uk_w_shown(long w);
KIT_HIDDEN long uk_w_enabled(long w);
KIT_HIDDEN long uk_w_focused(long w);
KIT_HIDDEN long uk_w_at(long x, long y, long win);
KIT_HIDDEN long uk_w_paint(long w);
KIT_HIDDEN long uk_w_draw(long w);
KIT_HIDDEN long uk_update_clip(long x, long y, long w, long h);
KIT_HIDDEN long uk_w_refresh(long w);
KIT_HIDDEN long uk_w_clamp(long w);
KIT_HIDDEN long uk_w_mouse(long w, long kind, long x, long y, long clicks);
KIT_HIDDEN ukr uk_sb_geom(long w);                       /* r0 rail, r1 pouce, r2 décalage, r3 1 si utile */
UK_VT_DRAW(uk_sb_draw) UK_VT_MOUSE(uk_sb_mouse) UK_VT_WHEEL(uk_sb_wheel)

/* uikit_window.inc (uk_window.c) */
KIT_HIDDEN long uk_wb_new(void);
KIT_HIDDEN long uk_wb_drop(void);
KIT_HIDDEN long uk_wb_find(long id);
KIT_HIDDEN long uk_route(long win);
KIT_HIDDEN long uk_w_destroy_win(long win);
KIT_HIDDEN long uk_on_resize_ev(long ev);
KIT_HIDDEN long uk_des_note(long w, long always);
KIT_HIDDEN long uk_adapt(long w, long h);
UK_VT_DRAW(uk_sp_draw) UK_VT_MOUSE(uk_sp_mouse)

/* uikit_theme.inc, uikit_rmask.inc (uk_theme.c) */
KIT_HIDDEN long uk_theme_load(void);
KIT_HIDDEN long uk_theme_watch(void);
KIT_HIDDEN long uk_retheme(void);
KIT_HIDDEN long uk_str_eq(long a, long b);

/* uikit_frame.inc (uk_frame.c) */
KIT_HIDDEN long uk_target(long img, long w, long h);
KIT_HIDDEN long uk_target_back(void);
KIT_HIDDEN long uk_bold(void);
KIT_HIDDEN long uk_create(long x, long y, long w, long h, long title, long role);
KIT_HIDDEN long uk_draw_chrome(void);

/* uikit_list.inc (uk_list.c) */
KIT_HIDDEN long uk_ls_rows(long l);
KIT_HIDDEN long uk_ls_select(long l, long item);
KIT_HIDDEN long uk_field(long text, long col);
KIT_HIDDEN long uk_items_grow(long l);
KIT_HIDDEN long uk_items_clear(long w);
KIT_HIDDEN long uk_ls_sort_range(long l, long col, long desc, long first);
UK_VT_DRAW(uk_ls_draw) UK_VT_MOUSE(uk_ls_mouse) UK_VT_KEY(uk_ls_key) UK_VT_WHEEL(uk_ls_wheel)
UK_VT_CHILD(uk_ls_child) UK_VT_LAYOUT(uk_ls_layout)

/* uikit_tree.inc (uk_tree.c) */
KIT_HIDDEN long uk_tr_index(long t, long id);
KIT_HIDDEN long uk_tr_select(long t, long rank);
UK_VT_DRAW(uk_tr_draw) UK_VT_MOUSE(uk_tr_mouse) UK_VT_KEY(uk_tr_key) UK_VT_WHEEL(uk_tr_wheel)
UK_VT_CHILD(uk_tr_child) UK_VT_LAYOUT(uk_tr_layout)

/* uikit_choice.inc (uk_choice.c) */
KIT_HIDDEN ukr uk_win_pos(void);                         /* r0, r1 = coin du contenu à l'écran */
KIT_HIDDEN long uk_new_sized(long type, long x, long y, long w, long h, long text, long extra);
KIT_HIDDEN long uk_tg_toggle(long w);
KIT_HIDDEN long uk_items_split(long w, long items);
KIT_HIDDEN long uk_dd_current(long w);
UK_VT_DRAW(uk_tg_draw) UK_VT_MOUSE(uk_tg_mouse) UK_VT_KEY(uk_tg_key)
UK_VT_DRAW(uk_dd_draw) UK_VT_MOUSE(uk_dd_mouse) UK_VT_KEY(uk_dd_key)
UK_VT_CHILD(uk_cb_child) UK_VT_LAYOUT(uk_cb_layout)

/* uikit_text.inc, uikit_clip.inc (uk_text.c) */
KIT_HIDDEN long uk_ws_clip_set(long text, long len);
KIT_HIDDEN long uk_ws_clip_get(long buf, long size);
KIT_HIDDEN long uk_set_text(long w, long text, long len);
KIT_HIDDEN ukr uk_tb_font(long w);                       /* r0 police, r1 taille px */
KIT_HIDDEN long uk_tb_hit(long w, long x);
KIT_HIDDEN long uk_u8_next(long text, long off, long len);
KIT_HIDDEN long uk_word_edge(long text, long off, long len, long right);
KIT_HIDDEN long uk_tb_delete(long w, long start, long end);
KIT_HIDDEN long uk_tb_insert(long w, long text, long len);
UK_VT_DRAW(uk_tb_draw) UK_VT_MOUSE(uk_tb_mouse) UK_VT_KEY(uk_tb_key)

/* uikit_undo.inc (uk_undo.c) */
KIT_HIDDEN long uk_undo_rec(long w, long kind, long off, long len);
KIT_HIDDEN long uk_undo_rec_at(long w, long kind, long off, long len, long pos);
KIT_HIDDEN long uk_undo_clear(long w);
KIT_HIDDEN long uk_undo_step(long w, long redo);

/* uikit_area.inc (uk_area.c) */
KIT_HIDDEN ukr uk_ta_line(long w, long line);            /* r0 début, r1 fin */
KIT_HIDDEN long uk_ta_sync(long w);
KIT_HIDDEN long uk_ta_hit(long w, long x, long y);       /* (asm : x0, x2, x3) */
UK_VT_DRAW(uk_ta_draw) UK_VT_MOUSE(uk_ta_mouse) UK_VT_KEY(uk_ta_key) UK_VT_WHEEL(uk_ta_wheel)
UK_VT_CHILD(uk_ta_child) UK_VT_LAYOUT(uk_ta_layout)

/* uikit_lex.inc (uk_lex.c) */
KIT_HIDDEN long uk_lex_start(long w);
KIT_HIDDEN ukr uk_lex(long text, long pos, long end, long state, long style); /* r0 fin, r1 UKC_*, r2 état */
KIT_HIDDEN long uk_cs_word(long word, long len);

/* uikit_dialog.inc (uk_dialog.c) */
KIT_HIDDEN long uk_dlg_bar(long dlg);
KIT_HIDDEN long uk_dlg_buttons_fn(long dlg, long w, long code);
KIT_HIDDEN long uk_dialog_under(long w, long wd, long ht, long title);

/* uikit_controls.inc (uk_controls.c) */
UK_VT_DRAW(uk_bt_draw) UK_VT_MOUSE(uk_bt_mouse) UK_VT_KEY(uk_bt_key) UK_VT_DRAW(uk_lb_draw)

/* uikit_value.inc (uk_value.c) */
KIT_HIDDEN long uk_vertical(long w);
UK_VT_KEY(uk_val_key) UK_VT_WHEEL(uk_val_wheel)
UK_VT_DRAW(uk_sl_draw) UK_VT_MOUSE(uk_sl_mouse) UK_VT_DRAW(uk_pg_draw)
UK_VT_DRAW(uk_nm_draw) UK_VT_MOUSE(uk_nm_mouse) UK_VT_KEY(uk_nm_key)
UK_VT_DRAW(uk_kn_draw) UK_VT_MOUSE(uk_kn_mouse) UK_VT_DRAW(uk_vu_draw) UK_VT_DRAW(uk_lcd_draw)

/* uikit_tabs.inc (uk_tabs.c) */
KIT_HIDDEN long uk_seg_choose(long w, long seg);
UK_VT_DRAW(uk_pn_draw) UK_VT_DRAW(uk_sg_draw) UK_VT_MOUSE(uk_sg_mouse) UK_VT_KEY(uk_sg_key)
UK_VT_DRAW(uk_tabs_draw) UK_VT_DRAW(uk_ts_draw) UK_VT_MOUSE(uk_ts_mouse) UK_VT_WHEEL(uk_ts_wheel)
KIT_HIDDEN long uk_tbar_shift(long bar, long dx, long dy);
UK_VT_DRAW(uk_tbar_draw) UK_VT_DRAW(uk_tool_draw) UK_VT_MOUSE(uk_tool_mouse)

/* uikit_more.inc (uk_more.c) */
KIT_HIDDEN ukr uk_date_text(long date, long buf);        /* r0 date (inchangée), r1 fin (le NUL) */
UK_VT_DRAW(uk_cal_draw) UK_VT_MOUSE(uk_cal_mouse) UK_VT_KEY(uk_cal_key)
UK_VT_DRAW(uk_dp_draw) UK_VT_MOUSE(uk_dp_mouse) UK_VT_KEY(uk_dp_key)
UK_VT_DRAW(uk_pal_draw) UK_VT_MOUSE(uk_pal_mouse)
UK_VT_DRAW(uk_cp_draw) UK_VT_MOUSE(uk_cp_mouse) UK_VT_KEY(uk_cp_key)

/* uikit_misc.inc (uk_misc.c) */
KIT_HIDDEN long uk_w_shape(long w);
KIT_HIDDEN long uk_tip_hide(void);
KIT_HIDDEN ukr uk_on_drop(long ev);                      /* (asm : x19 = événement) r0 widget, r1 code */
KIT_HIDDEN long uk_tr_items(long items);                 /* (asm : x4 entre et sort) */

/* uikit_image.inc (uk_image.c) */
KIT_HIDDEN long uk_img_new(long w, long h);
UK_VT_DRAW(uk_im_draw)

/* uikit_extra.inc (uk_extra.c) */
UK_VT_DRAW(uk_cv_draw) UK_VT_MOUSE(uk_cv_mouse)

/* uikit_view.inc (uk_view.c) */
UK_VT_DRAW(uk_vw_draw) UK_VT_MOUSE(uk_vw_mouse) UK_VT_KEY(uk_vw_key) UK_VT_WHEEL(uk_vw_wheel)
UK_VT_LAYOUT(uk_vw_fit)

/* ======================================================================== */
/* Fonctions de la table (uikit.abi, même ordre) : chaque argument est un    */
/* registre x0..x7 ; ukr : plusieurs registres rendus (ret de kit.conf).     */
/* ======================================================================== */
#define UKF KIT_HIDDEN
UKF long ui_init(long a0);
UKF long ui_load_font(long a0, long a1);
UKF long ui_window(long a0, long a1, long a2);
UKF long ui_fill(long a0, long a1, long a2, long a3, long a4);
UKF long ui_frame(long a0, long a1, long a2, long a3, long a4);
UKF long ui_pixel(long a0, long a1, long a2);
UKF long ui_text(long a0, long a1, long a2, long a3, long a4, long a5, long a6);
UKF long ui_text_width(long a0, long a1, long a2);
UKF long ui_button(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_update(long a0, long a1, long a2, long a3);
UKF long ui_update_all(void);
UKF long ui_event(long a0);
UKF long ui_hit(long a0, long a1, long a2, long a3);
UKF long ui_title(long a0);
UKF long ui_close(void);
UKF long ui_screen(long a0);
UKF long ui_window_ex(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_theme(void);
UKF long ui_tone(long a0, long a1);
UKF long ui_mix(long a0, long a1, long a2);
UKF long ui_ink(long a0);
UKF long ui_rbox(long a0, long a1, long a2, long a3, long a4, long a5, long a6);
UKF ukr ui_popup(long a0, long a1, long a2, long a3);
UKF long ui_winlist(long a0, long a1);
UKF long ui_raise(long a0);
UKF long ui_tokens(void);
UKF long ui_scale(void);
UKF long ui_icon(long a0, long a1, long a2, long a3);
UKF ukr ui_popup_ex(long a0, long a1, long a2, long a3, long a4, long a5, long a6); /* r0 choix, r1 r2 : clic (-2) ou y de la ligne, libellé */
UKF long ui_shadow(long a0, long a1);
UKF long ui_popup_close(void);
UKF long ui_theme_text(long a0);
UKF long ui_menu(long a0);
UKF ukr ui_menu_get(long a0, long a1, long a2);
UKF long ui_menu_command(long a0, long a1);
UKF long ui_clip(long a0, long a1, long a2, long a3);
UKF long ui_text_fit(long a0, long a1, long a2, long a3, long a4, long a5, long a6);
UKF ukr ui_w_event(long a0);
UKF long ui_w_draw(long a0);
UKF long ui_w_move(long a0, long a1, long a2, long a3, long a4);
UKF long ui_w_show(long a0, long a1);
UKF long ui_w_enable(long a0, long a1);
UKF long ui_w_focus(long a0);
UKF long ui_w_get_focus(void);
UKF long ui_w_on(long a0, long a1, long a2);
UKF long ui_w_user(long a0);
UKF long ui_w_get_value(long a0);
UKF long ui_w_set_value(long a0, long a1);
UKF long ui_w_set_range(long a0, long a1, long a2, long a3, long a4);
UKF long ui_w_destroy(long a0);
UKF long ui_scrollbar(long a0, long a1, long a2, long a3, long a4);
UKF long ui_list(long a0, long a1, long a2, long a3, long a4);
UKF long ui_list_column(long a0, long a1, long a2, long a3);
UKF long ui_list_add(long a0, long a1, long a2);
UKF long ui_list_clear(long a0);
UKF long ui_list_count(long a0);
UKF ukr ui_list_get(long a0, long a1);
UKF long ui_list_set(long a0, long a1, long a2);
UKF long ui_list_remove(long a0, long a1);
UKF long ui_list_sort(long a0, long a1, long a2);
UKF long ui_text_prefix(long a0, long a1, long a2, long a3);
UKF ukr ui_text_hit(long a0, long a1, long a2, long a3);
UKF long ui_w_set_text(long a0, long a1);
UKF long ui_w_get_text(long a0);
UKF long ui_textbox(long a0, long a1, long a2, long a3, long a4);
UKF long ui_textbox_hint(long a0, long a1);
UKF long ui_textbox_select(long a0, long a1, long a2);
UKF ukr ui_textbox_selection(long a0);
UKF long ui_clip_set(long a0, long a1);
UKF long ui_clip_get(long a0, long a1);
UKF long ui_text_wrap(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7);
UKF long ui_pushbutton(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_label(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_message(long a0, long a1, long a2);
UKF long ui_ask(long a0, long a1, long a2, long a3);
UKF long ui_file_open(long a0, long a1, long a2);
UKF long ui_file_save(long a0, long a1, long a2, long a3);
UKF long ui_folder_choose(long a0, long a1, long a2);
UKF long ui_dialog_begin(long a0, long a1, long a2);
UKF long ui_dialog_button(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_dialog_run(long a0, long a1);
UKF long ui_dialog_close(long a0, long a1);
UKF long ui_dialog_cancel(long a0, long a1);
UKF long ui_dialog_end(long a0);
UKF long ui_checkbox(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_radio(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_switch(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_dropdown(long a0, long a1, long a2, long a3, long a4);
UKF long ui_combobox(long a0, long a1, long a2, long a3, long a4);
UKF long ui_textarea(long a0, long a1, long a2, long a3, long a4);
UKF ukr ui_textarea_caret(long a0);
UKF long ui_textarea_goto(long a0, long a1);
UKF long ui_slider(long a0, long a1, long a2, long a3, long a4, long a5, long a6);
UKF long ui_progress(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_numeric(long a0, long a1, long a2, long a3, long a4, long a5, long a6);
UKF long ui_knob(long a0, long a1, long a2, long a3, long a4, long a5, long a6);
UKF long ui_vumeter(long a0, long a1, long a2, long a3, long a4);
UKF long ui_vumeter_level(long a0, long a1);
UKF long ui_lcd(long a0, long a1, long a2, long a3, long a4);
UKF long ui_textarea_highlight(long a0, long a1);
UKF long ui_textarea_keywords(long a0, long a1);
UKF long ui_textarea_font(long a0, long a1);
UKF long ui_textarea_edit(long a0, long a1);
UKF long ui_textarea_find(long a0, long a1);
UKF long ui_w_style(long a0, long a1);
UKF long ui_w_text_len(long a0);
UKF long ui_resize(long a0, long a1);
UKF long ui_min_size(long a0, long a1);
UKF long ui_on_resize(long a0);
UKF long ui_splitter(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_panel(long a0, long a1, long a2, long a3, long a4);
UKF long ui_groupbox(long a0, long a1, long a2, long a3, long a4);
UKF long ui_w_set_parent(long a0, long a1);
UKF long ui_segmented(long a0, long a1, long a2, long a3, long a4);
UKF long ui_tabs(long a0, long a1, long a2, long a3, long a4);
UKF long ui_tabs_page(long a0, long a1);
UKF long ui_tabstrip(long a0, long a1, long a2, long a3, long a4);
UKF long ui_tabstrip_add(long a0, long a1, long a2);
UKF long ui_tabstrip_closing(long a0);
UKF long ui_toolbar(long a0, long a1, long a2, long a3);
UKF long ui_toolbar_add(long a0, long a1, long a2, long a3);
UKF long ui_image_load(long a0);
UKF long ui_image_decode(long a0, long a1);
UKF long ui_image_free(long a0);
UKF ukr ui_image_size(long a0);
UKF long ui_image_draw(long a0, long a1, long a2, long a3, long a4);
UKF long ui_imagebox(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_imagebox_set(long a0, long a1);
UKF long ui_cursor(long a0);
UKF long ui_w_cursor(long a0, long a1);
UKF long ui_w_tip(long a0, long a1);
UKF long ui_w_idle(void);
UKF long ui_drag_start(long a0, long a1, long a2);
UKF ukr ui_drop_get(long a0, long a1);
UKF long ui_tr(long a0);
UKF long ui_tr_load(long a0);
UKF long ui_lang(void);
UKF long ui_menu_state(long a0, long a1);
UKF long ui_menu_key(long a0);
UKF long ui_font_italic(void);
UKF long ui_calendar(long a0, long a1, long a2, long a3, long a4);
UKF long ui_calendar_today(long a0, long a1);
UKF long ui_datepicker(long a0, long a1, long a2, long a3, long a4);
UKF long ui_datepicker_today(long a0, long a1);
UKF long ui_palette(long a0, long a1, long a2, long a3, long a4);
UKF long ui_colorpicker(long a0, long a1, long a2, long a3, long a4);
UKF long ui_color_dialog(long a0, long a1);
UKF long ui_tree(long a0, long a1, long a2, long a3);
UKF long ui_tree_add(long a0, long a1, long a2, long a3);
UKF long ui_tree_expand(long a0, long a1, long a2);
UKF ukr ui_tree_get(long a0, long a1);
UKF long ui_tree_clear(long a0);
UKF long ui_list_virtual(long a0, long a1, long a2);
UKF ukr ui_list_sorted(long a0);
UKF long ui_layout(long a0, long a1, long a2, long a3);
UKF long ui_canvas(long a0, long a1, long a2, long a3);
UKF ukr ui_canvas_pixels(long a0);
UKF long ui_canvas_begin(long a0);
UKF long ui_canvas_end(void);
UKF long ui_vp_disc(long a0, long a1, long a2, long a3);
UKF long ui_vp_line(long a0, long a1, long a2, long a3, long a4, long a5);
UKF long ui_vp_poly(long a0, long a1, long a2);
UKF long ui_pad_hints(long a0, long a1, long a2, long a3, long a4);
UKF long ui_objnav(long a0, long a1, long a2, long a3);
UKF ukr ui_popup_local(long a0, long a1, long a2, long a3);
UKF long ui_font_get(long a0);
UKF long ui_surface(long a0, long a1, long a2, long a3, long a4);
UKF ukr ui_surface_pixels(long a0);
UKF long ui_surface_damage(long a0, long a1, long a2, long a3, long a4);
UKF long ui_surface_damage_list(long a0, long a1, long a2);
UKF long ui_surface_move(long a0, long a1, long a2, long a3, long a4);
UKF long ui_surface_free(long a0);
UKF long ui_maximize(void);
UKF long ui_file_filter(long a0);
UKF long ui_frame_request(void);
UKF long ui_view(long a0, long a1, long a2, long a3, long a4);
UKF long ui_view_on(long a0, long a1, long a2, long a3, long a4);
UKF long ui_view_dirty(long a0);
UKF long ui_view_flush(void);
UKF long ui_list_cell(long a0, long a1);
UKF long ui_date_popup(long a0, long a1, long a2);
UKF long ui_dialog_at(long a0, long a1);
UKF long ui_window_use(long a0);
UKF long ui_window_close(long a0);
UKF long ui_window_count(void);
UKF long ui_w_window(long a0);
UKF long ui_desk(long a0, long a1);
UKF long ui_win_details(long a0, long a1);
UKF long ui_tip_at(long a0, long a1, long a2);
UKF long ui_dialog_timer(long a0, long a1, long a2);
UKF long ui_textarea_insert(long a0, long a1);
UKF long ui_window_drag(void);
UKF ukr ui_clip_put(long a0, long a1, long a2, long a3);
UKF ukr ui_clip_take(long a0, long a1, long a2);
UKF long ui_clip_set_image(long a0, long a1, long a2);
UKF ukr ui_clip_get_image(long a0, long a1);
UKF long ui_clip_set_files(long a0, long a1);
UKF ukr ui_clip_get_files(long a0, long a1);
UKF long ui_clip_done_cut(void);
UKF long ui_calendar_show(long a0, long a1);
UKF long ui_textarea_paper(long a0, long a1, long a2);
UKF long ui_glyph(long a0, long a1, long a2, long a3, long a4);
UKF long ui_surface_source(long a0, long a1, long a2, long a3);
UKF long ui_pointer_lock(long a0);
UKF long ui_assoc_app(long a0, long a1, long a2);
UKF long ui_assoc_open(long a0);
UKF long ui_assoc_set(long a0, long a1);
UKF long ui_open_with(long a0);
UKF long ui_arg(long a0);
UKF long ui_screen_grab(long a0, long a1, long a2);
UKF long ui_minimize(void);
UKF long ui_metric(long a0);
UKF long ui_anchor(long a0, long a1);
UKF long ui_design_size(long a0, long a1);
UKF long ui_surface_overlay(long a0, long a1);
UKF long ui_elevate(long a0);
UKF long ui_drag_image(long a0, long a1, long a2, long a3, long a4, long a5);

/* appel d'une fonction du programme (rappel) : x0..x7 comme l'asm */
typedef long (*uk_fn8)(long, long, long, long, long, long, long, long);
static inline long uk_cb8(long f, long a, long b, long c, long d, long e, long g, long h, long i)
{
	return ((uk_fn8)(uintptr_t)f)(a, b, c, d, e, g, h, i);
}
/* rappel à n arguments (les registres au-delà ne comptent pas) */
#define UK_CB1(f, a) (((long (*)(long))(uintptr_t)(f))((long)(a)))
#define UK_CB2(f, a, b) (((long (*)(long, long))(uintptr_t)(f))((long)(a), (long)(b)))
#define UK_CB3(f, a, b, c) (((long (*)(long, long, long))(uintptr_t)(f))((long)(a), (long)(b), (long)(c)))
#define UK_CB4(f, a, b, c, d) \
	(((long (*)(long, long, long, long))(uintptr_t)(f))((long)(a), (long)(b), (long)(c), (long)(d)))
#define UK_CB5(f, a, b, c, d, e) \
	(((long (*)(long, long, long, long, long))(uintptr_t)(f))((long)(a), (long)(b), (long)(c), (long)(d), (long)(e)))

#endif
