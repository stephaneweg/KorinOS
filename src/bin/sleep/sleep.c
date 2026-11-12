/* sleep - attend : sleep secondes (docs/OUTILS.md, comme sleep.asm : nombre entier, parse_dec
 * d'AppKit ; sans argument, rien ; code 0) */
#include "korinrt.h"

int main(int argc, char **argv)
{
	(void)argc;
	if (argv[1])
		sys_sleep((long)(parse_dec(argv[1]) * 1000));
	return 0;
}
