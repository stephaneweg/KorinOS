/* outil de tools/gcc/test/shtest.sh : sans argument, compte les octets de l'entree standard ;
 * avec un NOM, ecrit sa variable d'environnement ; -n : compte les arguments */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main (int argc, char **argv)
{
	if (argc > 1 && strcmp (argv[1], "-n") == 0)
	{
		printf ("%d\n", argc - 2);
		return 0;
	}
	if (argc > 1)
	{
		const char *v = getenv (argv[1]);
		printf ("%s\n", v ? v : "(absente)");
		return 0;
	}
	char b[512];
	long n = 0, r;
	while ((r = read (0, b, sizeof b)) > 0)
		n += r;
	printf ("%ld\n", n);
	return 0;
}
