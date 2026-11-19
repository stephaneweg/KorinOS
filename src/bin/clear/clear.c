/* clear - efface l'écran du terminal (docs/OUTILS.md) : le curseur en haut à gauche, l'écran vidé, puis
 * l'historique de défilement (ESC[3J, qu'un terminal qui ne le connaît pas ignore) ; les options sont ignorées */
#include "korinrt.h"

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	out_puts("\x1b[H\x1b[2J\x1b[3J");
	return 0;
}
