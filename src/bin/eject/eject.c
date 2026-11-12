/* eject - une clé USB prête à être retirée (docs/OUTILS.md), comme eject.asm :
 *   eject [-f] [USB0: | USB1: ...]
 * Sans volume : la seule clé USB montée. Les entrées de ses fichiers ouverts sont d'abord écrites
 * (FS_SYNC) ; s'il en reste d'ouverts, rien n'est fait, sauf avec -f. Puis vfs le démonte. Seuls
 * les volumes dont la source commence par « blk.usb » s'éjectent. Un service sans FS_SYNC (erreur)
 * est démonté sans question. Usage (« - », une option autre que -f, deux volumes, un nom vide) :
 * code 2, comme « plusieurs clés » ; les autres refus : code 1. */
#include "korinrt.h"

static int is_usb(const char *src)
{
	for (const char *p = "blk.usb"; *p;)
		if (*p++ != *src++)
			return 0;
	return 1;
}

int main(int argc, char **argv)
{
	int force = 0;
	const char *vol = 0;
	char *e;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] == '-') {
			if (a[1] != 'f' || a[2])
				goto usage;
			force = 1;
			continue;
		}
		if (vol)
			goto usage;
		vol = a;
	}
	if (vol) {
		if (!vu_name(vol))
			goto usage;
		e = vu_find(vu_vol);
		if (!e) {
			err_puts(vu_aff);
			err_say(" n'est pas monté\n");
			return 1;
		}
	} else { /* la seule clé USB montée */
		long n = vu_list();
		if (n <= 0)
			goto none;
		e = 0;
		for (char *v = (char *)vu_msg + MSG_DATA; n; n--, v += VU_ENT) {
			if (!is_usb(v + VU_SRC))
				continue;
			if (e) {
				err_say("eject : plusieurs clés USB sont montées : laquelle ? (eject USB1:)\n");
				return 2;
			}
			e = v;
		}
		if (!e)
			goto none;
		vu_name(e);
	}
	if (!is_usb(e + VU_SRC)) {
		err_puts(vu_aff);
		err_say(" n'est pas une clé USB : il ne s'éjecte pas\n");
		return 1;
	}
	long r = vu_sync(vu_vol); /* ses fichiers ouverts */
	if (r > 0) {
		if (!force) {
			char t[32];
			err_puts(vu_aff);
			err_say(" pas éjecté : fichiers ouverts : ");
			fmt_dec((u64)r, t);
			err_puts(t);
			err_say("\n(leurs entrées sont écrites ; fermez-les, ou eject -f)\n");
			return 1;
		}
		err_say("eject : des fichiers sont encore ouverts : éjecté quand même (-f)\n");
	}
	r = vu_unmount(vu_vol);
	if (r < 0) {
		err_puts(vu_aff);
		err_say(" pas éjecté : ");
		vu_err(r);
		err_puts("\n");
		return 1;
	}
	out_puts(vu_aff);
	out_say(" peut être retiré sans risque\n");
	return 0;
none:
	err_say("eject : aucune clé USB n'est montée\n");
	return 1;
usage:
	err_say("usage : eject [-f] [USB0: | USB1: ...]\n");
	return 2;
}
