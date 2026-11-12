/* rmdir - efface des dossiers vides : rmdir dossiers (docs/OUTILS.md), comme rmdir.asm : sans
 * option (SYS_UNLINK, qui efface aussi un fichier) ; code 1 si l'un échoue. */
#include "korinrt.h"

int main(int argc, char **argv)
{
	int code = 0;
	for (int i = 1; i < argc; i++)
		if (sys_unlink(argv[i])) {
			err_file(argv[i]);
			code = 1;
		}
	return code;
}
