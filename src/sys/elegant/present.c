/* present.c : la présentation, partie commune (present.inc) : les zones recomposées (cmp_list)
 * du tampon arrière vers le framebuffer.
 *
 * Sur le Pi (present_aarch64.c), comme Onyx (CCompositorTask::Present, option dispdma ; Circle :
 * CDMAChannel::SetupMemCopy2D) : un canal DMA du BCM2711 en mode 2D lit chaque zone EN PLACE dans
 * le tampon arrière et l'écrit dans le framebuffer ; les zones d'une recomposition forment une
 * chaîne de blocs de contrôle, lancée d'un coup ; Elegant repart aussitôt. Avant de redessiner,
 * compose attend la fin du transfert (present_wait). Sans DMA (option « no_dispdma », pas un
 * Pi 4, essai raté, PC) : la copie par le processeur (flush_rect, par machine).
 * Le journal dit le chemin pris (« elegant : copie vers l'écran par ... »). */
#include "elegant.h"

#define DMA_TIMEOUT 100 /* ms */
#define BUS_ALIAS 0xC0000000UL

u64 fb_alpha;  /* ou-logique des pixels copiés à l'écran (flush_rect) */
long pres_mode; /* présentation : 1 DMA 2D, 0 processeur */
long pres_busy; /* 1 : un transfert lancé, pas encore attendu */
long pres_t0;   /* (ms) son lancement */
u64 bb_bus, fb_bus; /* adresses de bus : tampon arrière, framebuffer */
long dma_n;     /* (diagnostic) transferts lancés */
long dma_irqs;  /* (diagnostic) fins vues par interruption */
char pres_line[192]; /* ligne du journal (present_init) */

static const char path_options[] = "/etc/options.txt";
static const char s_no_dispdma[] = "no_dispdma";
static const char m_cpu[] = "elegant : copie vers l'écran par le processeur (";
static const char m_r_opt[] = "no_dispdma)\n";
static const char m_r_board[] = "pas un Pi 4)\n";
static const char m_nl[] = "\n";
static const char m_dma_fail[] =
	"elegant : le DMA de l'écran ne répond plus : copie par le processeur\n";

/* taille de l'écran (largeur * hauteur * 4, la plus grande) -> le tampon arrière (bb, bb_pitch) et
 * le chemin de présentation ; 0 si mémoire insuffisante */
long present_init(long size)
{
	/* ailleurs que sur un Pi 4 (Pi 5) l'écran lit l'octet alpha : flush_rect rend les pixels
	   opaques */
	fb_alpha = board_pi4() ? 0 : 0xFF000000FF000000ULL;
	bb_pitch = scr_w << 2;
	const char *why = m_r_opt;
	if (!option_word(s_no_dispdma)) {
		why = m_r_board;
		if (board_pi4() && present_dma_init(size, &why)) { /* --- DMA 2D */
			pres_mode = 1;
			ak_puts(pres_line); /* (une ligne du journal, d'un seul puts) */
			return 1;
		}
	}
	str_append(str_append(pres_line, m_cpu), why); /* --- processeur (why : la raison) */
	ak_puts(pres_line);
	if (!bb) { /* (tampon DMA déjà pris : il sert) */
		bb = heap_alloc(size);
		if (!bb)
			return 0;
	}
	return 1;
}

/* fin de chaîne, chaîne : recopiée (0 final compris) -> sur le 0 */
char *str_append(char *d, const char *s)
{
	while ((*d = *s++))
		d++;
	return d;
}

/* adresse physique -> adresse de bus du DMA (0 : hors du premier Go) */
u64 phys_bus(u64 phys)
{
	if ((phys >> 30) || !phys)
		return 0;
	return phys | BUS_ALIAS;
}

/* avant de redessiner : le transfert précédent fini. Sans réponse en DMA_TIMEOUT ms, ou en
 * erreur : retour à la copie processeur, tout l'écran à recomposer. */
void present_wait(void)
{
	if (!pres_busy)
		return;
	pres_busy = 0;
	long left = DMA_TIMEOUT - (korin_sys0(SYS_TIME_MS) - pres_t0); /* (le temps déjà passé depuis
									    le lancement compte) */
	if (left < 0)
		left = 0;
	if (present_dma_finish(left + 1))
		return;
	pres_mode = 0; /* le DMA ne répond plus */
	ak_puts(m_dma_fail);
	dirty_all();
}

/* cmp_list (cmp_n zones) -> écran */
void present(void)
{
	if (pres_mode) {
		present_dma();
		return;
	}
	for (long i = 0; i < cmp_n; i++) /* --- processeur, zone par zone */
		flush_rect(cmp_list[i].x0, cmp_list[i].y0, cmp_list[i].x1, cmp_list[i].y1);
}

/* (F11) présentation : chemin, transferts DMA lancés, listes de zones pleines */
void present_diag(void)
{
	ak_puts("presentation (1 DMA 2D, 0 processeur) : ");
	print_dec(pres_mode);
	ak_puts(" ; transferts DMA : ");
	print_dec(dma_n);
	ak_puts(", par interruption ");
	print_dec(dma_irqs);
	ak_puts(" ; listes de zones pleines : ");
	print_dec(dirty_overflow);
	ak_puts(m_nl);
}

/* mot -> 1 s'il commence une ligne de /etc/options.txt (comme option_mot de hwmanager) */
long option_word(const char *word)
{
	long fd = sys_open(path_options, O_READ);
	if (fd < 0)
		return 0;
	long n = sys_read(fd, ini_buf, INI_MAX - 1);
	sys_close(fd);
	if (n <= 0)
		return 0;
	ini_buf[n] = 0;
	const u8 *p = (const u8 *)ini_buf;
	for (;;) {
		long k = 0; /* le mot en début de ligne */
		while (word[k] && (u8)word[k] == p[k])
			k++;
		if (!word[k] && p[k] <= ' ') /* suivi d'une fin de ligne */
			return 1;
		for (;;) { /* ligne suivante */
			u8 c = *p++;
			if (!c)
				return 0;
			if (c == 10)
				break;
		}
	}
}
