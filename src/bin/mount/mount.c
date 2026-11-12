/* mount - les volumes (docs/OUTILS.md), comme mount.asm :
 *   mount                       chaque volume monté : nom, système de fichiers, taille, place
 *                               libre, étiquette, genre, puis sa source, son service et son
 *                               rootpath s'il en a un
 *   mount VOLUME:               le remonter : sa source vient de SD0:/etc/mounts.txt, sinon
 *                               USBn: -> blk.usbNp1 (ou blk.usbN), service fatfs
 *   mount VOLUME: SOURCE [SERVICE [ROOTPATH]]   monter une source donnée (service : fatfs par
 *                               défaut) ; ROOTPATH : le dossier de la source qui sert de racine
 *                               au volume (docs/FICHIERS.md ; « / » ou omis : sa racine)
 * Un volume déjà monté : « VOL: est déjà monté », code 0 (même si une source est donnée). Plus
 * de quatre arguments, ou un premier qui commence par « - » : l'usage (sans le nom de la
 * commande devant), code 2. Écart avec mount.asm : ROOTPATH (12/11/2026). */
#include "korinrt.h"

/* texte, largeur : le texte, puis des espaces jusqu'à la largeur (une au moins) (t_col) */
static void col(const char *s, u64 w)
{
	out_puts(s);
	u64 n = (u64)str_len(s);
	do {
		out_putc(' ');
		n++;
	} while (n < w);
}

static int prefix(const char *s, const char *p)
{
	while (*p)
		if (*p++ != *s++)
			return 0;
	return 1;
}

/* chaque volume monté, une ligne */
static void list(void)
{
	static char copy[VU_MAX * VU_ENT];
	long n = vu_list();
	if (n <= 0) {
		out_say("(aucun volume monté)\n");
		return;
	}
	mem_move(copy, vu_msg + MSG_DATA, VU_MAX * VU_ENT); /* (vu_msg change avec vu_info) */
	for (char *e = copy; n; n--, e += VU_ENT) {
		vu_name(e);
		col(vu_aff, 8);
		if (vu_info(e) < 0) {
			out_say("(le service ne répond pas)");
		} else {
			u64 *r = (u64 *)vu_rep;
			col(vu_type_name(r[MSG_D / 8]), 7);
			vu_size(r[MSG_A / 8]);
			if ((i64)r[MSG_B / 8] >= 0) {
				out_say(", libre ");
				vu_size(r[MSG_B / 8]);
			}
			char *lab = (char *)vu_rep + MSG_DATA;
			if (*lab) {
				out_puts(", « ");
				lab[63] = 0;
				out_puts(lab);
				out_puts(" »");
			}
		}
		if (*(u32 *)e == 0x00306473) /* « sd0 » : le système */
			out_say(", système");
		if (prefix(e + VU_SRC, "blk.usb"))
			out_say(", amovible");
		if (prefix(e + VU_SRC, "ram"))
			out_say(", en mémoire");
		out_puts("   (");
		out_puts(e + VU_SRC);
		out_putc(' ');
		out_puts(e + VU_FS);
		char sv[16], rt[64];
		if (vu_volinfo(e, sv, rt) == 0 && !(rt[0] == '/' && !rt[1])) { /* son rootpath */
			out_putc(' ');
			out_puts(rt);
		}
		out_putc(')');
		out_nl();
	}
}

int main(int argc, char **argv)
{
	if (argc == 1) {
		list();
		return 0;
	}
	if (argc > 5 || argv[1][0] == '-' || !vu_name(argv[1])) {
		err_say("usage : mount [VOLUME: [source [service [rootpath]]]]\n");
		return 2;
	}
	if (vu_find(vu_vol)) {
		out_puts(vu_aff);
		out_say(" est déjà monté\n");
		return 0;
	}
	if (argc == 2) {
		if (!vu_source(vu_vol)) {
			err_puts(vu_aff);
			err_say(" : volume inconnu (ni dans SD0:/etc/mounts.txt, ni une clé USB branchée)\n");
			return 1;
		}
	} else {
		str_cat(vu_src, argv[2]);
		str_cat(vu_srv, argc >= 4 ? argv[3] : "fatfs");
		vu_root[0] = 0;
		if (argc == 5) {
			if (str_len(argv[4]) > 63) {
				err_puts(vu_aff);
				err_say(" pas monté : ");
				vu_err(-22);
				err_puts("\n");
				return 1;
			}
			str_cat(vu_root, argv[4]);
		}
	}
	long r = vu_mount_root(vu_aff, vu_src, vu_srv, vu_root);
	if (r < 0) {
		err_puts(vu_aff);
		err_say(" pas monté : ");
		vu_err(r);
		err_puts("\n");
		return 1;
	}
	out_puts(vu_aff);
	out_say(" monté (");
	out_puts(vu_src);
	out_putc(' ');
	out_puts(vu_srv);
	if (vu_root[0] && !(vu_root[0] == '/' && !vu_root[1])) {
		out_putc(' ');
		out_puts(vu_root);
	}
	out_putc(')');
	out_nl();
	return 0;
}
