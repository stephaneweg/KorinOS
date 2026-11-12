/* uk_image.c -- UIKit : les images (BMP, PNG) et leur widget (uikit_image.inc).
 * Port ligne à ligne de l'assembleur (src/kits/uikit/PORT.md).
 *
 * Une image (poignée) : +0 largeur, +8 hauteur, +16 pixels (largeur x hauteur
 * x 4 octets, 0xAARRGGBB, AA = opacité 255). Décodées dans le kit :
 *  - BMP : non compressé 24 / 32 bits (BI_RGB, BI_BITFIELDS en BGRA), 8, 4 et
 *    1 bit avec palette ; de bas en haut ou de haut en bas ;
 *  - PNG : gris, RVB, palette, gris + alpha, RVBA, profondeurs 1, 2, 4, 8, 16
 *    (octet fort), tRNS, filtres 0-4 ; pas l'entrelacement Adam7 (refusé) ;
 *    zlib / deflate (blocs stockés, Huffman fixe et dynamique) : uk_inflate,
 *    état dans uk.inf.
 * ui_image_draw met une image à l'échelle (plus proche voisin) et la mélange
 * par son alpha, dans la découpe courante. Widget : ui_imagebox. */
#include "uk.h"

/* x0 = largeur, x1 = hauteur -> image vide (0 : échec) */
long uk_img_new(long w, long h)
{
	if (!w || !h)
		return 0;
	if (UGT(w, 16384) || UGT(h, 16384))
		return 0;
	long p = uk_alloc(((w * h) << 2) + IMG_PIX);
	if (!p)
		return 0;
	STX(p, IMG_W, w);
	STX(p, IMG_H, h);
	return p;
}

/* x0 = image (0 : rien) : rendue */
long ui_image_free(long a0)
{
	return uk_free(a0);
}

/* x0 = image -> x0 = largeur, x1 = hauteur, x2 = pixels */
ukr ui_image_size(long a0)
{
	ukr r;
	if (!a0) {
		r.r0 = 0;
		r.r1 = 0;
		r.r2 = 0;
		r.r3 = 0;
		return r;
	}
	r.r0 = LDX(a0, IMG_W);
	r.r1 = LDX(a0, IMG_H);
	r.r2 = a0 + IMG_PIX;
	r.r3 = a0 + IMG_PIX;
	return r;
}

/* x0 = chemin -> image (0 : illisible ou format inconnu) */
long ui_image_load(long a0)
{
	long st[2];
	if (SYS2(SYS_STAT, a0, (long)(uintptr_t)st))
		return 0;
	long size = st[0];
	if (!size)
		return 0;
	long buf = uk_alloc(size);
	if (!buf)
		return 0;
	long fd = SYS2(SYS_OPEN, a0, O_READ);
	if (fd >= 0) {
		long n = SYS3(SYS_READ, fd, buf, size);
		SYS1(SYS_CLOSE, fd);
		if (n == size) {
			long img = ui_image_decode(buf, size);
			uk_free(buf);
			return img;
		}
	}
	uk_free(buf);
	return 0;
}

static long bmp_decode(long data, long size);
static long png_decode(long data, long size);

/* x0 = données, x1 = octets -> image (BMP ou PNG d'après la signature ;
 * 0 : inconnu ou invalide) */
long ui_image_decode(long a0, long a1)
{
	if (ULT(a1, 26))
		return 0;
	if (LDH(a0, 0) == 0x4D42)                  /* « BM » */
		return bmp_decode(a0, a1);
	if (LDW(a0, 0) == 0x474E5089)              /* 89 « PNG » */
		return png_decode(a0, a1);
	return 0;
}

/* ---------------------------------------------------------------------------
 *  BMP
 * ------------------------------------------------------------------------- */

/* uk_bmp_decode : x0 = données, x1 = octets -> image (0 : non pris en charge) */
static long bmp_decode(long data, long size)
{
	long off = LDW(data, 10);                  /* début des pixels */
	long hdr = LDW(data, 14);                  /* taille de l'en-tête */
	long w = LDSW(data, 18);                   /* largeur */
	long h = LDSW(data, 22);                   /* hauteur (< 0 : de haut en bas) */
	long bpp = LDH(data, 28);                  /* bits par pixel */
	long comp = LDW(data, 30);                 /* compression */
	if (UGT(comp, 3))
		return 0;
	/* comme l'asm : compression 1, 2 ou 3 acceptée en 32 bits (lue brute) */
	if (comp != 0 && bpp != 32)
		return 0;
	long topdown = 0;                          /* 1 : de haut en bas */
	if (h < 0) {
		h = -h;
		topdown = 1;
	}
	if (w <= 0)
		return 0;
	long img = uk_img_new(w, h);
	if (!img)
		return 0;
	long pitch = LSR(w * bpp + 31, 5) << 2;    /* pas d'une ligne : multiple de 4 */
	if (UGT(pitch * h + off, size)) {          /* tout dans le fichier ? */
		uk_free(img);
		return 0;
	}
	long pal = data + 14 + hdr;                /* palette (après l'en-tête) */
	for (long y = 0; ULT(y, h); y++) {
		long fy = topdown ? y : h - 1 - y;     /* ligne du fichier */
		long src = data + fy * pitch + off;
		long dst = img + IMG_PIX + ((y * w) << 2);
		for (long x = 0; ULT(x, w); x++) {
			u32 px;
			long idx;
			if (bpp == 24) {                   /* B, G, R */
				long p = src + x * 3;
				px = (u32)LDB(p, 0) | (u32)LDB(p, 1) << 8 | (u32)LDB(p, 2) << 16 | 0xFF000000u;
				STW(dst, x << 2, px);
				continue;
			}
			if (bpp == 32) {                   /* BGRA (alpha 0 partout : opaque) */
				px = (u32)LDW(src, x << 2);
				if (!(px & 0xFF000000u))
					px |= 0xFF000000u;
				STW(dst, x << 2, px);
				continue;
			}
			if (bpp == 8) {                    /* palette : 8, 4, 1 bits */
				idx = LDB(src, x);
			} else if (bpp == 4) {
				idx = LDB(src, LSR(x, 1));
				if (!(x & 1))
					idx >>= 4;
				idx &= 15;
			} else if (bpp == 1) {
				idx = LDB(src, LSR(x, 3));
				idx = LSRW(idx, 7 - (x & 7)) & 1;
			} else {
				uk_free(img);                  /* (au premier pixel) */
				return 0;
			}
			/* comme l'asm : indice non borné par la palette */
			px = (u32)LDW(pal, idx << 2) | 0xFF000000u;
			STW(dst, x << 2, px);
		}
	}
	return img;
}

/* ---------------------------------------------------------------------------
 *  Inflate (RFC 1951) : lecture bit à bit, codes de Huffman canoniques
 *  (comptes par longueur + symboles triés, décodage par premiers codes)
 * ------------------------------------------------------------------------- */

static const u16 len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
				  35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const u8 len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
				  3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const u16 dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
				   257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
				   12289, 16385, 24577 };
static const u8 dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
				   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const u8 clen_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* uk_inf_bits : x0 = nombre de bits (<= 24) -> ces bits (poids faible
 * d'abord) ; -1 à la fin des données */
static long inf_bits(long n)
{
	long I = UKA(inf);
	long bits = LDX(I, INF_BITS);
	long nb = LDX(I, INF_NB);
	while (ULT(nb, n)) {
		long src = LDX(I, INF_SRC);
		if (UGE(src, LDX(I, INF_END)))
			return -1;                     /* comme l'asm : réserve non rangée */
		long b = LDB(src, 0);
		STX(I, INF_SRC, src + 1);
		bits |= LSL(b, nb);
		nb += 8;
	}
	long v = bits & (LSL(1, n) - 1);
	bits = LSR(bits, n);
	nb -= n;
	STX(I, INF_BITS, bits);
	STX(I, INF_NB, nb);
	return v;
}

/* uk_inf_build : x0 = comptes (16 x 16 bits), x1 = symboles, x2 = longueurs
 * (octets), x3 = nombre de symboles : construit la table */
static void inf_build(long cnt, long sym, long lens, long n)
{
	for (long l = 0; l < 16; l++)              /* comptes à zéro */
		STH(cnt, l << 1, 0);
	for (long i = 0; ULT(i, n); i++) {
		long l = LDB(lens, i);
		STH(cnt, l << 1, LDH(cnt, l << 1) + 1);
	}
	STH(cnt, 0, 0);                            /* (longueur 0 : pas de code) */
	u16 offs[16];                              /* décalages par longueur */
	long sum = 0;
	for (long l = 0; l < 16; l++) {
		offs[l] = (u16)sum;
		sum += LDH(cnt, l << 1);
	}
	for (long i = 0; ULT(i, n); i++) {
		long l = LDB(lens, i);
		if (!l)
			continue;
		long o = offs[l];
		STH(sym, o << 1, i);
		offs[l] = (u16)(o + 1);
	}
}

/* uk_inf_decode : x0 = comptes, x1 = symboles -> symbole (-1 : erreur) */
static long inf_decode(long cnt, long sym)
{
	long code = 0;                             /* code */
	long first = 0;                            /* premier code de la longueur */
	long index = 0;                            /* indice du premier symbole */
	for (long len = 1;; len++) {               /* longueur */
		if (UGT(len, 15))
			return -1;
		long b = inf_bits(1);
		if (b < 0)
			return -1;
		code = b | (code << 1);
		long count = LDH(cnt, len << 1);       /* codes de cette longueur */
		long d = code - first;
		if (ULT(d, count))
			return LDH(sym, (index + d) << 1);
		index += count;
		first = (first + count) << 1;
	}
}

/* uk_inflate : x0 = données zlib, x1 = octets, x2 = sortie, x3 = taille de
 * la sortie -> octets produits (< 0 : erreur) */
static long inflate(long src, long len, long dst, long size)
{
	long I = UKA(inf);
	u8 lens[368];                              /* longueurs (19 puis 32 + 316 octets) */
	long ls = (long)(uintptr_t)lens;
	long l32 = ls + 32;                        /* (après 19 : sp+32 garde les longueurs) */
	src += 2;                                  /* en-tête zlib (méthode 8 : rien à voir) */
	len -= 2;
	STX(I, INF_SRC, src);
	STX(I, INF_END, src + len);
	STX(I, INF_BITS, 0);
	STX(I, INF_NB, 0);
	STX(I, INF_DST, dst);
	STX(I, INF_DEND, dst + size);
	STX(I, INF_DBEG, dst);
	for (;;) {                                 /* .block */
		long final = inf_bits(1);              /* dernier bloc ? */
		if (final < 0)
			return -1;
		long type = inf_bits(2);
		if (type < 0)
			return -1;
		if (type == 0) {                       /* bloc stocké : octets alignés */
			STX(I, INF_BITS, 0);
			STX(I, INF_NB, 0);
			long s = LDX(I, INF_SRC), end = LDX(I, INF_END);
			if (UGT(s + 4, end))
				return -1;
			long n = LDH(s, 0);
			s += 4;
			long d = LDX(I, INF_DST), dend = LDX(I, INF_DEND);
			while (n) {
				if (UGE(s, end) || UGE(d, dend))
					return -1;
				STB(d, 0, LDB(s, 0));
				s++;
				d++;
				n--;
			}
			STX(I, INF_SRC, s);
			STX(I, INF_DST, d);
		} else {
			if (type == 1) {                   /* Huffman fixe */
				long i;
				for (i = 0; i < 288; i++)
					lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
				for (; i < 288 + 30; i++)
					lens[i] = 5;
				inf_build(I + INF_LCNT, I + INF_LSYM, ls, 288);
				inf_build(I + INF_DCNT, I + INF_DSYM, ls + 288, 30);
			} else if (type == 2) {            /* Huffman dynamique */
				long v = inf_bits(5);
				if (v < 0)
					return -1;
				long hlit = v + 257;           /* HLIT */
				v = inf_bits(5);
				if (v < 0)
					return -1;
				long hdist = v + 1;            /* HDIST */
				v = inf_bits(4);
				if (v < 0)
					return -1;
				long hclen = v + 4;            /* HCLEN */
				for (long i = 0; i < 19; i++)  /* longueurs des codes de longueurs */
					lens[i] = 0;
				for (long i = 0; ULT(i, hclen); i++) {
					v = inf_bits(3);
					if (v < 0)
						return -1;
					lens[clen_order[i]] = (u8)v;
				}
				/* (table provisoire dans celle des littéraux) */
				inf_build(I + INF_LCNT, I + INF_LSYM, ls, 19);
				long total = hlit + hdist;     /* longueurs des deux tables, à la suite */
				long i = 0;
				while (ULT(i, total)) {
					long sym = inf_decode(I + INF_LCNT, I + INF_LSYM);
					if (sym < 0)
						return -1;
					if (ULT(sym, 16)) {
						STB(l32, i, sym);
						i++;
						continue;
					}
					long rep = 0;              /* valeur répétée */
					long n;
					if (sym == 16) {
						if (!i)
							return -1;
						rep = LDB(l32, i - 1);
						v = inf_bits(2);
						if (v < 0)
							return -1;
						n = v + 3;
					} else if (sym == 17) {
						v = inf_bits(3);
						if (v < 0)
							return -1;
						n = v + 3;
					} else {
						v = inf_bits(7);
						if (v < 0)
							return -1;
						n = v + 11;
					}
					while (n) {
						if (!ULT(i, total))
							return -1;
						STB(l32, i, rep);
						i++;
						n--;
					}
				}
				inf_build(I + INF_LCNT, I + INF_LSYM, l32, hlit);
				inf_build(I + INF_DCNT, I + INF_DSYM, l32 + hlit, hdist);
			} else {
				return -1;
			}
			for (;;) {                         /* --- littéraux, longueurs / distances */
				long sym = inf_decode(I + INF_LCNT, I + INF_LSYM);
				if (sym < 0)
					return -1;
				if (ULT(sym, 256)) {           /* littéral */
					long d = LDX(I, INF_DST);
					if (UGE(d, LDX(I, INF_DEND)))
						return -1;
					STB(d, 0, sym);
					STX(I, INF_DST, d + 1);
					continue;
				}
				if (sym == 256)
					break;
				sym -= 257;                    /* longueur */
				if (!ULT(sym, 29))
					return -1;
				long n = len_base[sym];
				long e = len_extra[sym];
				if (e) {
					long v = inf_bits(e);
					if (v < 0)
						return -1;
					n += v;
				}
				sym = inf_decode(I + INF_DCNT, I + INF_DSYM);
				if (sym < 0)
					return -1;
				if (!ULT(sym, 30))
					return -1;
				long dist = dist_base[sym];
				e = dist_extra[sym];
				if (e) {
					long v = inf_bits(e);
					if (v < 0)
						return -1;
					dist += v;
				}
				long d = LDX(I, INF_DST), dend = LDX(I, INF_DEND);   /* copie depuis la distance */
				long s = d - dist;
				if (ULT(s, LDX(I, INF_DBEG)))
					return -1;
				while (n) {
					if (UGE(d, dend))
						return -1;
					STB(d, 0, LDB(s, 0));
					s++;
					d++;
					n--;
				}
				STX(I, INF_DST, d);
			}
		}
		if (final)                             /* .next */
			return LDX(I, INF_DST) - LDX(I, INF_DBEG);
	}
}

/* ---------------------------------------------------------------------------
 *  PNG
 * ------------------------------------------------------------------------- */

/* uk_be32 : x0 = adresse -> entier de 32 bits gros-boutiste (zéro-étendu) */
static long be32(long p)
{
	return (long)__builtin_bswap32((u32)LDW(p, 0));
}

/* état de png_decode (la pile de l'asm, en mots de 64 bits) */
#define PS_W 0
#define PS_H 8
#define PS_BITS 16
#define PS_COLOR 24
#define PS_PLTE 32
#define PS_TRNS 40
#define PS_TRNS_LEN 48
#define PS_IDAT 56
#define PS_IDAT_LEN 64
#define PS_RAW 72
#define PS_IMG 80
#define PS_RAW_SIZE 88

/* uk_png_unfilter : x0 = brut (lignes : filtre + pas octets), x1 = pas, x2 =
 * lignes, x3 = octets par pixel : défait les filtres sur place */
static void png_unfilter(long raw, long pitch, long rows, long bpp)
{
	for (long y = 0; ULT(y, rows); y++) {
		long line = raw + y * (pitch + 1);     /* filtre ; line + 1 = la ligne */
		u32 f = (u32)LDB(line, 0);
		line++;
		long prev = y ? line - pitch - 1 : 0;  /* ligne d'avant ; 0 pour la première */
		for (long i = 0; ULT(i, pitch); i++) {
			u32 x = (u32)LDB(line, i);
			u32 a = 0, b = 0, c = 0;           /* gauche, haut, haut gauche */
			if (!ULT(i, bpp))
				a = (u32)LDB(line, i - bpp);
			if (prev) {
				b = (u32)LDB(prev, i);
				if (!ULT(i, bpp))
					c = (u32)LDB(prev, i - bpp);
			}
			if (f == 1) {                      /* Sub */
				x += a;
			} else if (f == 2) {               /* Up */
				x += b;
			} else if (f == 3) {               /* Average */
				x += (a + b) >> 1;
			} else if (f == 4) {               /* Paeth : p = a + b - c */
				s32 p = (s32)(a + b - c);
				s32 pa = p - (s32)a, pb = p - (s32)b, pc = p - (s32)c;
				if (pa < 0)
					pa = -pa;
				if (pb < 0)
					pb = -pb;
				if (pc < 0)
					pc = -pc;
				u32 pr = a;
				if (pa > pb || pa > pc) {
					pr = b;
					if (pb > pc)
						pr = c;
				}
				x += pr;
			}
			STB(line, i, x);
		}
	}
}

/* uk_png_sample_at : n = échantillons par pixel (8 ou 16 bits) -> s[0..3] =
 * quatre échantillons sur 8 bits (toujours quatre lus, comme l'asm) */
static void png_sample_at(long line, long x, long bits, long n, u32 *s)
{
	long o = x * n;
	if (bits == 16) {                          /* 16 bits : l'octet fort */
		o <<= 1;
		long p = line + o;
		s[0] = (u32)LDB(p, 0);
		s[1] = (u32)LDB(p, 2);
		s[2] = (u32)LDB(p, 4);
		s[3] = (u32)LDB(p, 6);
		return;
	}
	long p = line + o;
	s[0] = (u32)LDB(p, 0);
	s[1] = (u32)LDB(p, 1);
	s[2] = (u32)LDB(p, 2);
	s[3] = (u32)LDB(p, 3);
}

/* uk_png_bits : x = pixel -> l'échantillon de « bits » bits (1, 2, 4, 8 ;
 * 16 : l'octet fort) */
static u32 png_bits(long line, long x, long bits)
{
	if (bits == 16)
		return (u32)LDB(line, x << 1);
	if (bits == 8)
		return (u32)LDB(line, x);
	long pos = x * bits;                       /* position en bits (poids fort d'abord) */
	u32 v = (u32)LDB(line, LSR(pos, 3));
	long sh = 8 - ((pos & 7) + bits);
	v = (u32)LSRW(v, sh);
	return v & (u32)(LSLW(1, bits) - 1);
}

/* uk_png_pixels : x0 = état de png_decode, x1 = pas : en pixels 0xAARRGGBB */
static void png_pixels(long st, long pitch)
{
	long dst = LDX(st, PS_IMG) + IMG_PIX;      /* destination */
	for (long y = 0; ULT(y, LDX(st, PS_H)); y++) {
		long line = LDX(st, PS_RAW) + y * (pitch + 1) + 1;   /* la ligne */
		for (long x = 0; ULT(x, LDX(st, PS_W)); x++) {
			long bits = LDX(st, PS_BITS);
			long color = LDX(st, PS_COLOR);
			u32 s[4];
			u32 px;
			if (color == 3) {                  /* palette 1, 2, 4, 8 bits */
				u32 v = png_bits(line, x, bits);
				long pal = LDX(st, PS_PLTE);
				if (!pal) {
					px = 0xFF000000u;
				} else {
					long e = pal + (long)v * 3;   /* comme l'asm : indice non borné */
					px = (u32)LDB(e, 2) | (u32)LDB(e, 1) << 8 | (u32)LDB(e, 0) << 16;
					long tr = LDX(st, PS_TRNS);    /* alpha de la palette (tRNS) */
					if (!tr || !ULT((long)v, LDX(st, PS_TRNS_LEN)))
						px |= 0xFF000000u;
					else
						px |= (u32)LDB(tr, v) << 24;
				}
			} else if (color == 0) {           /* gris 1, 2, 4, 8, 16 bits */
				u32 v = png_bits(line, x, bits);
				u32 raw = v;                   /* (valeur brute : tRNS) */
				if (ULT(bits, 8))              /* mise à l'échelle 0..255 */
					v = (u32)UDIVW(v * 255, LSLW(1, bits) - 1);
				u32 g = v | v << 8;
				px = g | v << 16;
				long tr = LDX(st, PS_TRNS);
				/* comme l'asm : comparé à l'octet faible de la valeur de
				 * tRNS, à l'octet fort en 16 bits ; égal : alpha 0 */
				if (!tr || (u32)LDB(tr, 1) != raw)
					px |= 0xFF000000u;
			} else if (color == 4) {           /* gris + alpha */
				png_sample_at(line, x, bits, 2, s);
				px = s[0] | s[0] << 8 | s[0] << 16 | s[1] << 24;
			} else if (color == 2) {           /* RVB */
				png_sample_at(line, x, bits, 3, s);
				px = s[2] | s[1] << 8 | s[0] << 16;
				long tr = LDX(st, PS_TRNS);    /* tRNS : la couleur transparente */
				if (!tr || (u32)LDB(tr, 1) != s[0] || (u32)LDB(tr, 3) != s[1] ||
				    (u32)LDB(tr, 5) != s[2])
					px |= 0xFF000000u;
			} else {                           /* RVBA (6 ; comme l'asm : tout autre genre) */
				png_sample_at(line, x, bits, 4, s);
				px = s[2] | s[1] << 8 | s[0] << 16 | s[3] << 24;
			}
			STW(dst, 0, px);
			dst += 4;
		}
	}
}

/* uk_png_decode : x0 = données, x1 = octets -> image (0 : invalide, entrelacée) */
static long png_decode(long data, long size)
{
	/* +0 w, +8 h, +16 profondeur, +24 couleur, +32 palette, +40 tRNS, +48
	 * longueur tRNS, +56 idat, +64 longueur, +72 brut, +80 image, +88 taille
	 * du brut. Comme l'asm, seuls +32..+88 sont mis à zéro ; +0..+24 (pile non
	 * initialisée de l'asm sans IHDR) : à zéro ici, donc refusé. */
	long sb[12];
	long st = (long)(uintptr_t)sb;
	long end = data + size;
	long img;
	for (int i = 0; i < 12; i++)
		sb[i] = 0;
	long idat = uk_alloc(size);                /* IDAT mis bout à bout (au plus la taille du fichier) */
	if (!idat)
		goto fail;
	STX(st, PS_IDAT, idat);
	long c = data + 8;                         /* chunks */
	for (;;) {
		if (UGT(c + 8, end))
			break;
		long len = be32(c);                    /* longueur */
		u32 type = (u32)LDW(c, 4);             /* type */
		long d = c + 8;                        /* données */
		if (UGT(d + len + 4, end))
			break;
		if (type == 0x52444849) {              /* IHDR */
			STX(st, PS_W, be32(d));
			STX(st, PS_H, be32(d + 4));
			STX(st, PS_BITS, LDB(d, 8));
			STX(st, PS_COLOR, LDB(d, 9));
			if (LDB(d, 12))                    /* entrelacement : refusé */
				goto fail;
		} else if (type == 0x45544C50) {       /* PLTE */
			STX(st, PS_PLTE, d);
		} else if (type == 0x534E5274) {       /* tRNS */
			STX(st, PS_TRNS, d);
			STX(st, PS_TRNS_LEN, len);
		} else if (type == 0x54414449) {       /* IDAT */
			uk_memcpy(LDX(st, PS_IDAT) + LDX(st, PS_IDAT_LEN), d, len);
			STX(st, PS_IDAT_LEN, LDX(st, PS_IDAT_LEN) + len);
		} else if (type == 0x444E4549) {       /* IEND */
			break;
		}
		c = d + len + 4;                       /* (CRC) */
	}
	long w = LDX(st, PS_W), h = LDX(st, PS_H); /* largeur, hauteur */
	if (!w || !h)
		goto fail;
	long bits = LDX(st, PS_BITS);              /* bits par échantillon */
	long color = LDX(st, PS_COLOR);            /* échantillons par pixel */
	long spp = 1;                              /* gris, palette : 1 */
	if (color == 2)
		spp = 3;                               /* RVB */
	if (color == 4)
		spp = 2;                               /* gris + alpha */
	if (color == 6)
		spp = 4;                               /* RVBA */
	long pitch = LSR(w * spp * bits + 7, 3);   /* octets d'une ligne */
	long rsize = (pitch + 1) * h;              /* brut : h x (1 + pas) */
	STX(st, PS_RAW_SIZE, rsize);
	long raw = uk_alloc(rsize);
	if (!raw)
		goto fail;
	STX(st, PS_RAW, raw);
	long n = inflate(LDX(st, PS_IDAT), LDX(st, PS_IDAT_LEN), LDX(st, PS_RAW), LDX(st, PS_RAW_SIZE));
	if (n < LDX(st, PS_RAW_SIZE))
		goto fail;
	long bpp = LSR(bits * spp + 7, 3);         /* octets par pixel pour les filtres */
	png_unfilter(LDX(st, PS_RAW), pitch, h, bpp);   /* --- les filtres, ligne à ligne */
	img = uk_img_new(w, h);
	if (!img)
		goto fail;
	STX(st, PS_IMG, img);
	png_pixels(st, pitch);                     /* --- en pixels 0xAARRGGBB */
	img = LDX(st, PS_IMG);
	goto out;
fail:
	uk_free(LDX(st, PS_IMG));
	img = 0;
out:
	STX(st, PS_IMG, img);
	uk_free(LDX(st, PS_IDAT));
	uk_free(LDX(st, PS_RAW));
	return LDX(st, PS_IMG);
}

/* ---------------------------------------------------------------------------
 *  Dessin
 * ------------------------------------------------------------------------- */

/* x0 = image, x1 = x, x2 = y, x3 = largeur, x4 = hauteur (0, 0 : taille de
 * l'image) : à l'échelle (plus proche voisin), mélangée par son alpha,
 * découpée à la surface et à ui_clip */
long ui_image_draw(long a0, long a1, long a2, long a3, long a4)
{
	long img = a0;
	if (!img)
		return 0;
	long x = a1, y = a2, w = a3, h = a4;
	if (!w)
		w = LDX(img, IMG_W);
	if (!h)
		h = LDX(img, IMG_H);
	if (w <= 0 || h <= 0)
		return img;
	long v = uk.vars;                          /* bornes : surface, découpe */
	long right = LDX(v, UV_W), bottom = LDX(v, UV_H);
	long left = 0, top = 0;
	if (uk.clip_on) {
		left = uk.clip[0];
		top = uk.clip[1];
		if (uk.clip[2] < right)
			right = uk.clip[2];
		if (uk.clip[3] < bottom)
			bottom = uk.clip[3];
	}
	if (top < y)                               /* première ligne */
		top = y;
	if (bottom > y + h)
		bottom = y + h;
	if (left < x)
		left = x;
	long r = x + w;                            /* x0 rendu, comme l'asm */
	if (right > r)
		right = r;
	for (long row = top; row < bottom; row++) {
		long sy = UDIV((row - y) * LDX(img, IMG_H), h);   /* ligne source */
		long src = img + IMG_PIX + ((sy * LDX(img, IMG_W)) << 2);
		v = uk.vars;
		r = v;
		long dst = LDX(v, UV_SURF) + ((row * LDX(v, UV_W)) << 2);   /* ligne destination */
		for (long col = left; col < right; col++) {
			long sx = UDIV((col - x) * LDX(img, IMG_W), w);
			u32 p = (u32)LDW(src, sx << 2);    /* 0xAARRGGBB */
			u32 a = p >> 24;                   /* alpha */
			if (!a)
				continue;
			if (a == 255) {
				STW(dst, col << 2, p & 0x00FFFFFF);
				continue;
			}
			u32 d = (u32)LDW(dst, col << 2);   /* mélange : d + (s - d) * a / 255 */
			u32 out = 0;
			for (u32 sh = 0; sh < 24; sh += 8) {
				u32 s1 = (p >> sh) & 0xFF;
				u32 d1 = (d >> sh) & 0xFF;
				u32 m = (u32)SDIVW((s1 - d1) * a, 255);   /* sdiv w */
				m = (m + d1) & 0xFF;
				out |= m << sh;
			}
			STW(dst, col << 2, out);
		}
	}
	return r;
}

/* ---------------------------------------------------------------------------
 *  Widget : boîte d'image (UT_IMAGE)
 * ------------------------------------------------------------------------- */

/* x0..x3 = rectangle, x4 = image (0 : aucune ; elle reste au programme), x5 =
 * drapeaux (UIM_STRETCH, UIM_NOSCALE) -> poignée */
long ui_imagebox(long a0, long a1, long a2, long a3, long a4, long a5)
{
	long b = uk_w_new(UT_IMAGE, a0, a1, a2, a3);
	if (!b)
		return 0;
	STX(b, UW_AUX, a4);
	STX(b, UW_STYLE, a5);
	return b;
}

/* x0 = boîte, x1 = image : la montre */
long ui_imagebox_set(long a0, long a1)
{
	if (!a0)
		return 0;
	STX(a0, UW_AUX, a1);
	return uk_w_refresh(a0);
}

long uk_im_draw(long w)
{
	long x = SXTW(LDW(w, UW_X)), y = SXTW(LDW(w, UW_Y));
	long ww = LDW(w, UW_W), hh = LDW(w, UW_H);  /* (ldp w : zéro-étendus) */
	ui_fill(x, y, ww, hh, JET(TJ_CREUX));
	long img = LDX(w, UW_AUX);
	if (!img)
		return 0;
	long iw = LDX(img, IMG_W), ih = LDX(img, IMG_H);   /* taille de l'image */
	long st = LDX(w, UW_STYLE);
	if (st & 1)                                 /* étirée */
		return ui_image_draw(LDX(w, UW_AUX), x, y, ww, hh);
	if (!(st & 2)) {                            /* ajustée : le plus petit rapport */
		if (UGT(iw * hh, ih * ww)) {            /* par la largeur */
			ih = UDIV(ih * ww, iw);
			iw = ww;
		} else {                                /* par la hauteur */
			iw = UDIV(iw * hh, ih);
			ih = hh;
		}
	}
	long px = x + ASR(ww - iw, 1);              /* centrée */
	long py = y + ASR(hh - ih, 1);
	return ui_image_draw(LDX(w, UW_AUX), px, py, iw, ih);
}
