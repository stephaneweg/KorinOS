/* Essai de tpm.c sous Linux contre swtpm (TPM_TCP) : sceller, desceller, reprendre une clé en fichier,
 * refuser un fichier abîmé. Lancé par test/run.sh. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../tpm.h"

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAUX %s\n", m); fails++; } } while (0)

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp/kvtpm";
	char p[256];
	uint8_t a[32], b[32], k[32];
	mkdir(dir, 0700);
	snprintf(p, sizeof p, "%s/machine.tpm", dir);
	unlink(p);
	/* reprise d'une clé en fichier */
	for (int i = 0; i < 32; i++)
		k[i] = (uint8_t)(i * 7 + 1);
	char kf[256];
	snprintf(kf, sizeof kf, "%s/machine.key", dir);
	FILE *f = fopen(kf, "wb");
	fwrite(k, 1, 32, f);
	fclose(f);
	int r = tpm_machine_key(dir, a);
	CHECK(r == 1, "première fois : scellée (reprise de machine.key)");
	CHECK(!memcmp(a, k, 32), "même clé que machine.key");
	CHECK(access(kf, F_OK) != 0, "machine.key effacée");
	CHECK(access(p, F_OK) == 0, "machine.tpm écrite");
	r = tpm_machine_key(dir, b);
	CHECK(r == 1 && !memcmp(a, b, 32), "descellée : la même clé");
	/* nouvelle clé sans fichier */
	unlink(p);
	r = tpm_machine_key(dir, b);
	CHECK(r == 1 && memcmp(a, b, 32), "sans machine.key : une nouvelle clé");
	r = tpm_machine_key(dir, a);
	CHECK(r == 1 && !memcmp(a, b, 32), "relue : la même");
	/* fichier abîmé */
	f = fopen(p, "r+b");
	fseek(f, 20, SEEK_SET);
	fputc(0x5A, f);
	fclose(f);
	r = tpm_machine_key(dir, a);
	CHECK(r < 0, "machine.tpm abîmé : refusé");
	printf("%s\n", fails ? "ECHEC" : "tout est bon");
	return fails != 0;
}
