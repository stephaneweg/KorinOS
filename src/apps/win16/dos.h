/* dos.h -- win16 : la machine DOS (programmes MZ et .COM en mode réel ; docs/etude/WIN16.md §9).
 *
 * dos.c : la mémoire (MCB), les PSP, le chargeur, EXEC, l'INT 21h des processus et de la console, la boucle.
 * bios.c : la zone du BIOS, les interruptions du BIOS, le PIC, le PIT, le clavier, les ports, la fenêtre.
 * vga.c : la carte vidéo (texte pour l'instant) et son rendu dans la fenêtre. */
#ifndef W16_DOS_H
#define W16_DOS_H
#include "w16.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RM(seg, off)	(((uint32_t) (seg) << 4) + (uint16_t) (off))

/* la « ROM » du BIOS en F000 : F000:0000-00FF = un point d'arrêt par vecteur (le gestionnaire C, puis IRET),
 * F000:0100-01FF = des services lointains (le C, puis RETF), F000:1000 = du code 16 bits (IRQ) */
#define ROM_SEG		0xf000
#define TRAP_END	0x200
#define FAR_BASE	0x100
enum { FAR_TICK = 0x100, FAR_KBD, FAR_NULL, FAR_MOUSE, FAR_XMS };
#define ROM_CODE	0x1000
#define VROM_SEG	0xc000		/* les polices de la « ROM » vidéo */
#define FONT16_OFF	0x1000
#define FONT14_OFF	0x2000
#define FONT8_OFF	0x3000

/* un gestionnaire rend DONE, ou WAIT : il n'a pas fini (attente du clavier) et sera rappelé */
enum { DONE = 0, WAIT = 1 };

/* registres */
#define rAX ((uint16_t) cpu_reg (R_EAX))
#define rBX ((uint16_t) cpu_reg (R_EBX))
#define rCX ((uint16_t) cpu_reg (R_ECX))
#define rDX ((uint16_t) cpu_reg (R_EDX))
#define rSI ((uint16_t) cpu_reg (R_ESI))
#define rDI ((uint16_t) cpu_reg (R_EDI))
#define rBP ((uint16_t) cpu_reg (R_EBP))
#define rAH ((uint8_t) (cpu_reg (R_EAX) >> 8))
#define rAL ((uint8_t) cpu_reg (R_EAX))
#define rBH ((uint8_t) (cpu_reg (R_EBX) >> 8))
#define rBL ((uint8_t) cpu_reg (R_EBX))
#define rCH ((uint8_t) (cpu_reg (R_ECX) >> 8))
#define rCL ((uint8_t) cpu_reg (R_ECX))
#define rDH ((uint8_t) (cpu_reg (R_EDX) >> 8))
#define rDL ((uint8_t) cpu_reg (R_EDX))
void set16 (int r, uint16_t v);
void set8l (int r, uint8_t v);
void set8h (int r, uint8_t v);
void set_cf (int on);
void set_zf (int on);

/* dos.c */
int dos_main (const char *path, const char *dospath, const char *args);
int dos_trap (int vec);
void dos_far (int n);

/* bios.c */
void bios_init (void);
int bios_int (int vec);
void bios_far (int n);
void bios_poll (int force);
void bios_idle (int ms);
void pic_raise (int irq);
int pic_pending (void);
int pic_waiting (void);
int kbd_get (int remove, uint16_t *key);	/* une touche du tampon du BIOS -> 1 */
void bios_set_title (const char *title);
extern long bios_win;
extern int dos_hold, dos_closed;

/* mouse.c */
struct be_event;
void mouse_init (void);
void mouse_event (const struct be_event *e, int win_w, int win_h);
void mouse_far (void);
void mouse_mode_changed (void);	/* après un changement de mode vidéo : les bornes du nouveau mode */
int mouse_int33 (void);
int mouse_text_cursor (int *col, int *row, uint16_t *and_mask, uint16_t *xor_mask);
int mouse_graphic_cursor (int *x, int *y);

/* sound.c */
void sound_init (void);
void sound_poll (void);
int sound_in (uint16_t port, uint8_t *v);
int sound_out (uint16_t port, uint8_t v);
uint32_t bios_pit2_reload (void);
uint8_t bios_port61 (void);

/* vga.c */
void vga_init (void);
void vga_set_mode (int mode, int clear);
int vga_int10 (void);
void vga_tty (uint8_t ch);
void vga_render (void);
uint32_t vga_in (uint16_t port);
void vga_out (uint16_t port, uint8_t v);
void vga_mouse_space (int *w, int *h);	/* les coordonnées virtuelles de la souris dans ce mode */
extern int vga_width, vga_height;	/* la taille de l'image rendue */
uint32_t vga_rgb (int idx);
void vga_dac_put (int i, uint8_t r, uint8_t g, uint8_t b);
void vga_dac_get (int i, uint8_t *r, uint8_t *g, uint8_t *b);

/* svga.c */
extern int svga_on;
void svga_init (void);
void svga_off (void);
void svga_size (int *w, int *h);
int vesa_int10 (void);
void svga_render (uint32_t *px, int stride, int W, int H, int sx, int sy);
void svga_crtc_start (uint32_t bytes);

/* xms.c : XMS (INT 2Fh 43h, appel lointain) et EMS (INT 67h) */
void xms_init (void);
void xms_call (void);
int ems_int67 (void);

#ifdef __cplusplus
}
#endif
#endif
