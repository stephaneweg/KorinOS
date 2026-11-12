/* bench - coût de la composition d'Elegant sur le matériel, comme bench.asm :
 *   bench [secondes]   attend (20 s par défaut : bureau ouvert depuis
 *                      startup.txt), puis fait recomposer tout l'écran
 *                      20 fois avec les transparences (ombres, coins, dock),
 *                      puis 20 fois sans ; affiche le temps par image.
 * Les messages commencent par « mesure : » (l'ancien nom de l'outil), comme
 * l'asm. La mesure est faite par le serveur graphique (WS_OP_MESURE) : il n'y
 * a pas de boucle mesurée ici. L'argument est la ligne brute du noyau
 * (krt_line), lue par parse_dec (« 2x » : 2, « x » : 0). */
#include "korinrt.h"

#define IMAGES 20
#define WS_MSG 512
#define WM_OP 0
#define WM_A 8
#define WM_B 16
#define WS_OP_MESURE 9 /* a images, b 1 : sans transparence -> ms */

static long msg[WS_MSG / 8];

/* 1 sans transparence -> ms pour IMAGES recompositions */
static long run(long opaque)
{
	msg[WM_OP / 8] = WS_OP_MESURE;
	msg[WM_A / 8] = IMAGES;
	msg[WM_B / 8] = opaque;
	return korin_sys1(SYS_WS_CALL, msg);
}

/* ms pour IMAGES images : « x,y ms par image (z images/s) » */
static void per_image(u64 ms)
{
	u64 t = ms * 10 / IMAGES; /* dixièmes de ms par image */
	print_dec((long)(t / 10));
	ak_puts(",");
	print_dec((long)(t % 10));
	ak_puts(" ms par image (");
	print_dec((long)(IMAGES * 1000 / (ms ? ms : 1))); /* images par seconde */
	ak_puts(" images/s)\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long wait = 20000;
	if (krt_line && krt_line[0])
		wait = (long)(parse_dec(krt_line) * 1000);
	korin_sys1(SYS_SLEEP, wait);
	long a = run(0); /* avec transparences */
	long b = a;
	if (a >= 0)
		b = run(1); /* sans */
	if (a < 0 || b < 0) {
		ak_puts("mesure : pas de serveur graphique, erreur ");
		print_dec(a < 0 ? a : b);
		ak_puts("\n");
		korin_sys1(SYS_EXIT, 1);
	}
	ak_puts("mesure : composition de tout l'ecran avec transparences : ");
	per_image((u64)a);
	ak_puts("mesure : sans transparence (contour opaque) : ");
	per_image((u64)b);
	korin_sys1(SYS_EXIT, 0);
	return 0;
}
