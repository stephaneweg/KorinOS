/* elegant.h : Elegant en C, le serveur graphique de Korin (/sys/elegant), commun au Pi et
 * au PC. Repris de src/sys/elegant (elegant.asm et ses .inc) (l'asm reste dans le dépôt pour le selfhost) :
 * mêmes protocoles (WS_OP_*, WEV_*, ws_ext.inc), mêmes modes (shell= : desktop, poche, console),
 * mêmes fichiers lus. Ce qui ne sert qu'au Pi (DMA 2D, boîte aux lettres du firmware, EDID)
 * est dans les fichiers *_aarch64.c, le PC dans *_x86_64.c.
 *
 *	elegant.c	boucle, choix du mode (elegant.asm)
 *	core.c		fenêtres, recomposition, entrées, requêtes (core.inc)
 *	menu.c		menus des applications (menu.inc)
 *	desktop.c	mode « desktop » (desktop.inc)
 *	shell.c		le bus de la coquille, korin.shell.* (shell.inc)
 *	present.c	présentation à l'écran, partie commune (present.inc)
 *	present_aarch64.c / present_x86_64.c	DMA 2D du Pi / copie par le processeur
 *	uikit_ops.c	presse-papiers, tailles, curseurs, glisser, espaces (uikit_ops.inc)
 *	surface.c	surfaces attachées (surface.inc)
 *	snapshot.c	écran composé, vignettes (snapshot.inc)
 *	remote.c	les fenêtres une à une, rdpd (remote.inc)
 *	screen.c	résolution à chaud (screen.inc) ; screen_aarch64.c / _x86_64.c : EDID
 *	pocket.c	mode « poche » (pocket.inc)
 *	overview.c	la vue d'ensemble du bureau (bureaux en carrousel, exposé)
 *
 * Conventions de la traduction : une fonction de l'asm garde son nom (mots français traduits :
 * hid_abonner -> hid_subscribe, win_minimise -> win_minimize, req_mesure -> req_measure,
 * mode_bureau -> mode_desktop, mode_poche -> mode_pocket, bu_* -> dk_*, tas_alloc -> heap_alloc) ;
 * ses arguments x0, x1... deviennent ses paramètres dans l'ordre, x0 rendu son résultat ; une
 * fenêtre est un « struct win * » (l'adresse de sa fiche dans l'asm). Les bizarreries de l'asm
 * sont gardées et commentées « comme l'asm ». */
#ifndef ELEGANT_H
#define ELEGANT_H

/* korinrt, svclib, puis les constantes de l'asm en valeurs (uk_consts.h) */
#include "sys_consts.h"

#ifndef E_BADF
#define E_BADF -9
#endif

/* ---- entrées (anneau de HIDManager, format commun des pilotes) */
#define IN_KEY 1       /* +4 caractère, +8 code HID, +16 appui, +24 mods */
#define IN_MOUSE_REL 2 /* +8 dx, +16 dy, +24 boutons */
#define IN_MOUSE_ABS 3 /* +8 x, +16 y (0..32767), +24 boutons */
#define IN_WHEEL 4     /* +8 crans */
#define IN_HWHEEL 5    /* +8 crans horizontaux, vers la gauche > 0 (deux doigts sur le pavé tactile, i2chid) */
#define HID_OP_SUBSCRIBE 1
#define HID_RING_EV 64 /* anneau : +0 tête, +8 queue, événements */
#define HID_RING_N 64

/* ---- fenêtres */
#define MAX_WINDOWS 32
#define WEV_SIZE 32
#define TRANSPARENT 0xFF000000u /* pixel invisible (octet haut : transparence) */

#define MINI_REDUCED 1  /* (mini) le programme ou le cadre l'a réduite */
#define MINI_OFFDESK 2  /* sur un autre espace de travail */
#define MINI_CURTAIN 4  /* cachée par une fenêtre WF_CURTAIN */
#define MINI_BEHIND 8   /* mode poche : derrière l'app devant, ou sous l'accueil */
#define MINI_BAR 16     /* mode poche : barre du haut masquée (F11) */
#define MINI_OVERVIEW 32 /* (WF_OVERVIEW) hors de la vue d'ensemble (overview.c) */

/* zones (win_hit) */
#define Z_TITLE 1
#define Z_CLOSE 2
#define Z_CLIENT 3
#define Z_BORDER 4
#define Z_MINI 5
#define Z_MAXI 6

/* une fenêtre (W_* de core.inc ; la fiche n'est vue que d'Elegant) */
struct win {
	u64 used;
	u64 id;
	u64 pid;
	long x, y;   /* contenu, coordonnées écran */
	long w, h;
	u32 *surf;   /* contenu (tampon partagé, chez le serveur) */
	u64 buf;     /* numéro du tampon partagé */
	u64 flags;   /* rôle (WF_*) */
	u64 mini;    /* non nul : cachée ; MINI_* */
	u32 *chr_a;  /* cadre actif (largeur + 2 bords, hauteur + titre + bord) */
	u32 *chr_i;  /* cadre inactif */
	u64 nupd;    /* (diagnostic) mises à jour reçues */
	u64 mlen;    /* texte des menus : longueur (0 : aucun ; menu.c) */
	u64 mgen;    /* génération du menu (unique par envoi) */
	char title[120]; /* UTF-8 (W_TITLE..W_GEN de l'asm) */
	u32 gen;     /* génération du contenu (WS_OP_UPDATE) */
	u32 cgen;    /* génération du cadre (WS_OP_FRAME) */
	u64 *dmg;    /* en-tête WD_* en fin de tampon (ws_ext.inc) */
	u64 framep;  /* 1 : WEV_FRAME à envoyer */
	u64 desk;    /* son espace de travail + 1 (0 : tous) */
	u64 secure;  /* 1 : de /sys/authcard ou /sys/login en uid 0 (couche sûre, clavier gardé : A5) */
};

/* ---- la table d'un mode (mode.inc, POL_*) */
#define POLF_POCKET 1 /* mode poche : cadres à deux glyphes, fenêtres ni déplacées ni
			 redimensionnées à la main (pocket.c) */
struct policy {
	const char *name;              /* shell= de /etc/system.ini */
	long bar_h;                    /* hauteur réservée en bas (placement des fenêtres) */
	void (*init)(void);            /* après la prise de l'écran */
	void (*back)(void);            /* fond, dans le clip courant */
	void (*front)(void);           /* par-dessus les fenêtres (barres, menus) */
	long (*click)(long x, long y); /* clic gauche -> 1 si traité */
	long (*hover)(long x, long y); /* survol -> 1 si traité */
	void (*tick)(void);            /* à chaque tour de boucle */
	void (*wins)(void);            /* liste, focus ou titre des fenêtres changés */
	long flags;                    /* POLF_* */
	void (*place)(struct win *w);  /* fenêtre créée (placée, empilée, focus) */
	long (*key)(const u8 *in);     /* entrée brute IN_KEY -> 1 si prise (pas à l'app) */
	void (*gone)(struct win *w);   /* fenêtre qui va disparaître */
	void (*raise)(struct win *w);  /* fenêtre ramenée devant (WS_OP_RAISE) */
};
extern const struct policy mode_desktop, mode_pocket;
extern const struct policy *policy; /* table du mode actif */

/* ---- zones sales */
#define DIRTY_MAX 16
#define DIRTY_SLACK 4096 /* pixels redessinés en trop acceptés */
struct rect {
	long x0, y0, x1, y1;
};

/* ---- état commun (segment de données d'elegant.asm) */
extern long scr_w, scr_h;
extern u32 *fb_va;
extern long fb_pitch;
extern u32 *bb;          /* tampon arrière */
extern long bb_pitch;
extern long dirty_n, dirty_overflow;
extern struct rect dirty_list[DIRTY_MAX], cmp_list[DIRTY_MAX];
extern long cmp_n;
extern struct rect clip_r;
extern long cover_only;
extern long cur_mods, cur_x, cur_y, cur_buttons;
extern struct win *drag_win;
extern long drag_dx[2];
extern struct win *capture_win, *lock_win;
extern u64 cur_req_win; /* id de la fenêtre qui a demandé la forme (WS_OP_CURSOR) */
extern long cur_req, cur_shape;
extern long drag_on, drag_type, drag_len;
extern struct win *resize_win;
extern long resize_st[5];
extern long win_count;
extern struct win *win_focus;
extern long desk_cur, desk_count, desk_gen;
extern long curtain_on;
extern struct win *win_z[MAX_WINDOWS]; /* ordre d'empilement (0 : le plus bas) */
extern long next_win_id, win_cascade;
extern long area_top, area_bot; /* bords réservés (barre de menus, dock) */
extern long wins_dirty, need_all;
extern u8 *hid_ring;
extern long hist_n, diag_bad, measure_opaque;
extern long menu_gen, menu_last_gen, menu_serial;
extern long frame_next;
extern u8 msg[WS_MSG];       /* la requête en cours (et sa réponse) */
extern u8 evbuf[32];
extern char mode_name[32];
extern struct win windows[MAX_WINDOWS];
extern u8 menu_specs[MAX_WINDOWS][WS_MENU_MAX];
extern u8 drag_data[WS_DRAG_MAX];
extern long clip_len, clip_gen;
extern u8 *clip_buf;

#define M64(m, off) (*(u64 *)((u8 *)(m) + (off)))
#define M32(m, off) (*(u32 *)((u8 *)(m) + (off)))
#define MS64(m, off) (*(long *)((u8 *)(m) + (off)))

void *heap_alloc(long size); /* tas du programme (SYS_SBRK), aligné 16 ; 0 si échec */

/* prototypes des fichiers de la première partie (elegant.c, core.c, menu.c, desktop.c,
 * shell.c, present*.c) */
#include "elegant_core.h"
/* prototypes de la seconde (uikit_ops.c, surface.c, snapshot.c, remote.c, screen*.c,
 * pocket.c) */
#include "elegant_ext.h"

#endif
