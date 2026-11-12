/* AudioKit en C : lecture de fichiers MIDI et FM Song (réécriture de
 * ak_midi.inc), deux formats de plus pour le décodeur générique : ils rendent
 * du PCM stéréo 44,1 kHz, donc ak_dec_read, ak_play, les encodeurs et les
 * programmes de lecture les prennent comme les autres.
 *  * .mid (Standard MIDI File, formats 0, 1, 2) : séquenceur sur ak_synth ; la
 *    fin (une passe à blanc) et la recherche (rejoue sans les notes) ;
 *  * .fms (FM Song, FM Tracker d'Onyx) : 8 canaux, deux opérateurs ; un rang
 *    de la grille dure vitesse / 20 s. */
#include "ak_fm.h"
#include "ak_synth.h"

#define MID_UNIT 1024	     /* trames par unité rendue */
#define MID_TAIL (2 * 44100) /* après le dernier événement (les relâchements) */
#define MI_FSH 16

/* lecteur .mid (MI_*, 96 octets) ; les adresses du fichier sont des entiers
 * (une piste peut annoncer plus que le fichier : comme l'asm) */
struct mi_trk {
	u64 p;	    /* 0 prochain octet */
	u64 end;    /* 8 */
	u64 tick;   /* 16 pulsation du prochain événement */
	u32 status; /* 24 statut courant */
	u32 pad;
};
_Static_assert(sizeof(struct mi_trk) == 32, "mi_trk");

struct mi {
	struct aksy *syn;   /* 0 */
	u8 *data;	    /* 8 fichier en mémoire */
	u64 ntrk;	    /* 16 */
	u64 div;	    /* 24 pulsations par noire */
	u64 tempo;	    /* 32 microsecondes par noire */
	u64 tick;	    /* 40 pulsation du dernier point de référence */
	u64 frame;	    /* 48 sa trame (<< MI_FSH) */
	u64 fpt;	    /* 56 trames par pulsation (<< MI_FSH) */
	struct mi_trk *trk; /* 64 */
	u64 done;	    /* 72 */
	u64 pos;	    /* 80 trames rendues */
	u64 len;	    /* 88 octets du fichier */
};
_Static_assert(sizeof(struct mi) == 96, "mi");

static inline u8 byte_at(u64 a)
{
	return *(const u8 *)(uintptr_t)a;
}

/* nombre à longueur variable (4 octets au plus) ; *next = octet suivant */
static u64 mid_varlen(u64 p, u64 end, u64 *next)
{
	u64 v = 0;
	for (int k = 4; k; k--) {
		if (p >= end)
			break;
		u32 b = byte_at(p++);
		v = (v << 7) | (b & 127);
		if (!(b & 128))
			break;
	}
	*next = p;
	return v;
}

/* recalcule fpt */
static void midi_fpt(struct mi *m)
{
	m->fpt = (m->tempo * FM_RATE << MI_FSH) / (1000000 * m->div);
}

/* -> trame (<< MI_FSH) de la pulsation t */
static u64 midi_tick_frame(struct mi *m, u64 t)
{
	return m->frame + (t - m->tick) * m->fpt;
}

/* au début */
static void midi_rewind(struct mi *m)
{
	m->tempo = 500000;
	m->tick = 0;
	m->frame = 0;
	m->done = 0;
	m->pos = 0;
	midi_fpt(m);
	u64 p = (uintptr_t)m->data, end = p + m->len;
	p += (u64)ak_rd32be(m->data + 4) + 8;
	struct mi_trk *t = m->trk;
	for (u64 n = m->ntrk; n; n--, t++) {
		t->p = 0; /* piste vide par défaut */
		t->end = 0;
		if (p + 8 > end)
			continue;
		u64 sz = ak_rd32be((const u8 *)(uintptr_t)p + 4);
		u64 a = p + 8, e = a + sz;
		if (e > end)
			e = end;
		t->p = a;
		t->end = e;
		t->status = 0;
		p += 8 + sz;
		t->tick = mid_varlen(a, e, &t->p); /* premier délai */
	}
}

/* -> piste du prochain événement (0 : fin) ; *tick = sa pulsation */
static struct mi_trk *midi_peek(struct mi *m, u64 *tick)
{
	struct mi_trk *r = 0, *t = m->trk;
	u64 best = (u64)-1;
	for (u64 n = m->ntrk; n; n--, t++) {
		if (t->p >= t->end || t->tick >= best)
			continue;
		r = t;
		best = t->tick;
	}
	*tick = best;
	return r;
}

/* mode : 0 tout jouer, 1 sans les notes, 2 rien vers le synthé -> 1 un
 * événement traité, 0 : fin */
static long midi_step(struct mi *m, u64 mode)
{
	u64 tick;
	struct mi_trk *t = midi_peek(m, &tick);
	if (!t)
		return 0;
	m->frame = midi_tick_frame(m, tick); /* le temps avance jusqu'à cet événement */
	m->tick = tick;
	u64 p = t->p, end = t->end;
	u32 st = byte_at(p);
	if (st & 0x80) {
		p++;
		if (st >= 0xF0)
			goto sys;
		t->status = st;
	} else {
		st = t->status;
		if (!st) { /* données sans statut : l'octet est perdu */
			p++;
			goto delta;
		}
	}
	{
		u32 op = st & 0xF0, d1, d2 = 0;
		d1 = byte_at(p++);
		if (op != 0xC0 && op != 0xD0) /* deux données */
			d2 = byte_at(p++);
		if (mode == 2)
			goto delta;
		if (mode == 1 && (op == 0x90 || op == 0x80)) /* recherche : ni notes */
			goto delta;
		ak_synth_midi(m->syn, st & 15, op, d1, d2);
		goto delta;
	}
sys:
	if (st == 0xFF) { /* méta : type, longueur */
		u32 type = byte_at(p++);
		u64 len = mid_varlen(p, end, &p);
		if (type == 0x51) {
			if (len >= 3 && p + 3 <= end) { /* tempo */
				u64 tp = ((u64)byte_at(p) << 16) | ((u64)byte_at(p + 1) << 8) | byte_at(p + 2);
				if (tp) {
					m->tempo = tp;
					midi_fpt(m);
				}
			}
		} else if (type == 0x2F) { /* fin de piste */
			t->p = end;
			return 1;
		}
		p += len;
	} else { /* F0, F7 : longueur, octets */
		u64 len = mid_varlen(p, end, &p);
		p += len;
	}
delta:
	if (p >= end) {
		t->p = end;
		return 1;
	}
	{
		u64 d = mid_varlen(p, end, &t->p);
		t->tick += d;
	}
	return 1;
}

/* joue les événements d'avant la trame fr */
static void midi_upto(struct mi *m, u64 fr, u64 mode)
{
	u64 lim = fr << MI_FSH, tick;
	while (midi_peek(m, &tick)) {
		if (midi_tick_frame(m, tick) >= lim)
			return;
		midi_step(m, mode);
	}
}

/* -> trames dans les tampons plans */
static long midi_next(struct akd *d)
{
	struct mi *m = d->codec;
	u64 n = 0;
	do {
		midi_upto(m, m->pos + SY_BLOCK, 0);
		ak_synth_render_f32(m->syn, (float *)d->pcm[0] + n, (float *)d->pcm[1] + n, SY_BLOCK);
		m->pos += SY_BLOCK;
		n += SY_BLOCK;
	} while (n < MID_UNIT);
	return (long)n;
}

/* trame -> 0 : rejoue les contrôleurs jusque-là */
static long midi_seek(struct akd *d, u64 fr)
{
	struct mi *m = d->codec;
	ak_synth_reset(m->syn);
	midi_rewind(m);
	midi_upto(m, fr, 1);
	fr &= ~(u64)(SY_BLOCK - 1);
	m->pos = fr;
	d->pos = (s64)fr;
	return 0;
}

static void midi_close(struct akd *d)
{
	struct mi *m = d->codec;
	ak_synth_free(m->syn);
}

/* tout le flux (len octets depuis la position) dans dst -> 1, 0 : trop court */
static int read_all(struct akd *d, u8 *dst, u64 len)
{
	while (len) {
		u64 avail;
		u8 *p = akd_need(d, len < AKD_IOBUF / 2 ? len : AKD_IOBUF / 2, &avail);
		if (!avail)
			return 0;
		u64 k = avail < len ? avail : len;
		memmove(dst, p, k);
		dst += k;
		len -= k;
		akd_skip(d, k);
	}
	return 1;
}

/* décodeur (lecture sur "MThd") -> 0 / < 0 */
long midi_open(struct akd *d)
{
	struct mi *m = akd_alloc(d, sizeof *m);
	if (!m)
		return AKE_NOMEM;
	d->codec = m;
	/* tout le fichier en mémoire */
	u64 len = (u64)d->len;
	if (len >= (u64)1 << 26) /* (64 Mo : un .mid ne va pas si loin) */
		return AKE_CORRUPT;
	m->len = len;
	m->data = akd_alloc(d, len + 16);
	if (!m->data)
		return AKE_NOMEM;
	if (!read_all(d, m->data, len))
		return AKE_CORRUPT;
	const u8 *h = m->data;
	if (len < 14)
		return AKE_CORRUPT;
	u32 ntrk = __builtin_bswap16(ak_rd16le(h + 10));
	u32 div = __builtin_bswap16(ak_rd16le(h + 12));
	if (div & 0x8000) { /* SMPTE : -images/s, sous-images */
		s32 sd = (s16)div;
		u32 fps = (u32)(-(sd >> 8)), sub = (u32)sd & 255;
		div = (fps * sub) >> 1; /* pulsations par seconde : 500 ms par noire (tempo par défaut) */
	}
	if (!div)
		return AKE_CORRUPT;
	m->ntrk = ntrk;
	m->div = div;
	if (!ntrk)
		return AKE_CORRUPT;
	m->trk = akd_alloc(d, (u64)ntrk * sizeof(struct mi_trk));
	if (!m->trk)
		return AKE_NOMEM;
	long s = ak_synth_new(0, 0); /* (banque FM sans SoundFont du système) */
	if (s < 0)
		return s;
	m->syn = (struct aksy *)s;
	d->modfree = midi_close;
	/* passe à blanc : la durée */
	midi_rewind(m);
	while (midi_step(m, 2))
		;
	d->total = (s64)((m->frame >> MI_FSH) + MID_TAIL); /* dernière trame d'événement */
	d->limit = 1;
	midi_rewind(m);
	d->fmt = AKF_MIDI;
	d->rate = FM_RATE;
	d->ch = 2;
	d->bits = 0;
	d->next = midi_next;
	d->seekfn = midi_seek;
	return akd_planes(d, MID_UNIT, 2);
}

/* ---------------------------------------------------------------------------
 *  FM Song
 * --------------------------------------------------------------------------- */
#define FMS_CH 8
#define FMS_HEAD 107 /* magie, titre, auteur, commentaire, motifs */
#define FMS_INS 34   /* nom (8) + 26 octets */
#define FMS_MAXPAT 64
#define FMS_MAXROWS 256

struct fs {
	struct akfm *fm; /* 0 synthé FM */
	u8 *data;	 /* 8 fichier */
	u64 len;	 /* 16 */
	u64 pat;	 /* 24 motif courant : numéro */
	u64 row;	 /* 32 rang */
	u64 npat;	 /* 40 */
	u64 posbyte;	 /* 48 (inutilisé) */
	u64 rowleft;	 /* 56 trames avant le rang suivant */
	u32 tab[FMS_MAXPAT]; /* 64 décalage de chaque motif */
};
_Static_assert(sizeof(struct fs) == 64 + FMS_MAXPAT * 4, "fs");

static const u8 fms_semi[8] = {0, 0, 2, 4, 5, 7, 9, 11}; /* (1..7 = do..si) */

static void fms_rewind(struct fs *f)
{
	f->pat = 0;
	f->row = 0;
	f->rowleft = 0;
}

/* joue le rang courant (quiet : sans son) et passe au suivant ; pose rowleft
 * -> 1 s'il restait un rang (0 : fin de la chanson) */
static long fms_row(struct fs *f, u64 quiet)
{
	if (f->pat >= f->npat) {
		ak_fm_stop(f->fm, -1);
		f->rowleft = MID_TAIL; /* rien de plus : du silence */
		return 0;
	}
	const u8 *pt = f->data + f->tab[f->pat]; /* le motif */
	s32 rows = (s16)ak_rd16le(pt);
	s32 speed = (s16)ak_rd16le(pt + 2);
	if (speed < 1)
		speed = 1;
	u64 row = f->row;
	for (u64 c = 0; c < FMS_CH; c++) {
		if (quiet)
			continue;
		u64 o = (u64)((u32)c * (u32)rows + (u32)row) + 12;
		const u8 *q = pt + o;
		if ((uintptr_t)q >= (uintptr_t)f->data + f->len)
			continue;
		u32 b = *q;
		if (b & 0x80) { /* silence */
			ak_fm_stop(f->fm, (long)c);
			continue;
		}
		if (!(b & 7)) /* la note continue */
			continue;
		if (pt[4 + c]) { /* canal muet dans ce motif */
			ak_fm_stop(f->fm, (long)c);
			continue;
		}
		long note = fms_semi[b & 7] + ((b >> 6) & 1); /* dièse */
		long mhz = ak_note_octave_mhz(note, (b >> 3) & 7);
		ak_fm_start(f->fm, (long)c, mhz, AKW_FM, 220);
	}
	row++; /* rang suivant */
	if (!((u32)row < (u32)rows)) {
		row = 0;
		f->pat++;
	}
	f->row = row;
	f->rowleft = (u32)(2205 * (u32)speed);
	return 1;
}

static long fms_next(struct akd *d)
{
	struct fs *f = d->codec;
	s16 t[SY_BLOCK * 2];
	u64 n = 0;
	do {
		u64 left = f->rowleft;
		if (!left) {
			fms_row(f, 0);
			left = f->rowleft;
		}
		u64 k = MID_UNIT - n;
		if (k >= left)
			k = left;
		if (k >= SY_BLOCK)
			k = SY_BLOCK;
		ak_fm_render(f->fm, t, k);
		s32 *l = (s32 *)d->pcm[0] + n, *r = (s32 *)d->pcm[1] + n;
		for (u64 i = 0; i < k; i++) { /* s16 -> 32 bits cadré à gauche */
			l[i] = (s32)((u32)t[2 * i] << 16);
			r[i] = (s32)((u32)t[2 * i + 1] << 16);
		}
		f->rowleft = left - k;
		n += k;
	} while (n < MID_UNIT);
	return (long)n;
}

/* trame -> 0 : les rangs d'avant passés sans son */
static long fms_seek(struct akd *d, u64 fr)
{
	struct fs *f = d->codec;
	ak_fm_silence(f->fm);
	fms_rewind(f);
	u64 at = 0; /* trame atteinte */
	while (fms_row(f, 1)) {
		u64 x = at + f->rowleft;
		if (x > fr)
			break;
		at = x;
		f->rowleft = 0;
	}
	u64 rest = fr - at; /* reste du rang */
	f->rowleft = f->rowleft >= rest ? f->rowleft - rest : 0;
	d->pos = (s64)fr;
	return 0;
}

static void fms_close(struct akd *d)
{
	struct fs *f = d->codec;
	ak_fm_free(f->fm);
}

/* champ fixe, sans les espaces de fin */
static void fms_tag(struct akd *d, u64 tag, const u8 *p, u64 n)
{
	while (n && p[n - 1] <= ' ')
		n--;
	if (n)
		akd_tag_put(d, tag, p, n, 0);
}

/* décodeur -> 0 / < 0 */
long fms_open(struct akd *d)
{
	u8 inst[AKFI_SIZE + 8];
	struct fs *f = akd_alloc(d, sizeof *f);
	if (!f)
		return AKE_NOMEM;
	d->codec = f;
	u64 len = (u64)d->len;
	if (len < FMS_HEAD + FMS_CH * FMS_INS)
		return AKE_FORMAT;
	f->len = len;
	f->data = akd_alloc(d, len + 16);
	if (!f->data)
		return AKE_NOMEM;
	if (!read_all(d, f->data, len))
		return AKE_FORMAT;
	const u8 *b = f->data;
	s32 np = (s16)ak_rd16le(b + 105); /* nombre de motifs */
	if (np < 1 || np > FMS_MAXPAT)
		return AKE_FORMAT;
	f->npat = (u64)np;
	/* motifs : les décalages, la durée */
	u64 o = FMS_HEAD + FMS_CH * FMS_INS, dur = 0, i; /* (vingtièmes de seconde) */
	for (i = 0; i < (u64)np; i++) {
		if (o + 12 > len)
			break;
		s32 rows = (s16)ak_rd16le(b + o), speed = (s16)ak_rd16le(b + o + 2);
		if (rows < 1 || rows > FMS_MAXROWS)
			break;
		if (speed < 1)
			speed = 1;
		f->tab[i] = (u32)o;
		dur += (u32)rows * (u32)speed;
		o += 12;
		o += (u32)rows << 3;
	}
	if (i < (u64)np) { /* (fichier coupé : ce qui est entier) */
		if (!i)
			return AKE_FORMAT;
		f->npat = i;
	}
	d->total = (s64)(dur * 2205 + MID_TAIL); /* 44100 / 20 = 2205 trames */
	d->limit = 1;
	/* le synthé, les instruments */
	long fm = ak_fm_new(FMS_CH);
	if (fm < 0)
		return fm;
	f->fm = (struct akfm *)fm;
	d->modfree = fms_close;
	for (u64 c = 0; c < FMS_CH; c++) {
		ak_fm_from_fms(inst, b + FMS_HEAD + FMS_INS * c + 8); /* (après le nom) */
		ak_fm_instrument(f->fm, (long)c, inst);
	}
	/* titre et auteur (champs fixes, espaces de fin retirés) */
	fms_tag(d, AKT_TITLE, b + 15, 20);
	fms_tag(d, AKT_ARTIST, b + 35, 20);
	d->fmt = AKF_FMS;
	d->rate = FM_RATE;
	d->ch = 2;
	d->bits = 0;
	d->pcmfmt = AKD_S32; /* (entiers 32 bits cadrés à gauche) */
	d->next = fms_next;
	d->seekfn = fms_seek;
	fms_rewind(f);
	return akd_planes(d, MID_UNIT, 2);
}
