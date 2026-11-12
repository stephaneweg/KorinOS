/* pwd - le dossier courant (docs/OUTILS.md, comme pwd.asm : forme « SD0:/... » de vfs) */
#include "korinrt.h"

static char dir[256];

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	sys_getcwd(dir, 256);
	out_puts(dir);
	out_nl();
	return 0;
}
