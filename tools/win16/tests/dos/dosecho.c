/* dosecho.c -- l'enfant de dostest.c : affiche ses arguments et rend 42 (EXEC et code de retour de la machine
 * DOS de win16). */
#include <stdio.h>

int main (int argc, char **argv)
{
	int i;
	printf ("  [enfant]");
	for (i = 1; i < argc; i++)
		printf (" %s", argv[i]);
	printf ("\n");
	return 42;
}
