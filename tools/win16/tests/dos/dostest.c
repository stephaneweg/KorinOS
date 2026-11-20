/* dostest.c -- essai de la machine DOS de win16 (étape D1) : la console du DOS, l'écran texte en B800,
 * le BIOS (curseur, INT 10h, INT 16h, INT 1Ah), la mémoire (48h/4Ah), les fichiers, EXEC d'un enfant (dosecho.exe)
 * et son code de retour. Compilé par Open Watcom (tools/win16/build-tests.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <process.h>
#include <conio.h>
#include <i86.h>

static void at (int row, int col, const char *s, unsigned char attr)
{
	unsigned short far *v = (unsigned short far *) MK_FP (0xb800, 0);
	while (*s)
		v[row * 80 + col++] = (unsigned short) ((unsigned char) *s++ | attr << 8);
}

int main (int argc, char **argv)
{
	union REGS r;
	FILE *f;
	char buf[64];
	unsigned seg, ticks0, ticks1;
	int rc, k;

	printf ("Korin DOS : essai de la machine DOS\n");
	printf ("arguments : %d, premier : %s\n", argc, argc > 1 ? argv[1] : "(aucun)");
	at (3, 10, " Texte direct en B800 ", 0x1e);
	at (4, 10, " Couleurs : ", 0x07);
	for (k = 0; k < 16; k++)
	{
		char c[2];
		c[0] = (char) ('0' + k % 10);
		c[1] = 0;
		at (4, 22 + k, c, (unsigned char) (k << 4 | 15));
	}
	/* le curseur par le BIOS */
	r.h.ah = 2;
	r.h.bh = 0;
	r.h.dh = 6;
	r.h.dl = 0;
	int86 (0x10, &r, &r);
	/* la mémoire */
	if (_dos_allocmem (0x100, &seg) == 0)
	{
		printf ("bloc de 4 Ko en %04X\n", seg);
		_dos_freemem (seg);
	}
	/* un fichier */
	f = fopen ("ESSAI.TXT", "w");
	if (f)
	{
		fputs ("bonjour du DOS\n", f);
		fclose (f);
	}
	f = fopen ("ESSAI.TXT", "r");
	if (f && fgets (buf, sizeof buf, f))
		printf ("relu : %s", buf);
	if (f)
		fclose (f);
	remove ("ESSAI.TXT");
	/* l'horloge (INT 1Ah) */
	r.h.ah = 0;
	int86 (0x1a, &r, &r);
	ticks0 = r.x.dx;
	delay (300);
	r.h.ah = 0;
	int86 (0x1a, &r, &r);
	ticks1 = r.x.dx;
	printf ("tics en 300 ms : %u\n", ticks1 - ticks0);
	/* un enfant */
	rc = spawnl (P_WAIT, "DOSECHO.EXE", "DOSECHO.EXE", "salut", NULL);
	printf ("l'enfant a rendu %d\n", rc);
	printf ("Une touche pour finir...");
	k = getch ();
	if (!k)
		k = 0x100 | getch ();
	printf ("\ntouche : %02X\n", k);
	return 3;
}
