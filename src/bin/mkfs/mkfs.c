/* mkfs - formater un volume : tout ce qu'il contient est effacé, un système de fichiers FAT32
 * neuf y est écrit (docs/OUTILS.md), comme mkfs.asm :
 *   mkfs [-t auto|fat32] [-L étiquette] [-c octets] [-y] [-f] [--card] VOLUME:
 *   -t      FAT32 seulement (auto, fat32, vfat, sans la casse)
 *   -L      l'étiquette du volume (11 caractères au plus, mise en majuscules)
 *   -c      la taille d'un cluster : 512 à 64K (« 4096 », « 4k ») ; sinon d'après la taille
 *   -y      sans question (sinon : taper le nom du volume sur l'entrée standard)
 *   -f      même avec des fichiers ouverts dessus
 *   --card  permettre une partition de la carte SD. SD0: (ou SD:, blk.sd0p1), jamais.
 * Le volume est formaté sur sa source (monté : celle de vfs ; sinon SD0:/etc/mounts.txt ou
 * blk.usbNp1) par le protocole des périphériques blocs (src/inc/blk.inc) : démonté, écrit, puis
 * remonté (même en cas d'échec). Comme l'asm : une option inconnue prend l'argument suivant puis
 * donne l'usage ; -t, -L, -c sans valeur : l'usage ; usage et options invalides : code 2, les
 * autres erreurs : code 1. */
#include "korinrt.h"

#define BLK_INFO 1
#define BLK_ATTACH 2
#define BLK_WRITE 4
#define BUF_VA_BASE 0x6800000000ull /* tampon n : BUF_VA_BASE + n * BUF_SLOT (armpi.inc) */
#define BUF_SLOT 0x4000000ull
#define TAMPON 65536 /* tampon partagé avec le service bloc */
#define PAQUET (TAMPON / 512) /* secteurs par écriture */
#define RSVD 32 /* secteurs réservés (FAT32) */
#define NFATS 2
#define CLUS_MIN 65525 /* en dessous : FAT16 pour les autres systèmes */
#define CLUS_MAX 0x0FFFFFF5

static long chan, pid_blk, mounted;
static u8 *tbuf;
static u64 opt_cluster, sectors, spc, fatsz, data_start, clusters;
static char label[64];
static u64 msg[MSG_SIZE / 8];

static int prefix(const char *s, const char *p)
{
	while (*p)
		if (*p++ != *s++)
			return 0;
	return 1;
}

/* ---- le périphérique bloc */

/* op, a, b, c : requête au service bloc -> résultat (réponse dans msg) */
static long blk(long op, long a, long b, long c)
{
	msg[0] = (u64)op;
	msg[1] = (u64)a;
	msg[2] = (u64)b;
	msg[3] = (u64)c;
	msg[4] = 0;
	msg[5] = 0;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* premier secteur, nombre (<= PAQUET), décalage dans le tampon : écrits -> 0, erreur */
static long write_sectors(u64 first, u64 n, u64 off)
{
	return blk(BLK_WRITE, (long)first, (long)n, (long)(tbuf + off));
}

/* premier secteur, nombre : mis à zéro (le tampon est à zéro) -> 0, erreur */
static long zeros(u64 first, u64 n)
{
	while (n) {
		u64 k = n < PAQUET ? n : PAQUET;
		n -= k;
		long r = write_sectors(first, k, 0);
		first += k;
		if (r < 0)
			return r;
	}
	return 0;
}

static void buf_zero(void)
{
	memset(tbuf, 0, TAMPON);
}

/* ---- la disposition FAT32 (fatgen103) */

/* secteurs -> spc, fatsz, clusters ; 0, -1 si le volume est trop petit (ou trop grand) */
static int layout(void)
{
	u64 s = sectors;
	u64 c = opt_cluster >> 9; /* secteurs par cluster demandés */
	if (!c) { /* comme Windows, d'après la taille */
		if (s <= 131072)
			c = 1;
		else if (s <= 262144)
			c = 2;
		else if (s <= 524288)
			c = 4;
		else if (s <= 16777216)
			c = 8;
		else if (s <= 33554432)
			c = 16;
		else if (s <= 67108864)
			c = 32;
		else
			c = 64;
	}
	spc = c;
	if (s <= RSVD)
		return -1;
	u64 t1 = s - RSVD;
	u64 t2 = (c << 7) + 1; /* 128 * spc + 1 (deux FAT) */
	fatsz = (t1 + t2 - 1) / t2;
	data_start = RSVD + (fatsz << 1);
	if (s <= data_start)
		return -1;
	clusters = (s - data_start) / c;
	if (clusters < CLUS_MIN || clusters > CLUS_MAX)
		return -1;
	return 0;
}

static void st16(u8 *p, u64 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static void st32(u8 *p, u64 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* destination (11 octets) : l'étiquette en majuscules, complétée d'espaces */
static void label_name(u8 *d)
{
	int i = 0;
	for (; i < 11; i++) {
		u8 c = (u8)label[i];
		if (!c)
			break;
		if ((u8)(c - 'a') <= 25)
			c -= 32;
		d[i] = c;
	}
	for (; i < 11; i++)
		d[i] = ' ';
}

/* le secteur de démarrage FAT32 dans le tampon ; BPB d'après fatgen103 */
static void boot_sector(u8 *b)
{
	b[0] = 0xEB; /* saut, OEM */
	b[1] = 0x58;
	b[2] = 0x90;
	memcpy(b + 3, "KORIN OS", 8);
	st16(b + 11, 512); /* octets par secteur */
	b[13] = (u8)spc;
	st16(b + 14, RSVD);
	b[16] = NFATS;
	b[21] = 0xF8; /* support fixe */
	st16(b + 24, 63); /* géométrie (sans usage) */
	st16(b + 26, 255);
	st32(b + 32, sectors);
	st32(b + 36, fatsz);
	st32(b + 44, 2); /* cluster de la racine */
	st16(b + 48, 1); /* FSInfo, copie du secteur de démarrage */
	st16(b + 50, 6);
	b[64] = 0x80; /* lecteur */
	b[66] = 0x29; /* signature étendue */
	u64 ms = (u64)sys_time_ms(); /* numéro de série */
	st32(b + 67, ms ^ (u64)sys_clock(0));
	if (label[0])
		label_name(b + 71);
	else
		memcpy(b + 71, "NO NAME    ", 11);
	memcpy(b + 82, "FAT32   ", 8);
	b[90] = 0xEB; /* code : une boucle (« jmp $ ») */
	b[91] = 0xFE;
	st16(b + 510, 0xAA55);
}

/* le formatage -> 0, erreur */
static long format(void)
{
	long id = korin_sys1(SYS_BUF_CREATE, TAMPON); /* le tampon partagé avec le service bloc */
	if (id < 0)
		return id;
	tbuf = (u8 *)(BUF_VA_BASE + (u64)id * BUF_SLOT);
	long r = korin_sys2(SYS_BUF_GRANT, id, pid_blk);
	if (r < 0)
		return r;
	if ((r = blk(BLK_ATTACH, id, TAMPON, 0)) < 0)
		return r;
	buf_zero();
	u8 *b = tbuf;
	/* 1) à zéro : les secteurs réservés, les deux FAT, le cluster de la racine */
	if ((r = zeros(0, data_start + spc)) < 0)
		return r;
	/* 2) le début de chaque FAT : entrées 0, 1, et 2 (la racine, fin de chaîne) */
	st32(b, 0x0FFFFFF8);
	st32(b + 4, 0x0FFFFFFF);
	st32(b + 8, 0x0FFFFFFF);
	if ((r = write_sectors(RSVD, 1, 0)) < 0)
		return r;
	if ((r = write_sectors(fatsz + RSVD, 1, 0)) < 0)
		return r;
	buf_zero();
	/* 3) la racine : l'étiquette, s'il y en a une */
	if (label[0]) {
		label_name(b);
		b[11] = 0x08; /* attribut : étiquette du volume */
		if ((r = write_sectors(data_start, 1, 0)) < 0)
			return r;
		buf_zero();
	}
	/* 4) FSInfo (1, copie en 7) */
	st32(b, 0x41615252);
	st32(b + 484, 0x61417272);
	st32(b + 488, clusters - 1); /* clusters libres (la racine en prend un) */
	st32(b + 492, 3); /* prochain libre */
	st32(b + 508, 0xAA550000);
	if ((r = write_sectors(1, 1, 0)) < 0)
		return r;
	if ((r = write_sectors(7, 1, 0)) < 0)
		return r;
	buf_zero();
	/* 5) le secteur de démarrage, en dernier (copie en 6 d'abord) */
	boot_sector(b);
	if ((r = write_sectors(6, 1, 0)) < 0)
		return r;
	if ((r = write_sectors(0, 1, 0)) < 0)
		return r;
	return 0;
}

/* « mkfs : USB0: pas formaté : <texte> » */
static void err_not_formatted(long e)
{
	err_say("mkfs : ");
	err_puts(vu_aff);
	err_say(" pas formaté : ");
	vu_err(e);
	err_puts("\n");
}

/* « mkfs : USB0: <texte> », code 1 */
static int refuse(const char *s)
{
	err_say("mkfs : ");
	err_puts(vu_aff);
	err_say(s);
	return 1;
}

int main(int argc, char **argv)
{
	int flags = 0; /* bit 0 -y, 1 -f, 2 --card */
	char *vol = 0;
	for (int i = 1; i < argc;) {
		char *a = argv[i++];
		if (a[0] != '-') {
			if (vol)
				goto usage;
			vol = a;
			continue;
		}
		if (str_eq(a, "-y")) {
			flags |= 1;
			continue;
		}
		if (str_eq(a, "-f")) {
			flags |= 2;
			continue;
		}
		if (str_eq(a, "--card")) {
			flags |= 4;
			continue;
		}
		if (i >= argc) /* les options à valeur */
			goto usage;
		char *v = argv[i++];
		if (str_eq(a, "-t")) {
			for (char *p = v; *p; p++)
				if ((u8)(*p - 'A') <= 25)
					*p += 32;
			if (str_eq(v, "auto") || str_eq(v, "fat32") || str_eq(v, "vfat"))
				continue;
			err_say("mkfs : seul FAT32 est pris en charge (-t auto ou fat32)\n");
			return 2;
		}
		if (str_eq(a, "-L")) { /* 11 caractères au plus */
			if (str_len(v) > 11) {
				err_say("mkfs : étiquette trop longue (11 caractères au plus)\n");
				return 2;
			}
			str_cat(label, v);
			continue;
		}
		if (!str_eq(a, "-c"))
			goto usage;
		u64 c = parse_dec(v); /* « 4096 », « 4k », « 32K » (comme l'asm : la dernière lettre) */
		if ((v[str_len(v) - 1] | 0x20) == 'k')
			c <<= 10;
		opt_cluster = c;
	}
	if (!vol || !vu_name(vol))
		goto usage;
	if (str_eq(vu_vol, "sd0") || str_eq(vu_vol, "sd")) /* SD0: (ou SD:) : jamais */
		return refuse(" est le volume du système : il n'est jamais formaté\n");
	if (opt_cluster && ((opt_cluster & (opt_cluster - 1)) || opt_cluster < 512 || opt_cluster > 65536))
		return refuse(" : taille de cluster invalide (512 à 65536, une puissance de 2)\n");
	char *e = vu_find(vu_vol); /* sa source : montée, sinon devinée */
	if (e) {
		str_cat(vu_src, e + VU_SRC);
		str_cat(vu_srv, e + VU_FS);
		mounted = 1;
	} else if (!vu_source(vu_vol)) {
		return refuse(" : volume inconnu (ni monté, ni dans SD0:/etc/mounts.txt, ni une clé USB)\n");
	}
	if (!prefix(vu_src, "blk.")) /* un périphérique bloc */
		return refuse(" n'est pas sur un périphérique bloc\n");
	if (prefix(vu_src, "blk.sd")) { /* la carte : --card */
		if (!(flags & 4))
			return refuse(" est une partition de la carte SD : ajoutez --card\n");
		if (str_eq(vu_src, "blk.sd0p1")) /* (le système, jamais) */
			return refuse(" est le volume du système : il n'est jamais formaté\n");
	}
	long r = korin_sys2(SYS_SVC_OPEN, vu_src, 2000); /* le service bloc */
	if (r < 0)
		return refuse(" : son périphérique ne répond pas\n");
	chan = r;
	if (blk(BLK_INFO, 0, 0, 0) < 0)
		return refuse(" : son périphérique ne répond pas\n");
	if (msg[MSG_A / 8])
		return refuse(" : le périphérique est protégé en écriture\n");
	sectors = msg[MSG_B / 8] <= 0xFFFFFFFFull ? msg[MSG_B / 8] : 0xFFFFFFFFull; /* (2 To au plus) */
	pid_blk = (long)msg[MSG_D / 8];
	if (layout() < 0)
		return refuse(" : trop petit ou trop grand pour FAT32 avec ces clusters (65 525 clusters au moins : 33 Mo)\n");
	if (!(flags & 1)) { /* « Tout ce qui est sur USB0: (blk.usb0p1, 1,9 Go) sera effacé. » */
		out_say("Tout ce qui est sur ");
		out_puts(vu_aff);
		out_puts(" (");
		out_puts(vu_src);
		out_puts(", ");
		vu_size(sectors << 9);
		out_say(") sera effacé.\n");
		out_say("Tapez le nom du volume (");
		out_puts(vu_aff);
		out_say(") pour le formater : ");
		out_flush();
		rd_init(&krt_in, 0); /* la réponse, sur l'entrée standard */
		char *l = rd_line(&krt_in, 0);
		if (!l)
			goto no;
		char keep[16]; /* même nom ? (vu_name le récrit : copie) */
		memcpy(keep, vu_aff, 16);
		vu_name(l);
		int same = str_eq(vu_aff, keep) != 0;
		vu_name(keep); /* (vu_vol et vu_aff rendus) */
		if (!same)
			goto no;
	}
	if (mounted) { /* démonté d'abord, ses fichiers écrits */
		r = vu_sync(vu_vol);
		if (r > 0 && !(flags & 2)) {
			err_not_formatted(E_BUSY_VU);
			err_say("(fermez ses fichiers, ou ajoutez -f)\n");
			return 1;
		}
		r = vu_unmount(vu_vol);
		if (r < 0) {
			err_not_formatted(r);
			return 1;
		}
	}
	out_say("Formatage de ");
	out_puts(vu_aff);
	out_puts("...\n");
	out_flush();
	long f = format();
	long m = vu_mount(vu_aff, vu_src, vu_srv); /* remonté, même en cas d'échec */
	if (f < 0) {
		out_nl();
		err_not_formatted(f);
		return 1;
	}
	out_puts(vu_aff);
	out_say(" formaté : ");
	if (m < 0) {
		out_nl();
		err_puts(vu_aff);
		err_say(" : formaté, mais pas remonté : ");
		vu_err(m);
		err_puts("\n");
		return 1;
	}
	if (vu_info(vu_vol) >= 0) {
		u64 *q = (u64 *)vu_rep;
		out_puts(vu_type_name(q[MSG_D / 8]));
		out_puts(", ");
		vu_size(q[MSG_A / 8]);
		out_say(", clusters de ");
		out_putn(q[MSG_C / 8]);
		out_say(" octets");
		char *lab = (char *)vu_rep + MSG_DATA;
		if (*lab) {
			out_puts(", « ");
			lab[63] = 0;
			out_puts(lab);
			out_puts(" »");
		}
	}
	out_nl();
	return 0;
no:
	err_say("mkfs : pas formaté\n");
	return 1;
usage:
	err_say("usage : mkfs [-t auto|fat32] [-L étiquette] [-c octets] [-y] [-f] [--card] VOLUME:\n");
	return 2;
}
