/* AudioKit en C : synthèse FM (ak_fm.c, réécriture de ak_fm.inc ; tables :
 * ak_fmtab.c, d'après ak_fmtab.inc).
 *
 * Des voix, chacune une onde simple (AKW_SQUARE..AKW_NOISE) ou un instrument
 * FM à deux opérateurs (AKFI_*), en arithmétique entière. L'enveloppe d'un
 * opérateur travaille sur une ATTÉNUATION en 1/256 d'octave (4096 unités =
 * 96 dB = silence), en virgule fixe 16.16.
 *
 * Un synthé (ak_fm_new) est un bloc de mémoire à soi, protégé par un verrou
 * (fm_lock : attente active courte, pas d'appel système) : un thread peut
 * rendre (ak_fm_render, ak_fm_live) pendant qu'un autre joue. Le synthé MIDI
 * (ak_synth) range son propre verrou au même endroit (SY_LOCK = AKFM_LOCK) et
 * se sert aussi de fm_lock / fm_unlock. */
#ifndef AK_FM_H
#define AK_FM_H

#include "ak.h"

/* ---- opérateur (FOP_*, 64 octets) */
#define FOP_PHASE 0
#define FOP_MULT2 4  /* multiplicateur x 2 */
#define FOP_STAGE 8  /* EG_* */
#define FOP_ATT 12   /* atténuation 16.16 */
#define FOP_ATTK 16  /* attaque : facteur exponentiel 16.16 par trame */
#define FOP_DECINC 20 /* déclin : unités 16.16 par trame */
#define FOP_RELINC 24 /* relâchement */
#define FOP_SUSATT 28 /* niveau de maintien (unités) */
#define FOP_LEVATT 32 /* niveau de l'opérateur (unités) */
#define FOP_WAVE 36  /* 0 sinus, 1 demi, 2 absolu, 3 impulsions */
#define FOP_SUS 40   /* != 0 : tenu */
#define FOP_TREM 44
#define FOP_VIB 48
#define FOP_SIZE 64

#define EG_OFF 0
#define EG_ATTACK 1
#define EG_DECAY 2
#define EG_SUSTAIN 3
#define EG_RELEASE 4

struct fop {
	u32 phase;  /* 0 */
	u32 mult2;  /* 4 */
	u32 stage;  /* 8 EG_* */
	u32 att;    /* 12 */
	u32 attk;   /* 16 (0xFFFFFFFF : instantané) */
	u32 decinc; /* 20 */
	u32 relinc; /* 24 */
	u32 susatt; /* 28 */
	u32 levatt; /* 32 */
	u32 wave;   /* 36 */
	u32 sus;    /* 40 AKFF_SUSTAINED (masqué) */
	u32 trem;   /* 44 AKFF_TREMOLO (masqué : 0 ou 2) */
	u32 vib;    /* 48 AKFF_VIBRATO (masqué : 0 ou 4) */
	u32 pad[3];
};
AK_STATIC_OFF(struct fop, stage, FOP_STAGE);
AK_STATIC_OFF(struct fop, levatt, FOP_LEVATT);
AK_STATIC_OFF(struct fop, vib, FOP_VIB);
_Static_assert(sizeof(struct fop) == FOP_SIZE, "fop");

/* ---- voix (FMV_*, 192 octets) */
#define FMV_ON 0 /* touche enfoncée */
#define FMV_PHASE 4
#define FMV_INC 8     /* pas de phase (32 bits) */
#define FMV_WAVE 12   /* AKW_* */
#define FMV_GAIN 16   /* 0..65535 (FM : le volume) */
#define FMV_TARGET 20
#define FMV_NOISE 24  /* registre à décalage du bruit */
#define FMV_NOISEV 28
#define FMV_FEEDBACK 32
#define FMV_CONN 36 /* 1 additif */
#define FMV_FB1 40  /* deux dernières sorties du modulateur */
#define FMV_FB2 44
#define FMV_HASFM 48
#define FMV_GL 52 /* panoramique : 0..256 de chaque côté */
#define FMV_GR 56
#define FMV_OP0 64 /* modulateur */
#define FMV_OP1 (FMV_OP0 + FOP_SIZE) /* porteuse */
#define FMV_SIZE (FMV_OP1 + FOP_SIZE)

struct fmv {
	u32 on;	      /* 0 */
	u32 phase;    /* 4 */
	u32 inc;      /* 8 */
	u32 wave;     /* 12 */
	u32 gain;     /* 16 */
	u32 target;   /* 20 */
	u32 noise;    /* 24 */
	s32 noisev;   /* 28 */
	u32 feedback; /* 32 */
	u32 conn;     /* 36 */
	s32 fb1;      /* 40 */
	s32 fb2;      /* 44 */
	u32 hasfm;    /* 48 */
	u32 gl;	      /* 52 */
	u32 gr;	      /* 56 */
	u32 pad;      /* 60 */
	struct fop op[2]; /* 64 modulateur, 128 porteuse */
};
AK_STATIC_OFF(struct fmv, noise, FMV_NOISE);
AK_STATIC_OFF(struct fmv, hasfm, FMV_HASFM);
AK_STATIC_OFF(struct fmv, gr, FMV_GR);
AK_STATIC_OFF(struct fmv, op[0], FMV_OP0);
AK_STATIC_OFF(struct fmv, op[1], FMV_OP1);
_Static_assert(sizeof(struct fmv) == FMV_SIZE, "fmv");

/* ---- synthé (AKFM_*) */
#define AKFM_LOCK 0
#define AKFM_NV 8    /* nombre de voix */
#define AKFM_LFOT 16 /* phases : trémolo 3,7 Hz, vibrato 6,1 Hz (32 bits) */
#define AKFM_LFOV 20
#define AKFM_LIVE 24 /* thread de sortie (ak_fm_live) : son bloc */
#define AKFM_VOICES 64

#define FM_ATT_MAX 4096 /* 96 dB */
#define FM_RATE 44100
#define FM_PIECE 64 /* trames rendues sous le verrou */

struct akfm {
	u32 lock;	    /* 0 */
	u32 pad0;
	u64 nv;		    /* 8 */
	u32 lfot;	    /* 16 */
	u32 lfov;	    /* 20 */
	u64 live;	    /* 24 */
	u8 pad1[32];
	struct fmv voices[]; /* 64 */
};
AK_STATIC_OFF(struct akfm, nv, AKFM_NV);
AK_STATIC_OFF(struct akfm, lfot, AKFM_LFOT);
AK_STATIC_OFF(struct akfm, lfov, AKFM_LFOV);
AK_STATIC_OFF(struct akfm, live, AKFM_LIVE);
AK_STATIC_OFF(struct akfm, voices, AKFM_VOICES);

/* ---- tables (ak_fmtab.c) */
extern const s16 ak_sin1024[1024];  /* une période de sinus, amplitude 32767 */
extern const u16 ak_exp256[256];    /* 65535 * 2^(-i/256) */
extern const u32 ak_cents1200[1200]; /* 2^(i/1200) en 2.30 */

/* ---- fonctions partagées (noms des étiquettes de l'asm) */

/* verrou du bloc (u32 en +0 : synthé FM ou synthé MIDI) */
void fm_lock(void *blk);
void fm_unlock(void *blk);
/* voix v du synthé, 0 si le numéro est mauvais (l'asm : x2 et Z) */
struct fmv *fm_voice(struct akfm *fm, u64 v);
/* opérateur <- paramètres AKFO_* ; fresh = 1 si la voix n'est pas déjà en FM */
void fm_op_set(struct fop *op, const u8 *par, u64 fresh);
/* touche relâchée (sans verrou) */
void fm_key_off(struct fmv *v);
/* 1 si la voix sonne (touche enfoncée ou relâchement) */
long fm_voice_active(const struct fmv *v);
/* ÉCRIT frames trames s16 stéréo, sans verrou */
void fm_render_piece(struct akfm *fm, s16 *out, u64 frames);
/* base * 2^(cents / 1200), arrondi */
long ak_cents_mul(long cents, long base);

/* ---- fonctions publiques (audiokit.abi) */
long ak_fm_new(long nv);
long ak_fm_free(struct akfm *fm);
long ak_fm_instrument(struct akfm *fm, long v, const u8 *inst);
long ak_fm_from_fms(u8 *inst, const u8 *fms);
long ak_fm_start(struct akfm *fm, long v, long mhz, long wave, long vol);
long ak_fm_stop(struct akfm *fm, long v);
long ak_fm_silence(struct akfm *fm);
long ak_fm_active(struct akfm *fm);
long ak_fm_pan(struct akfm *fm, long v, long pan);
long ak_fm_pitch(struct akfm *fm, long v, long mhz);
long ak_fm_gain(struct akfm *fm, long v, long vol);
long ak_fm_render(struct akfm *fm, s16 *out, u64 frames);
long ak_note_mhz(long key);
long ak_note_octave_mhz(long note, long octave);
/* audiokit.c (thread de sortie) ; out = -1 : l'arrête */
long ak_fm_live(long fm, long out);

#endif
