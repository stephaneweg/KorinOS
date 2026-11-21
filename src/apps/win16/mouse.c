/* mouse.c -- win16, la machine DOS : le pilote de souris (INT 33h, comme MOUSE.COM de Microsoft ; WIN16.md §9.2).
 *
 * La souris de Korin donne des pixels de la fenêtre ; le pilote les ramène aux coordonnées virtuelles du mode (640 ×
 * 200 en texte et en CGA, 640 × 480 en VGA 12h…). Le curseur du mode texte est dessiné par le rendu (vga.c), pas
 * écrit dans la mémoire vidéo. Le gestionnaire d'événements du programme (fonction 0Ch) est appelé par l'IRQ 12 :
 * du code 16 bits en F000:1080 sauve les registres, demande au C les valeurs de l'événement (FAR_MOUSE), appelle le
 * gestionnaire et rend l'EOI. */
#include <stdio.h>
#include <string.h>
#include "dos.h"
#include "be.h"

static int mx, my;			/* la position virtuelle */
static int buttons;			/* bit 0 gauche, 1 droit, 2 milieu */
static int shown = -1;			/* visible quand >= 0 */
static int xmin, xmax = 639, ymin, ymax = 199;
static int mick_x, mick_y;
static int press_n[3], release_n[3], press_x[3], press_y[3], release_x[3], release_y[3];
static uint16_t handler_mask;
static uint32_t handler;		/* seg:off */
static uint16_t ev_mask;		/* les conditions en attente pour le gestionnaire */
static uint16_t text_and = 0xffff, text_xor = 0x7700;

#define HANDLER_LIN	RM (ROM_SEG, 0x1100)

static void virt_size (int *w, int *h)
{
	vga_mouse_space (w, h);
}

static void clamp (void)
{
	if (mx < xmin) mx = xmin;
	if (mx > xmax) mx = xmax;
	if (my < ymin) my = ymin;
	if (my > ymax) my = ymax;
}

static void reset (void)
{
	int w, h;
	virt_size (&w, &h);
	xmin = 0;
	ymin = 0;
	xmax = w - 1;
	ymax = h - 1;
	mx = w / 2;
	my = h / 2;
	shown = -1;
	handler_mask = 0;
	handler = 0;
	ev_mask = 0;
	memset (press_n, 0, sizeof press_n);
	memset (release_n, 0, sizeof release_n);
	mick_x = mick_y = 0;
}

/* un changement de mode vidéo (INT 10h 00h, VESA 4F02h) : comme le pilote de Microsoft, les bornes deviennent
 * celles du nouveau mode (QuickBASIC remet le pilote à zéro en mode texte, 640 × 200, puis passe en SCREEN 12 :
 * la souris s'arrêtait à la ligne 199 des 480) */
void mouse_mode_changed (void)
{
	int w, h;
	virt_size (&w, &h);
	xmin = 0;
	ymin = 0;
	xmax = w - 1;
	ymax = h - 1;
	clamp ();
}

static void post (uint16_t cond)
{
	if (!(cond & handler_mask) || !handler)
		return;
	ev_mask |= cond & handler_mask;
	pic_raise (12);
}

/* un événement de souris de Korin (x, y dans la zone cliente, w × h sa taille) */
void mouse_event (const struct be_event *e, int win_w, int win_h)
{
	int w, h;
	virt_size (&w, &h);
	if (win_w <= 0 || win_h <= 0)
		return;
	int nx = e->x * w / win_w, ny = e->y * h / win_h;
	uint16_t cond = 0;
	if (nx != mx || ny != my)
	{
		mick_x += (nx - mx) * 8 * 640 / (w * 8);
		mick_y += (ny - my) * 16 * 200 / (h * 8);
		mx = nx;
		my = ny;
		clamp ();
		cond |= 1;
	}
	if (e->type == BE_MOUSE_DOWN || e->type == BE_MOUSE_UP)
	{
		int b = e->button & 1 ? 0 : e->button & 2 ? 1 : e->button & 4 ? 2 : (e->type == BE_MOUSE_UP ? -1 : 0);
		for (int i = 0; i < 3; i++)
		{
			if (b >= 0 && i != b)
				continue;
			int bit = 1 << i;
			if (e->type == BE_MOUSE_DOWN && !(buttons & bit))
			{
				buttons |= bit;
				press_n[i]++;
				press_x[i] = mx;
				press_y[i] = my;
				cond |= (uint16_t) (2 << (i * 2));
			}
			else if (e->type == BE_MOUSE_UP && (buttons & bit))
			{
				buttons &= ~bit;
				release_n[i]++;
				release_x[i] = mx;
				release_y[i] = my;
				cond |= (uint16_t) (4 << (i * 2));
			}
		}
	}
	post (cond);
}

/* FAR_MOUSE : les registres du gestionnaire (AX conditions, BX boutons, CX/DX position, SI/DI mickeys) */
void mouse_far (void)
{
	set16 (R_EAX, ev_mask);
	ev_mask = 0;
	set16 (R_EBX, (uint16_t) buttons);
	set16 (R_ECX, (uint16_t) mx);
	set16 (R_EDX, (uint16_t) my);
	set16 (R_ESI, (uint16_t) mick_x);
	set16 (R_EDI, (uint16_t) mick_y);
	wr32 (HANDLER_LIN, handler);
}

/* le curseur à dessiner en mode texte : 1 et la case, son masque (ET, OU exclusif sur caractère et attribut) */
int mouse_text_cursor (int *col, int *row, uint16_t *and_mask, uint16_t *xor_mask)
{
	if (shown < 0)
		return 0;
	*col = mx / 8;
	*row = my / 8;
	*and_mask = text_and;
	*xor_mask = text_xor;
	return 1;
}

int mouse_graphic_cursor (int *x, int *y)
{
	if (shown < 0)
		return 0;
	*x = mx;
	*y = my;
	return 1;
}

static int button_index (void) { return rBX < 3 ? rBX : 0; }

int mouse_int33 (void)
{
	int i;
	switch (rAX)
	{
	case 0x00:
	case 0x21:
		reset ();
		set16 (R_EAX, 0xffff);
		set16 (R_EBX, 2);
		break;
	case 0x01:
		if (shown < 0)
			shown++;
		break;
	case 0x02:
		shown--;
		break;
	case 0x03:
	{
		/* (un programme qui sonde la souris en boucle : laisser souffler Korin quand rien ne bouge) */
		static int polls, lx, ly, lb;
		if (mx == lx && my == ly && buttons == lb)
		{
			if (++polls > 50)
			{
				polls = 0;
				bios_idle (2);
			}
		}
		else
			polls = 0;
		lx = mx;
		ly = my;
		lb = buttons;
		set16 (R_EBX, (uint16_t) buttons);
		set16 (R_ECX, (uint16_t) mx);
		set16 (R_EDX, (uint16_t) my);
		bios_poll (0);
		break;
	}
	case 0x04:
		mx = (int16_t) rCX;
		my = (int16_t) rDX;
		clamp ();
		break;
	case 0x05:
		i = button_index ();
		set16 (R_EAX, (uint16_t) buttons);
		set16 (R_EBX, (uint16_t) press_n[i]);
		set16 (R_ECX, (uint16_t) press_x[i]);
		set16 (R_EDX, (uint16_t) press_y[i]);
		press_n[i] = 0;
		break;
	case 0x06:
		i = button_index ();
		set16 (R_EAX, (uint16_t) buttons);
		set16 (R_EBX, (uint16_t) release_n[i]);
		set16 (R_ECX, (uint16_t) release_x[i]);
		set16 (R_EDX, (uint16_t) release_y[i]);
		release_n[i] = 0;
		break;
	case 0x07:
		xmin = (int16_t) rCX < (int16_t) rDX ? (int16_t) rCX : (int16_t) rDX;
		xmax = (int16_t) rCX < (int16_t) rDX ? (int16_t) rDX : (int16_t) rCX;
		clamp ();
		break;
	case 0x08:
		ymin = (int16_t) rCX < (int16_t) rDX ? (int16_t) rCX : (int16_t) rDX;
		ymax = (int16_t) rCX < (int16_t) rDX ? (int16_t) rDX : (int16_t) rCX;
		clamp ();
		break;
	case 0x0a:
		if (rBX == 0)
		{
			text_and = rCX;
			text_xor = rDX;
		}
		break;
	case 0x0b:
		set16 (R_ECX, (uint16_t) mick_x);
		set16 (R_EDX, (uint16_t) mick_y);
		mick_x = mick_y = 0;
		break;
	case 0x0c:
		handler_mask = rCX;
		handler = (uint32_t) cpu_sreg (S_ES) << 16 | rDX;
		break;
	case 0x14:
	{
		uint16_t om = handler_mask;
		uint32_t oh = handler;
		handler_mask = rCX;
		handler = (uint32_t) cpu_sreg (S_ES) << 16 | rDX;
		set16 (R_ECX, om);
		set16 (R_EDX, (uint16_t) oh);
		cpu_set_sreg (S_ES, (uint16_t) (oh >> 16));
		break;
	}
	case 0x15:
		set16 (R_EBX, 0x40);
		break;
	case 0x16:
	case 0x17:
		break;
	case 0x1b:
		set16 (R_EBX, 50);
		set16 (R_ECX, 50);
		set16 (R_EDX, 50);
		break;
	case 0x1d:
	case 0x1e:
		set16 (R_EBX, 0);
		break;
	case 0x24:
		set16 (R_EBX, 0x0626);
		set16 (R_ECX, 0x0400);
		break;
	default:
		break;
	}
	return DONE;
}

void mouse_init (void)
{
	/* l'IRQ 12 : appelle le gestionnaire du programme */
	static const uint8_t stub[] = {
		0x60, 0x1e, 0x06,					/* pusha ; push ds ; push es */
		0x9a, FAR_MOUSE & 0xff, FAR_MOUSE >> 8, 0x00, 0xf0,	/* call far F000:FAR_MOUSE */
		0x2e, 0xff, 0x1e, 0x00, 0x11,				/* call far [cs:1100] */
		0x07, 0x1f,						/* pop es ; pop ds */
		0xb0, 0x20, 0xe6, 0xa0, 0xe6, 0x20,			/* EOI (esclave, maître) */
		0x61, 0xcf						/* popa ; iret */
	};
	memcpy (lin_ptr (RM (ROM_SEG, 0x1080)), stub, sizeof stub);
	wr16 (0x74 * 4, 0x1080);
	wr16 (0x74 * 4 + 2, ROM_SEG);
	reset ();
}
