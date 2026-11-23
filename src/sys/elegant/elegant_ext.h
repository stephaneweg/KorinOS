/* elegant_ext.h : prototypes de uikit_ops.c, surface.c, snapshot.c, remote.c, screen*.c, pocket.c
 * (seconde partie de la traduction d'Elegant ; inclus par elegant.h après elegant_core.h).
 *
 * Ce que la première partie appelle chez nous est déjà déclaré dans elegant_core.h (« de
 * l'autre partie ») : ici, le reste de ce qui passe d'un de nos fichiers à l'autre. Une fonction
 * de l'asm qui rend plusieurs registres : out-pointeurs, notés à chaque ligne. */
#ifndef ELEGANT_EXT_H
#define ELEGANT_EXT_H

#ifndef E_BUSY
#define E_BUSY -16
#endif

/* ---- uikit_ops.c (uikit_ops.inc) */

/* win_restore (déclaré dans elegant_core.h, défini dans uikit_ops.c) : par place de fenêtre */
#define WR_X 0
#define WR_Y 1
#define WR_W 2
#define WR_H 3
#define WR_ON 4 /* 1 : agrandie */

void resize_post(struct win *w, long width, long height); /* WEV_RESIZE à son programme */
void desk_set(long desk, long count); /* < 0 : garder l'espace ; 0 : garder le nombre */
void desk_apply(void);
void desk_forget(struct win **v); /* remise à 0 si sa fenêtre est cachée */
void pointer_dirty(void);

/* ---- surface.c (surface.inc) */

/* (rdpd, WS_OP_WIN_READ, WIN_THUMB) la surface par-dessus le contenu recopié dans buf
 * (pas : width * 4) ; x, y : le coin du rectangle dans le contenu */
void surf_read_over(struct win *w, u32 *buf, long x, long y, long width, long height);
/* source RGB565 (pas en octets), x, y du tampon arrière : copie convertie, découpée par le clip */
void bb_blit565(const u16 *src, long pitch, long x, long y, long width, long height);

/* ---- snapshot.c (snapshot.inc) */

/* un client de WS_OP_SNAPSHOT, WIN_THUMB, WIN_READ (SC_*) : son tampon de la taille de l'écran */
struct snap_cli {
	u64 pid;
	u64 num; /* numéro du tampon partagé */
	u8 *adr; /* chez Elegant */
};
struct snap_cli *snap_client(u64 pid, long *err); /* (créé au premier appel) ; 0 : *err < 0 */
void snap_reset(void); /* l'écran a changé de taille : tampons des clients rendus */

/* ---- screen.c (screen.inc) ; screen_aarch64.c / screen_x86_64.c */

#define SCR_MAX_W 2560 /* (noyau : FB_MAX_W, FB_MAX_H) */
#define SCR_MAX_H 1600
#define SCR_BB_BYTES (SCR_MAX_W * SCR_MAX_H * 4)

/* la zone libre de l'écran : out coin du contenu d'une fenêtre agrandie, sa taille */
void screen_work(long *x, long *y, long *width, long *height);
/* in/out coin ; taille du contenu : dans l'écran autant que possible, titre visible */
void screen_fit(long *x, long *y, long width, long height);
long screen_switch(long width, long height); /* -> 0 ; E_IO : refusée (taille d'avant gardée) */
void screen_windows(long old_w, long old_h);
/* par machine : le premier bloc EDID de l'écran (128 octets), 0 : aucun (QEMU, écran muet, PC) */
const u8 *edid_read(void);
/* par machine : le framebuffer a pu bouger (info : SYS_WS_MODE) ; le Pi refait l'adresse de bus
 * du DMA 2D */
void screen_fb_moved(void);

/* ---- pocket.c (pocket.inc) */

long pk_is_app(const struct win *w); /* normale, à cadre (dialogues compris), hors accueil */
long pk_owns(const struct win *w);   /* mode poche : sa place est tenue par Elegant */

/* ---- overview.c : la vue d'ensemble (mode desktop) */

extern long ov_on;        /* non nul : la vue a l'écran (ouverture, ouverte, fermeture) */
void ov_open(void);
void ov_close(long desk, struct win *w); /* bureau (-1 : celui du milieu), fenêtre à ramener */
void ov_toggle(void);
void ov_tick(void);
long ov_busy(void);       /* une animation en cours */
void ov_reset(void);      /* l'écran change de taille */
void ov_compose_clip(void);
long ov_top(const struct win *w); /* montrée et touchée normalement pendant la vue */
long ov_mouse(long x, long y, long down, long up);
long ov_wheel(long n);   /* -> 1 si prise (0 : la grille des applications la reçoit) */
void ov_hwheel(long n);  /* crans horizontaux (vers la gauche > 0) */
long ov_key(const u8 *in);
void ov_gone(struct win *w);
long req_overview(void);
/* le glissement d'un bureau à l'autre dans la vue normale (overview.c) */
extern long sw_dir;       /* sens voulu (1 : le nouveau vient de la droite ; 0 : d'après les numéros) */
extern long sw_on;        /* un glissement en cours */
long sw_prepare(void);    /* l'image du bureau d'avant -> 1 si le glissement peut se faire */
void sw_start(long dir);  /* après le changement : l'image du nouveau, puis l'animation */
void sw_compose_clip(void);

#endif
