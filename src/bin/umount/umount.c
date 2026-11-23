/* umount - démonter un volume (docs/OUTILS.md) : le pendant de mount, en C seulement (23/11/2026).
 *   umount POINT                le volume monté à ce point (« /media/usb0 », « /mnt/ram »)
 *   umount VOLUME               ou par son nom (« usb0 »)
 * Le volume quitte l'arbre et son service s'arrête (VFS_UNMOUNT) : un ramfs rend alors toute sa mémoire. Des fichiers
 * y sont encore ouverts : vfs le dit (occupé). « / » (le volume système) n'est jamais démonté. Un volume inconnu ou
 * pas monté : code 1 ; l'usage : code 2. */
#include "korinrt.h"

int main(int argc, char **argv)
{
	if (argc != 2 || argv[1][0] == '-') {
		err_say("usage : umount POINT | VOLUME\n");
		return 2;
	}
	const char *a = argv[1];
	if (a[0] == '/' && !a[1]) {
		err_say("umount : / est le volume système, il ne se démonte pas\n");
		return 1;
	}
	char name[64];
	if (a[0] == '/') { /* un point de montage : son nom chez vfs */
		if (!vu_name(a) || !vu_vol[0]) {
			err_puts(a);
			err_say(" : pas de volume monté à ce point\n");
			return 1;
		}
		name[0] = 0;
		str_cat(name, vu_vol); /* (16 octets au plus) */
	} else { /* un nom : en minuscules, sans « : » final */
		long n = 0;
		for (; a[n] && a[n] != ':' && n < 15; n++)
			name[n] = (char)(a[n] >= 'A' && a[n] <= 'Z' ? a[n] + 32 : a[n]);
		name[n] = 0;
	}
	if (str_eq(name, "root")) {
		err_say("umount : / est le volume système, il ne se démonte pas\n");
		return 1;
	}
	if (!vu_find(name)) {
		err_puts(a);
		err_say(" : pas de tel volume, ou pas monté\n");
		return 1;
	}
	long r = vu_unmount(name);
	if (r < 0) {
		err_puts(a);
		err_say(" pas démonté : ");
		vu_err(r);
		err_puts("\n");
		return 1;
	}
	out_puts(a);
	out_say(" démonté\n");
	return 0;
}
