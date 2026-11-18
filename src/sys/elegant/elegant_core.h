/* elegant_core.h : prototypes de elegant.c, core.c, menu.c, desktop.c, shell.c, present*.c
 * (première partie de la traduction d'Elegant ; inclus par elegant.h).
 *
 * Une fonction de l'asm qui rend plusieurs registres : out-pointeurs, notés à chaque ligne.
 * Les noms sont ceux de l'asm (mots français traduits : voir elegant.h). */
#ifndef ELEGANT_CORE_H
#define ELEGANT_CORE_H

/* ---- elegant.c (elegant.asm) */

#define INI_MAX 1024
#define WIN_RESTORE_N 5 /* win_restore : x, y, l, h d'avant, 1 si agrandie */

extern u64 info[5];         /* SYS_WS_DISPLAY / SYS_WS_MODE : adresse, largeur, hauteur, pas,
			       adresse physique */
extern char ini_val[32];    /* la valeur lue par read_key */
extern char ini_buf[INI_MAX]; /* (read_key, session_start, option_word) */
extern long win_restore[MAX_WINDOWS][WIN_RESTORE_N]; /* (uikit_ops, screen, pocket) */

long mode_choose(const char *arg); /* -> policy ; 0 si « console » */
long force_desktop(void);
void read_shell(void);
long read_key_shell(const char *file);
long read_key(const char *file, const char *key); /* -> 1 si trouvée (valeur dans ini_val) */
/* heap_alloc : elegant.h */

/* ---- core.c (core.inc) */

#define WIN_LAYERS 4
void dirty(long x, long y, long w, long h);
void dirty_all(void);
void win_outer(const struct win *w, long *x, long *y, long *wd, long *ht); /* rectangle extérieur */
void dirty_window(struct win *w);
long win_layer(const struct win *w); /* 0 fond, 1 normale, 2 dessus, 3 plein écran */
void wins_changed(void);
void hist(long kind, long a, long b, long c, long d);
/* in : x, y, largeur, hauteur ; out : coin, taille de l'intersection avec clip_r (<= 0 si vide) */
void clip_rect(long *x, long *y, long *w, long *h);
void bb_fill(long x, long y, long w, long h, u32 color);
void bb_blit(const u32 *src, long pitch, long x, long y, long w, long h);
void bb_blit_any(const u32 *src, long pitch, long x, long y, long w, long h, long alpha);
void compose(void);
void compose_clip(void);
void draw_window(struct win *w);
void draw_framed(struct win *w);
void draw_cursor(void);
void hid_subscribe(void);
void do_input(void);
struct win *key_target(void);
struct win *find_grab(void);
void in_event(const u8 *in);
void damage_collect(void);
long frames_post(void); /* -> attente suivante (ms) */
long win_push(struct win *w, u8 *ev); /* -> résultat de SYS_WS_POST (< 0 : file pleine) */
void mouse(long x, long y, long buttons);
void send_mouse(struct win *w, long kind); /* 0 déplacement, 1 appui, 2 relâché */
struct win *win_hit(long x, long y, long *zone); /* zone : Z_* (0 si aucune fenêtre) */
struct win *win_blocker(struct win *w);
void win_raise(struct win *w);
void win_stack_top(struct win *w);
void win_set_focus(struct win *w);
void focus_top(void);
void win_minimize(struct win *w);
void win_unlink(struct win *w);
void win_destroy(struct win *w);
void recompute_reserve(void);
struct win *win_by_id(u64 id, long pid); /* pid -1 : quelconque */
void copy_title(char *d, const char *s, long max);
void notify_listeners(void);
void req_handle(long n);
void fill64(void *a, long bytes, u64 pattern); /* bytes : multiple de 8 */
struct win *req_win(void); /* la fenêtre de l'appelant désignée par msg a */
void win_damage(struct win *w, long x, long y, long wd, long ht);
long req_create(void);
long req_update(void);
long req_close(void);
long req_title(void);
long req_frame(void);
long req_raise(void);
long req_list(void);
long req_measure(void);
void req_exit(long pid);
void diag_windows(void);

/* ---- menu.c (menu.inc) */

u8 *menu_spec_of(struct win *w);
struct win *menu_active(void);
void menu_new_gen(struct win *w);
void menu_title_changed(struct win *w);
struct win *win_app(struct win *w);
struct win *menu_owner(struct win *w);
long req_menu_set(void);
long req_menu_get(void);
long req_menu_command(void);

/* ---- desktop.c (desktop.inc) */

#define COL_DESK 0x002B3A4E /* avant le fond d'écran */
extern const char str_mode_desktop[]; /* « desktop » */
void dk_none(void);            /* (bu_none) rien ; aussi dans la table du mode poche */
long dk_no(long x, long y);    /* (bu_no) -> 0 */
void dk_init(void);            /* (bu_init) la session : /etc/session */
void dk_back(void);
void dk_tick(void);
/* fichier de la session, programmes sans lui (chaînes à la suite, puis une vide) */
void session_start(const char *file, const char *defaults);
void session_poll(void); /* desktop.c : la fin de /sys/login, à chaque tour */
void session_run(char *line);

/* ---- shell.c (shell.inc, et src/svc/inc/buspub.inc) */

typedef void (*sh_ev_fn)(u8 *ev); /* un événement WEV_BUS de 32 octets */
extern long bp_canal;          /* canal de busd + 1 (0 : pas ouvert) */
extern long sh_sub_home, sh_sub_theme;
extern const char sh_topic_mode[], sh_topic_home[], sh_topic_switch[], sh_topic_quick[],
	sh_topic_sleep[], sh_topic_theme[], sh_val_1[];
void sh_tick(sh_ev_fn fn);
long sh_publish(const char *topic, const char *val); /* -> 1 si busd l'a pris */
long sh_subscribe(const char *pattern, long flags);  /* -> abonnement (<= 0 : aucun) */
long bp_open(long ms);
long bp_publish(const char *topic, const char *val, long flags);

/* ---- present.c, present_aarch64.c / present_x86_64.c (present.inc) */

extern u64 fb_alpha;  /* ou-logique des pixels copiés à l'écran (2 pixels) */
extern long pres_mode; /* 1 DMA 2D, 0 processeur */
extern long pres_busy, pres_t0;
extern u64 bb_bus, fb_bus;
extern long dma_n, dma_irqs; /* (diagnostic) */
extern char pres_line[192]; /* ligne du journal (present_init) */
long present_init(long size); /* -> 0 si mémoire insuffisante */
char *str_append(char *d, const char *s); /* -> sur le 0 */
u64 phys_bus(u64 phys);  /* -> adresse de bus du DMA (0 : hors du premier Go) */
void present_wait(void);
void present(void);
void present_diag(void);
long option_word(const char *word);
/* par machine */
void flush_rect(long x0, long y0, long x1, long y1); /* copie par le processeur */
long board_pi4(void);
/* le DMA 2D (Pi) : 1 s'il est pris ; sinon *reason = la raison du journal */
long present_dma_init(long size, const char **reason);
long present_dma_finish(long ms); /* (dma_finish) */
void present_dma(void);

/* ---- (de l'autre partie) : ce que la première appelle ; signatures supposées d'après
 * les en-têtes de l'asm, à corriger à l'intégration */

/* uikit_ops.c */
long req_clip_set(void);
long req_clip_get(void);
long req_win_pos(void);
long req_resize(void);
long req_cursor(void);
long req_cursor_get(void);
long req_pointer_lock(void);
long req_drag(void);
long req_drag_image(void);
void cursor_box(long *x, long *y, long *w, long *h);
void draw_ghost(void);
long req_drop_get(void);
long req_maximize(void);
long req_minimize(void);
long req_desk(void);
long req_win_drag(void);
const u8 *cursor_pick(long *hot_x, long *hot_y); /* dessin 12 x 19, point chaud */
void curtain_apply(void);
long desk_key(const u8 *in);
void desk_show(long desk);
void drag_drop(long x, long y);
void pointer_unlock(void);
void resize_begin(struct win *w, long x, long y);
void resize_end(long x, long y); /* (x1, x2 de l'asm ; x0 ignoré) */
void win_maximize(struct win *w);
/* surface.c */
long req_surface_attach(void);
long req_surface_damage(void);
long req_surface_over(void);
void draw_surface(struct win *w, long x, long y);
struct win *surf_cover(void);
void surf_clear(struct win *w);
void surf_detach(struct win *w);
/* snapshot.c */
long req_snapshot(void);
long req_win_thumb(void);
void snap_record(long x0, long y0, long x1, long y1);
void snap_exit(long pid);
/* remote.c */
long req_win_list(void);
long req_win_read(void);
long req_win_move(void);
long req_win_close(void);
/* screen.c */
long req_screen_set(void);
long req_screen_keep(void);
long req_screen_modes(void);
long req_win_place(void);
long req_win_top(void);
void screen_tick(void);
long win_is_max(struct win *w);
void restore_clear(struct win *w);
void reserve_update(void);
/* pocket.c */
extern struct win *pk_veil; /* le dialogue sous lequel est le voile */
void veil_fill(void);
void pk_home_cmd(void);

#endif
