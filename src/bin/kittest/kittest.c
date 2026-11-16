/* kittest - essai du chargement des kits hors du noyau (docs/FICHIERS.md,
 * étape 5), comme kittest.asm : un programme sans droits fait inscrire NetKit
 * par init2 (f_kit_load -> INIT_OP_KIT_LOAD), puis l'ouvre (lib_open) ; 150
 * chargements de suite ne doivent rien laisser (mémoire libre et tas d'init2
 * inchangés) ; refus : kit absent, nom invalide, kit_register et boot_file
 * sans droits ; ancien open du noyau : E_NOSYS ; read(5) : E_BADF. Une ligne
 * « PASS » ou « FAIL » par essai, le bilan ; code de sortie = nombre de FAIL.
 * Comme l'asm : SYS_LIB_OPEN, SYS_KIT_REGISTER, SYS_BOOT_FILE et l'ancien
 * SYS_OPEN vont directement au noyau (svc 0), read(5) par file_sys. Sorties
 * par puts et print_dec d'AppKit (sans tampon). */
#include "korinrt.h"

#define N_LOADS 150

static u64 n_pass, n_fail;
static u64 infos[10];
static u8 plist[64 * 64];
static u8 buffer[4096];

/* -> mémoire libre (Ko) */
static long free_kb(void)
{
	korin_sys1(SYS_SYSINFO, infos);
	return (long)infos[1];
}

/* -> tas d'init2 (Ko, proclist), -1 s'il est absent */
static long init2_heap(void)
{
	long n = korin_sys2(SYS_PROCLIST, plist, 64);
	for (u8 *p = plist; n > 0; n--, p += 64)
		if (*(u32 *)(p + 24) == 0x74696E69 && *(u16 *)(p + 28) == 0x32) /* « init », « 2\0 » */
			return *(long *)(p + 16);
	return -1;
}

/* « kittest : PASS libellé (valeur) » */
static void verdict(int ok, const char *label, long v)
{
	if (ok)
		n_pass++;
	else
		n_fail++;
	ak_puts("kittest : ");
	ak_puts(ok ? "PASS " : "FAIL ");
	ak_puts(label);
	ak_puts(" (");
	print_dec(v);
	ak_puts(")\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long r = korin_sys2(SYS_LIB_OPEN, "netkit", 1); /* avant : en mémoire ou non */
	ak_puts("kittest : lib_open(netkit) avant : ");
	print_dec(r);
	ak_puts("\n");
	r = f_kit_load("netkit"); /* par init2 (sans droits) */
	verdict(r == 0, "f_kit_load(netkit) par init2", r);
	r = korin_sys2(SYS_LIB_OPEN, "netkit", 1);
	verdict(r > 0, "lib_open(netkit) ensuite", r);
	long mem0 = free_kb();
	long heap0 = init2_heap();
	long i;
	for (i = 0; i < N_LOADS; i++)
		if (f_kit_load("netkit"))
			break;
	verdict(i == N_LOADS, "chargements de suite", i);
	long d = mem0 - free_kb();
	verdict(d < 64, "memoire libre perdue (Ko)", d);
	d = init2_heap() - heap0;
	verdict(d == 0, "tas d'init2 (Ko de plus)", d);
	r = f_kit_load("absent");
	verdict(r == -2, "kit absent : E_NOENT", r);
	r = f_kit_load("../uikit");
	verdict(r == -22, "nom invalide : E_INVAL", r);
	r = korin_sys3(SYS_KIT_REGISTER, "uikit", buffer, 4096); /* droits de /sys */
	verdict(r == -1, "kit_register sans droits : E_PERM", r);
	r = korin_sys3(SYS_BOOT_FILE, "/etc/options.txt", buffer, 16); /* init seulement */
	verdict(r == -1, "boot_file sans droits : E_PERM", r);
	r = korin_sys2(SYS_OPEN, "/etc/options.txt", O_READ); /* ancien open du noyau */
	verdict(r == -38, "ancien open du noyau : E_NOSYS", r);
	r = sys_read(5, buffer, 16); /* descripteur 5 (ni AppKit ni stdio) */
	verdict(r == -9, "read(5) : E_BADF", r);
	ak_puts("kittest : ");
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	_exit((int)n_fail);
}
