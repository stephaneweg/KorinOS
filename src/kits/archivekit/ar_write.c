/* ar_write.c - créer une archive : ZIP, tar, tar.gz, gz, dans un fichier ou
 * en mémoire (d'après ar_write.inc). Tout est écrit à la suite. ZIP : en-tête
 * local avec des champs provisoires, données (deflate ou stockées), puis
 * l'en-tête est corrigé ; aucun extra, aucun descripteur de données : un
 * mimetype d'ODF ajouté en premier au niveau 0 est bien à l'octet 30, sans rien
 * autour. Une entrée de mémoire que deflate ne réduit pas est stockée. */
#include "ar.h"

#define WR_NAMEMAX 3900
#define WR_SIZE (8192 - 16)
#define AR_NFORMATS 4

#define ZIP_LOCAL 0x04034B50u
#define ZIP_CENTRAL 0x02014B50u
#define ZIP_END 0x06054B50u

struct writer {
	u64 magic, data, size;
	u64 fmt;
	struct defl *df;  /* état de deflate */
	struct sink sink; /* le puits de l'archive */
	struct sink cd;   /* le répertoire central, en mémoire */
	u64 n;            /* entrées */
	u64 time;         /* date imposée (0 : l'heure courante) */
	u64 done;
	u64 tarpos; /* octets du tar déjà écrits */
	u64 eoff;   /* ZIP : en-tête local de l'entrée en cours */
	u64 edata;  /* ZIP : début de ses données */
	u64 esize;  /* octets reçus pour l'entrée */
	u64 ecrc;
	u64 elevel;
	u64 etime; /* date DOS (ZIP), secondes (tar) */
	u64 enlen; /* longueur du nom */
	u64 eunix; /* date, secondes */
	u64 edir;
	u8 pad[512 - 208];
	u8 name[4096]; /* le nom propre de l'entrée en cours */
};

static const char s_longlink[] = "././@LongLink";
static const char ar_fmtnames[] = "ZIP\0TAR\0TAR.GZ\0GZIP";
static const char ar_extensions[] = "zip jar docx xlsx pptx odt ods odp epub apk\0"
				    "tar\0"
				    "tar.gz tgz\0"
				    "gz";
static const char ar_errors[] = "\x02"
				"fichier ou dossier introuvable\0"
				"\x05"
				"lecture ou ecriture impossible\0"
				"\x0C"
				"plus de memoire\0"
				"\x0D"
				"entree chiffree : pas prise en charge\0"
				"\x16"
				"argument invalide\0"
				"\x1B"
				"trop grand pour ce format\0"
				"\x1C"
				"plus de place\0"
				"\x26"
				"methode de compression inconnue\0"
				"\x4A"
				"donnees abimees ou tronquees\0"
				"\x54"
				"somme de controle fausse\0"
				"\x5F"
				"format inconnu\0";
static const char s_err_unk[] = "erreur inconnue";

static inline void wr16(u8 *p, u64 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void wr32(u8 *p, u64 v)
{
	wr16(p, v);
	wr16(p + 2, v >> 16);
}

/* poignée -> écrivain non terminé, 0 sinon */
static struct writer *wr_handle(long h)
{
	if (h <= 0)
		return 0;
	struct writer *w = (struct writer *)h;
	if (w->magic != AR_MAGIC_WR || w->done)
		return 0;
	return w;
}

/* -> état de deflate, 0 si plus de mémoire */
static struct defl *wr_df(struct writer *w)
{
	if (!w->df)
		w->df = df_new(&w->sink);
	return w->df;
}

/* format, genre de puits, descripteur -> écrivain, erreur */
static long wr_new(long fmt, u64 kind, long fd)
{
	if ((u64)(fmt - 1) > AR_FMT_GZIP - 1)
		return AR_E_INVAL;
	struct writer *w = ar_alloc(WR_SIZE);
	if (!w)
		return AR_E_NOMEM;
	w->magic = AR_MAGIC_WR;
	w->fmt = (u64)fmt;
	w->sink.kind = kind;
	if (kind == SKK_FD)
		w->sink.ptr = (u64)fd;
	w->cd.kind = SKK_GROW;
	if (fmt == AR_FMT_TGZ) {
		/* un seul flux deflate pour tout le tar */
		struct defl *s = wr_df(w);
		if (!s)
			return AR_E_NOMEM;
		df_begin(s, AR_DEFAULT, AR_GZIP);
		long r = wrap_head(&w->sink, AR_GZIP, AR_DEFAULT);
		if (r) {
			wr_free(w);
			return r;
		}
	}
	return (long)w;
}

long ar_create(long path, long fmt)
{
	if ((u64)(fmt - 1) > AR_FMT_GZIP - 1)
		return AR_E_INVAL;
	long fd = f_ouvrir(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	long w = wr_new(fmt, SKK_FD, fd);
	if (w < 0)
		f_fermer(fd);
	return w;
}

long ar_create_mem(long fmt)
{
	return wr_new(fmt, SKK_GROW, 0);
}

/* écrivain -> 0 */
long wr_free(void *p)
{
	struct writer *w = p;
	w->magic = 0;
	if (w->sink.kind == SKK_FD) {
		if (!w->done)
			f_fermer((long)w->sink.ptr);
	} else {
		ar_mfree((u8 *)w->sink.ptr);
	}
	ar_mfree((u8 *)w->cd.ptr);
	ar_mfree(w->df);
	ar_mfree(w);
	return 0;
}

long ar_set_time(long h, long t)
{
	struct writer *w = wr_handle(h);
	if (!w)
		return AR_E_INVAL;
	w->time = (u64)t;
	return 0;
}

/* secondes UTC -> date DOS (date << 16 | heure) ; avant 1980 : 1980-01-01 */
static u64 unix_dos(u64 t)
{
	if (t < 315532800)
		return (u64)0x21 << 16;
	u64 days = t / 86400, secs = t % 86400;
	/* jours -> année, mois, jour */
	u64 z = days + 719468;
	u64 era = z / 146097;
	u64 doe = z - era * 146097; /* jour de l'ère */
	u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* année de l'ère */
	u64 y = era * 400 + yoe;
	u64 doy = doe - (yoe * 365 + (yoe >> 2) - yoe / 100); /* jour de l'année (depuis mars) */
	u64 mp = (doy * 5 + 2) / 153;                           /* mois depuis mars */
	u64 d = doy - (mp * 153 + 2) / 5 + 1;                   /* jour */
	u64 m = mp + 3;
	if (m > 12) {
		m -= 12;
		y++;
	}
	y -= 1980;
	if (y > 127)
		y = 127;
	u64 r = y << 25 | m << 21 | d << 16;
	r |= (secs / 3600) << 11;
	secs %= 3600;
	r |= (secs / 60) << 5;
	r |= (secs % 60) >> 1;
	return r;
}

/* nom -> name, enlen ; 0, erreur. Sans / au début, \ -> /, sans / à la fin */
static long wr_name(struct writer *w, const char *nm)
{
	if (!nm)
		return AR_E_INVAL;
	u8 *b = w->name, *o = b;
	for (;;) {
		if (*nm == '/' || *nm == '\\') {
			nm++;
			continue;
		}
		if (*nm == '.' && nm[1] == '/') {
			nm += 2;
			continue;
		}
		break;
	}
	for (;;) {
		u8 c = (u8)*nm++;
		if (!c)
			break;
		if (c == '\\')
			c = '/';
		*o++ = c;
		if (o - b >= WR_NAMEMAX)
			return AR_E_INVAL;
	}
	for (;;) {
		if (o == b)
			return AR_E_INVAL;
		if (o[-1] != '/')
			break;
		o--;
	}
	*o = 0;
	w->enlen = (u64)(o - b);
	return 0;
}

/* eunix et etime (t = secondes, 0 : maintenant) */
static void wr_stamp(struct writer *w, u64 t)
{
	if (!t) {
		t = w->time;
		if (!t) {
			long c = korin_sys1(SYS_CLOCK_GET, 0);
			t = c > 0 ? (u64)c : 0;
		}
	}
	w->eunix = t;
	w->etime = unix_dos(t);
}

/* données, octets : à l'archive (à travers le gzip pour un tar.gz) -> 0, erreur */
static long wr_out(struct writer *w, const void *p, u64 n)
{
	if (w->fmt == AR_FMT_TGZ)
		return df_feed(w->df, p, n);
	return sink_write(&w->sink, p, n);
}

/* --- tar ------------------------------------------------------------------------ */

/* valeur, champ, chiffres : octal, complété par des 0, puis un 0 final */
static void tar_oct(u64 v, u8 *f, u64 n)
{
	f[n] = 0;
	while (n) {
		n--;
		f[n] = (u8)('0' + (v & 7));
		v >>= 3;
	}
}

/* en-tête (512 o, nom déjà en place), taille, date, type (lettre) -> en-tête
 * complet, à écrire */
static void tar_fill(u8 *h, u64 size, u64 time, u64 type)
{
	tar_oct(type == '5' ? 493 : 420, h + 100, 7); /* 0755, 0644 */
	tar_oct(0, h + 108, 7);
	tar_oct(0, h + 116, 7);
	tar_oct(size, h + 124, 11);
	tar_oct(time, h + 136, 11);
	h[156] = (u8)type;
	memcpy(h + 257, "ustar\0" "00", 8);
	/* somme, le champ compte pour des blancs */
	memset(h + 148, ' ', 8);
	u64 sum = 0;
	for (int i = 0; i < 512; i++)
		sum += h[i];
	tar_oct(sum, h + 148, 6);
	h[155] = ' ';
}

/* octets -> bourrage jusqu'à 512 */
static inline u64 tar_pad_of(u64 n)
{
	return (0 - n) & 511;
}

/* des zéros à l'archive -> 0, erreur */
static long wr_zeros(struct writer *w, u64 n)
{
	u8 z[512];
	memset(z, 0, 512);
	while (n) {
		u64 c = n < 512 ? n : 512;
		n -= c;
		long r = wr_out(w, z, c);
		if (r)
			return r;
	}
	return 0;
}

/* taille, type : l'en-tête de l'entrée name -> 0, erreur */
static long tar_head(struct writer *w, u64 size, u64 type)
{
	u8 h[512];
	memset(h, 0, 512);
	u8 *nm = w->name;
	u64 len = w->enlen;
	long r;
	if (type == '5') { /* (les dossiers finissent par /) */
		nm[len++] = '/';
		nm[len] = 0;
	}
	if (len <= 100)
		goto plain;
	{
		/* préfixe : couper à un / qui laisse au plus 155 + 100 */
		u64 c = len > 155 ? 155 : len; /* le dernier / possible */
		for (;;) {
			if (!c)
				goto lng;
			c--;
			if (nm[c] == '/')
				break;
		}
		u64 rest = len - c - 1;
		if (rest > 100 || !rest)
			goto lng;
		ar_memcpy(h + 345, nm, c);
		ar_memcpy(h, nm + c + 1, rest);
		goto fill;
	}
lng: /* nom long de GNU : un en-tête L, le nom, puis l'en-tête tronqué */
	ar_memcpy(h, s_longlink, 14);
	tar_fill(h, len + 1, 0, 'L');
	r = wr_out(w, h, 512);
	if (r)
		return r;
	r = wr_out(w, nm, len + 1);
	if (r)
		return r;
	{
		u64 pad = tar_pad_of(len + 1);
		r = wr_zeros(w, pad);
		if (r)
			return r;
		w->tarpos += len + 1 + 512 + pad;
	}
	memset(h, 0, 512);
	len = 100;
plain:
	ar_memcpy(h, nm, len);
fill:
	tar_fill(h, size, w->eunix, type);
	r = wr_out(w, h, 512);
	if (r)
		return r;
	w->tarpos += 512;
	return 0;
}

/* --- une entrée, en trois temps ------------------------------------------------------ */

/* -> drapeaux de l'entrée name : bit 11 si le nom n'est pas ASCII */
static u64 zip_flags(struct writer *w)
{
	for (const u8 *p = w->name; *p; p++)
		if (*p >= 0x80)
			return 1 << 11;
	return 0;
}

/* niveau : l'en-tête local provisoire et le nom -> 0, erreur */
static long zip_local(struct writer *w, u64 level)
{
	u8 h[32];
	wr32(h, ZIP_LOCAL);
	wr16(h + 4, 20);
	wr16(h + 6, zip_flags(w));
	wr16(h + 8, level ? 8 : 0);
	wr32(h + 10, w->etime);
	wr32(h + 14, 0);
	wr32(h + 18, 0);
	wr32(h + 22, 0);
	wr16(h + 26, w->enlen);
	wr16(h + 28, 0);
	long r = sink_write(&w->sink, h, 30);
	if (r)
		return r;
	return sink_write(&w->sink, w->name, w->enlen);
}

/* méthode, octets compressés : la fiche du répertoire central -> 0, erreur */
static long zip_central(struct writer *w, u64 method, u64 packed)
{
	u8 h[64];
	wr32(h, ZIP_CENTRAL);
	wr16(h + 4, 0x031E); /* Unix, version 3.0 */
	wr16(h + 6, 20);
	wr16(h + 8, zip_flags(w));
	wr16(h + 10, method);
	wr32(h + 12, w->etime);
	wr32(h + 16, w->ecrc);
	wr32(h + 20, packed);
	wr32(h + 24, w->esize);
	wr16(h + 28, w->enlen);
	wr16(h + 30, 0);
	wr16(h + 32, 0);
	wr16(h + 34, 0);
	wr16(h + 36, 0);
	wr32(h + 38, w->edir ? 0x41ED0010u : 0x81A40000u); /* drwxr-xr-x, -rw-r--r-- */
	wr32(h + 42, w->eoff);
	long r = sink_write(&w->cd, h, 46);
	if (r)
		return r;
	r = sink_write(&w->cd, w->name, w->enlen);
	if (r)
		return r;
	w->n++;
	return 0;
}

/* l'en-tête gzip, avec le nom et la date -> 0, erreur */
static long gz_head(struct writer *w)
{
	u8 h[16];
	h[0] = 0x1F;
	h[1] = 0x8B;
	h[2] = 8;
	h[3] = 8; /* FNAME */
	wr32(h + 4, w->eunix);
	h[8] = 0;
	h[9] = 3;
	long r = sink_write(&w->sink, h, 10);
	if (r)
		return r;
	return sink_write(&w->sink, w->name, w->enlen + 1);
}

/* name prêt, eunix, taille annoncée, niveau -> 0, erreur */
static long wr_begin(struct writer *w, u64 size, u64 level)
{
	w->elevel = level;
	w->esize = 0;
	w->ecrc = 0;
	w->edir = 0;
	if (w->fmt == AR_FMT_ZIP) {
		if (size >= 0xFFFFFFFFu || w->sink.pos >= 0xFFFFFFFFu)
			return AR_E_FBIG;
		w->eoff = w->sink.pos;
		long r = zip_local(w, level);
		if (r)
			return r;
		w->edata = w->sink.pos;
		if (level) {
			struct defl *s = wr_df(w);
			if (!s)
				return AR_E_NOMEM;
			df_begin(s, level, AR_RAW);
		}
		return 0;
	}
	if (w->fmt == AR_FMT_GZIP) {
		if (w->n)
			return AR_E_INVAL;
		w->n = 1;
		long r = gz_head(w);
		if (r)
			return r;
		struct defl *s = wr_df(w);
		if (!s)
			return AR_E_NOMEM;
		df_begin(s, level, AR_GZIP);
		return 0;
	}
	if (size >= (u64)1 << 33)
		return AR_E_FBIG;
	return tar_head(w, size, '0');
}

/* données, octets -> 0, erreur */
static long wr_write(struct writer *w, const u8 *p, u64 n)
{
	w->esize += n;
	if (w->fmt == AR_FMT_ZIP) {
		w->ecrc = crc32_upd((u32)w->ecrc, p, n);
		if (!w->elevel)
			return sink_write(&w->sink, p, n);
		return df_feed(w->df, p, n);
	}
	if (w->fmt == AR_FMT_GZIP)
		return df_feed(w->df, p, n);
	long r = wr_out(w, p, n);
	if (r)
		return r;
	w->tarpos += n;
	return 0;
}

/* position, valeur, octets : corrige ce qui est déjà écrit -> 0, erreur */
static long zip_patch(struct writer *w, u64 pos, u64 v, u64 n)
{
	u8 b[8];
	for (int i = 0; i < 8; i++)
		b[i] = (u8)(v >> (8 * i));
	if (w->sink.kind != SKK_FD) {
		ar_memcpy((u8 *)w->sink.ptr + pos, b, n);
		return 0;
	}
	long r = f_position((long)w->sink.ptr, (long)pos, 0);
	if (r < 0)
		return r;
	r = f_ecrire((long)w->sink.ptr, (long)b, (long)n);
	if (r < 0)
		return r;
	return 0;
}

/* taille annoncée (tar), les données si elles sont en mémoire (0 : non), leur
 * taille -> 0, erreur */
static long wr_end(struct writer *w, u64 size, const u8 *data, u64 dlen)
{
	long r;
	if (w->fmt == AR_FMT_GZIP)
		return 0;
	if (w->fmt != AR_FMT_ZIP) {
		if (w->esize != size)
			return AR_E_IO; /* le fichier a changé sous nos yeux */
		u64 pad = tar_pad_of(size);
		r = wr_zeros(w, pad);
		if (r)
			return r;
		w->tarpos += pad;
		return 0;
	}
	u64 method = 0;
	if (w->elevel) {
		method = 8;
		r = df_finish(w->df);
		if (r)
			return r;
		/* pas plus petit : stocker, si les données sont là et qu'on peut revenir
		 * en arrière */
		if (data) {
			u64 comp = w->sink.pos - w->edata;
			if (comp >= dlen) {
				if (w->sink.kind == SKK_FD) {
					if (comp - dlen > 64) /* (le répertoire central recouvrira l'excès) */
						goto close;
					r = f_position((long)w->sink.ptr, (long)w->edata, 0);
					if (r < 0)
						return r;
				}
				w->sink.pos = w->edata;
				r = sink_write(&w->sink, data, dlen);
				if (r)
					return r;
				method = 0;
			}
		}
	}
close: { /* l'en-tête local corrigé */
	u64 end = w->sink.pos;
	u64 packed = end - w->edata;
	if (end >= 0xFFFFFFFFu)
		return AR_E_FBIG;
	r = zip_patch(w, w->eoff + 8, method, 2);
	if (r)
		return r;
	r = zip_patch(w, w->eoff + 14, w->ecrc, 4);
	if (r)
		return r;
	r = zip_patch(w, w->eoff + 18, packed, 4);
	if (r)
		return r;
	r = zip_patch(w, w->eoff + 22, w->esize, 4);
	if (r)
		return r;
	if (w->sink.kind == SKK_FD) {
		r = f_position((long)w->sink.ptr, (long)end, 0);
		if (r < 0)
			return r;
	}
	return zip_central(w, method, packed);
}
}

/* --- l'interface ------------------------------------------------------------------ */

long ar_add_mem(long h, long name, long data, long n, long level)
{
	struct writer *w = wr_handle(h);
	if (!w)
		return AR_E_INVAL;
	long lv = ar_level_of(level);
	if (lv < 0)
		return lv;
	long r = wr_name(w, (const char *)name);
	if (r)
		return r;
	wr_stamp(w, 0);
	r = wr_begin(w, (u64)n, (u64)lv);
	if (r)
		return r;
	r = wr_write(w, (const u8 *)data, (u64)n);
	if (r)
		return r;
	return wr_end(w, (u64)n, (const u8 *)data, (u64)n);
}

/* name, eunix : une entrée dossier -> 0, erreur */
static long wr_dir(struct writer *w)
{
	if (w->fmt == AR_FMT_GZIP)
		return AR_E_INVAL;
	if (w->fmt != AR_FMT_ZIP)
		return tar_head(w, 0, '5');
	/* ZIP : le nom finit par / */
	w->name[w->enlen++] = '/';
	w->name[w->enlen] = 0;
	long r = wr_begin(w, 0, 0);
	if (r)
		return r;
	w->edir = 1;
	return wr_end(w, 0, 0, 0);
}

long ar_add_dir(long h, long name)
{
	struct writer *w = wr_handle(h);
	if (!w)
		return AR_E_INVAL;
	long r = wr_name(w, (const char *)name);
	if (r)
		return r;
	wr_stamp(w, 0);
	return wr_dir(w);
}

/* chemin, {taille, attributs, date} -> 0, erreur */
static long ar_stat(const char *path, u64 *st)
{
	u64 m[MSG_TAILLE / 8];
	u8 *msg = (u8 *)m;
	m[0] = V_STAT;
	m[1] = 0;
	m[2] = 0;
	m[3] = 0;
	m[4] = 0;
	u8 *d = msg + MSG_DATA;
	u64 i = 0;
	for (;;) {
		if (i >= MSG_TAILLE - MSG_DATA - 1)
			return AR_E_INVAL;
		u8 c = (u8)path[i];
		d[i++] = c;
		if (!c)
			break;
	}
	*(u32 *)(msg + MSG_LEN) = (u32)i;
	long r = ar_vfs(msg);
	if (r < 0)
		return r;
	st[0] = m[MSG_B / 8];
	st[1] = m[MSG_C / 8];
	st[2] = m[MSG_D / 8];
	return 0;
}

/* destination (2048 o au plus), dossier, nom -> 0, erreur */
static long wr_join(char *dst, const char *dir, const char *nm)
{
	char *o = dst;
	for (;;) {
		char c = *dir++;
		if (!c)
			break;
		*o++ = c;
		if (o - dst >= 1500)
			return AR_E_INVAL;
	}
	if (o > dst && o[-1] != '/' && o[-1] != ':')
		*o++ = '/';
	for (;;) {
		char c = *nm++;
		*o++ = c;
		if (!c)
			break;
	}
	return 0;
}

/* nom, chemin, niveau, profondeur -> 0, erreur */
static long wr_path(struct writer *w, const char *nm, const char *path, u64 level, u64 depth)
{
	u64 st[4];
	long r;
	if (depth >= 32)
		return AR_E_INVAL;
	r = ar_stat(path, st);
	if (r)
		return r;
	r = wr_name(w, nm);
	if (r)
		return r;
	wr_stamp(w, st[2]);
	if (!(st[1] & ATTR_DIR)) {
		/* un fichier */
		long fd = f_ouvrir((long)path, O_READ);
		if (fd < 0)
			return fd;
		u64 size = st[0];
		r = wr_begin(w, size, level);
		if (!r) {
			u8 *buf = ar_alloc(65536);
			if (!buf) {
				r = AR_E_NOMEM;
			} else {
				for (;;) {
					long got = f_lire(fd, (long)buf, 65536);
					if (got <= 0) {
						r = got < 0 ? got : wr_end(w, size, 0, 0);
						break;
					}
					r = wr_write(w, buf, (u64)got);
					if (r)
						break;
				}
				ar_mfree(buf);
			}
		}
		f_fermer(fd);
		return r;
	}
	/* un dossier : son entrée, puis ce qu'il contient */
	r = wr_dir(w);
	if (r)
		return r;
	char *blk = ar_alloc(12288); /* DIRENT, nom suivant, chemin suivant */
	if (!blk)
		return AR_E_NOMEM;
	long fd = f_ouvrir((long)path, O_READ);
	if (fd < 0) {
		r = fd;
	} else {
		for (;;) {
			r = f_lire_rep(fd, (long)blk);
			if (r < 0)
				break;
			if (!r)
				break;
			/* . et .. */
			if (blk[0] == '.' && (!blk[1] || (blk[1] == '.' && !blk[2])))
				continue;
			r = wr_join(blk + 1024, nm, blk);
			if (r)
				break;
			r = wr_join(blk + 4096, path, blk);
			if (r)
				break;
			r = wr_path(w, blk + 1024, blk + 4096, level, depth + 1);
			if (r)
				break;
		}
		f_fermer(fd);
	}
	ar_mfree(blk);
	return r;
}

long ar_add_file(long h, long name, long path, long level)
{
	struct writer *w = wr_handle(h);
	if (!w || !path)
		return AR_E_INVAL;
	long lv = ar_level_of(level);
	if (lv < 0)
		return lv;
	const char *nm = (const char *)name;
	if (!nm) {
		/* le nom de base */
		const char *p = (const char *)path;
		nm = p;
		for (;;) {
			char c = *p++;
			if (!c)
				break;
			if ((c == '/' || c == ':') && *p)
				nm = p;
		}
	}
	return wr_path(w, nm, (const char *)path, (u64)lv, 0);
}

long ar_finish(long h)
{
	struct writer *w = wr_handle(h);
	long r;
	if (!w)
		return AR_E_INVAL;
	if (w->fmt == AR_FMT_ZIP) {
		u64 cpos = w->sink.pos; /* position du répertoire */
		u64 csize = w->cd.pos;  /* sa taille */
		u64 n = w->n;
		if (n >= 0xFFFF || cpos + csize >= 0xFFFFFFFFu)
			return AR_E_FBIG;
		r = sink_write(&w->sink, (const u8 *)w->cd.ptr, csize);
		if (r)
			return r;
		u8 e[32];
		wr32(e, ZIP_END);
		wr32(e + 4, 0);
		wr16(e + 8, n);
		wr16(e + 10, n);
		wr32(e + 12, csize);
		wr32(e + 16, cpos);
		wr16(e + 20, 0);
		r = sink_write(&w->sink, e, 22);
		if (r)
			return r;
		goto done;
	}
	if (w->fmt == AR_FMT_GZIP) {
		if (!w->n) {
			/* aucune entrée : un gz vide, sans nom */
			r = wrap_head(&w->sink, AR_GZIP, AR_DEFAULT);
			if (r)
				return r;
			struct defl *s = wr_df(w);
			if (!s)
				return AR_E_NOMEM;
			df_begin(s, AR_DEFAULT, AR_GZIP);
		}
		goto gzend;
	}
	{
		/* tar : deux blocs de zéros, puis jusqu'à un enregistrement de 10240 octets */
		u64 x = w->tarpos + 1024;
		u64 pad = (10240 - x % 10240) % 10240;
		r = wr_zeros(w, pad + 1024);
		if (r)
			return r;
		if (w->fmt != AR_FMT_TGZ)
			goto done;
	}
gzend:
	r = df_finish(w->df);
	if (r)
		return r;
	r = wrap_tail(&w->sink, w->df);
	if (r)
		return r;
done:
	w->done = 1;
	w->data = w->sink.ptr;
	w->size = w->sink.pos;
	if (w->sink.kind == SKK_FD) {
		f_fermer((long)w->sink.ptr);
		w->data = 0;
		return (long)w->size;
	}
	r = sink_zero(&w->sink); /* un 0 suit les octets */
	if (r < 0)
		return r;
	w->data = w->sink.ptr;
	return (long)w->size;
}

/* --- questions au kit ------------------------------------------------------------- */

long ar_format_name(long f)
{
	u64 i = (u64)f - 1;
	if (i >= AR_NFORMATS)
		return 0;
	const char *p = ar_fmtnames;
	while (i) {
		while (*p++)
			;
		i--;
	}
	return (long)p;
}

long ar_formats(long arr, long places)
{
	if (!arr)
		return AR_NFORMATS;
	for (u64 i = 0; i < AR_NFORMATS && i < (u64)places; i++) {
		u8 *rec = (u8 *)arr + i * ARFI_SIZE;
		u64 *q = (u64 *)rec;
		q[0] = i + 1;
		q[1] = 1;
		q[2] = 1;
		const char *s = (const char *)ar_format_name((long)i + 1);
		u8 *d = rec + 24;
		do
			*d = (u8)*s++;
		while (*d++);
		s = ar_extensions;
		for (u64 k = i; k; k--)
			while (*s++)
				;
		d = rec + 40;
		do
			*d = (u8)*s++;
		while (*d++);
	}
	return AR_NFORMATS;
}

long ar_strerror(long e)
{
	u32 want = (u32)(0 - (u32)e); /* -erreur */
	const char *p = ar_errors;
	for (;;) {
		u8 c = (u8)*p++;
		if (!c)
			return (long)s_err_unk;
		if (c == want)
			return (long)p;
		while (*p++)
			;
	}
}
