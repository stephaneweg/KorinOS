/* AudioKit en C : synthé MIDI (ak_synth.c, réécriture de ak_synth.inc ; tables :
 * ak_gmbank.c, d'après ak_sftab.inc et ak_gmbank.inc).
 *
 * 16 canaux General MIDI, deux moteurs :
 *  * SoundFont 2 : voix à table d'ondes, enveloppes de volume et de
 *    modulation, deux LFO, filtre passe-bas résonant, panoramique,
 *    réverbération, chorus ; les calculs lents se font par bloc de 64 trames ;
 *  * banque FM (ak_gmbank.c) quand aucune SoundFont n'est là : les 128
 *    programmes et les percussions, sur la synthèse FM (ak_fm.c).
 * Chaque synthé est un bloc à soi, protégé par son verrou (SY_LOCK = AKFM_LOCK :
 * fm_lock / fm_unlock). Une SoundFont (SF_*) est en lecture seule : plusieurs
 * synthés peuvent la partager.
 *
 * Dispositions : celles de l'asm (mêmes décalages, vérifiés ci-dessous). */
#ifndef AK_SYNTH_H
#define AK_SYNTH_H

#include "ak.h"

#define SY_BLOCK 64 /* trames par bloc */
#define SY_NCH 16
#define SY_NV 64  /* voix SoundFont */
#define SY_NFM 32 /* voix FM */

/* ---- canal (SC_*, 64 octets) */
#define SC_PROG 0
#define SC_BANK 4
#define SC_VOL 8
#define SC_EXPR 12
#define SC_PAN 16
#define SC_BEND 20 /* -8192..8191 */
#define SC_RANGE 24 /* étendue (cents) */
#define SC_SUST 28
#define SC_MOD 32
#define SC_RPN 36
#define SC_REV 40
#define SC_CHO 44
#define SC_PRESET 48 /* preset SoundFont + 1, 0 : à chercher */
#define SC_SIZE 64

struct sy_chan {
	u32 prog;   /* 0 */
	u32 bank;   /* 4 */
	u32 vol;    /* 8 CC 7 */
	u32 expr;   /* 12 CC 11 */
	u32 pan;    /* 16 CC 10 */
	s32 bend;   /* 20 */
	u32 range;  /* 24 */
	u32 sust;   /* 28 CC 64 */
	u32 mod;    /* 32 CC 1 */
	u32 rpn;    /* 36 */
	u32 rev;    /* 40 CC 91 */
	u32 cho;    /* 44 CC 93 */
	u32 preset; /* 48 */
	u32 pad[3];
};
AK_STATIC_OFF(struct sy_chan, bend, SC_BEND);
AK_STATIC_OFF(struct sy_chan, preset, SC_PRESET);
_Static_assert(sizeof(struct sy_chan) == SC_SIZE, "sy_chan");

/* ---- voix FM de la banque (FV_*, 16 octets) */
#define FV_STATE 0 /* 0 libre, 1 touche, 2 tenue (pédale), 3 relâchée */
#define FV_CHAN 1
#define FV_KEY 2
#define FV_PLAYED 3 /* touche jouée (percussions : celle du tableau) */
#define FV_AGE 4
#define FV_INS 8 /* instrument chargé + 1 */
#define FV_VEL 12
#define FV_SIZE 16

struct sy_fmv {
	u8 state;  /* 0 */
	u8 chan;   /* 1 */
	u8 key;	   /* 2 */
	u8 played; /* 3 */
	u32 age;   /* 4 */
	u32 ins;   /* 8 */
	u8 vel;	   /* 12 */
	u8 pad[3];
};
AK_STATIC_OFF(struct sy_fmv, age, FV_AGE);
AK_STATIC_OFF(struct sy_fmv, ins, FV_INS);
AK_STATIC_OFF(struct sy_fmv, vel, FV_VEL);
_Static_assert(sizeof(struct sy_fmv) == FV_SIZE, "sy_fmv");

/* ---- enveloppe (EN_*) : niveau 16.16 (0..65536) */
#define EN_STAGE 0
#define EN_COUNT 4
#define EN_LVL 8
#define EN_ATTS 12
#define EN_DECS 16
#define EN_SUS 20
#define EN_RELS 24
#define EN_DELAY 28
#define EN_HOLD 32
#define ES_DELAY 0
#define ES_ATTACK 1
#define ES_HOLD 2
#define ES_DECAY 3
#define ES_SUSTAIN 4
#define ES_RELEASE 5
#define ES_DONE 6

struct sy_env {
	u32 stage; /* 0 */
	u32 count; /* 4 */
	u32 lvl;   /* 8 */
	u32 atts;  /* 12 */
	u32 decs;  /* 16 */
	u32 sus;   /* 20 */
	u32 rels;  /* 24 */
	u32 delay; /* 28 */
	u32 hold;  /* 32 */
	u32 pad;
};
AK_STATIC_OFF(struct sy_env, hold, EN_HOLD);
_Static_assert(sizeof(struct sy_env) == 40, "sy_env");

/* ---- voix SoundFont (SV_*, 288 octets) */
#define SV_STATE 0
#define SV_REL 4 /* 0 touche enfoncée, 1 relâchée, 2 tenue par la pédale */
#define SV_CHAN 8
#define SV_KEY 12
#define SV_VEL 16
#define SV_AGE 20
#define SV_EXCL 24
#define SV_MODE 28 /* 0 sans boucle, 1 boucle, 3 boucle jusqu'au relâchement */
#define SV_POS 32  /* 32.32 */
#define SV_INC 40
#define SV_SMP 48
#define SV_END 56
#define SV_LS 64
#define SV_LE 72
#define SV_CENTS 80
#define SV_RATE 84 /* fréquence relative 12.20 */
#define SV_VE 88
#define SV_ME 128
#define SV_ATT 168
#define SV_PAN 172
#define SV_RSEND 176
#define SV_CSEND 180
#define SV_GL 184
#define SV_GR 188
#define SV_FC 192
#define SV_Q 196
#define SV_ML2P 200
#define SV_VL2P 204
#define SV_ME2P 208
#define SV_ML2F 212
#define SV_ME2F 216
#define SV_ML2V 220
#define SV_MLPH 224
#define SV_MLINC 228
#define SV_MLDEL 232
#define SV_VLPH 236
#define SV_VLINC 240
#define SV_VLDEL 244
#define SV_Z1 248
#define SV_Z2 252
#define SV_B0 256
#define SV_B1 260
#define SV_A1 264
#define SV_A2 268
#define SV_LASTFC 272
#define SV_SIZE 288

struct sy_voice {
	u32 state;     /* 0 */
	u32 rel;       /* 4 */
	u32 chan;      /* 8 */
	u32 key;       /* 12 */
	u32 vel;       /* 16 */
	u32 age;       /* 20 */
	u32 excl;      /* 24 */
	u32 mode;      /* 28 */
	u64 pos;       /* 32 */
	u64 inc;       /* 40 */
	const u8 *smp; /* 48 */
	u64 end;       /* 56 */
	u64 ls;	       /* 64 */
	u64 le;	       /* 72 */
	s32 cents;     /* 80 */
	u32 rate;      /* 84 */
	struct sy_env ve; /* 88 */
	struct sy_env me; /* 128 */
	s32 att;       /* 168 */
	s32 pan;       /* 172 */
	s32 rsend;     /* 176 */
	s32 csend;     /* 180 */
	float gl;      /* 184 */
	float gr;      /* 188 */
	s32 fc;	       /* 192 */
	s32 q;	       /* 196 */
	s32 ml2p;      /* 200 */
	s32 vl2p;      /* 204 */
	s32 me2p;      /* 208 */
	s32 ml2f;      /* 212 */
	s32 me2f;      /* 216 */
	s32 ml2v;      /* 220 */
	u32 mlph;      /* 224 */
	u32 mlinc;     /* 228 */
	u32 mldel;     /* 232 */
	u32 vlph;      /* 236 */
	u32 vlinc;     /* 240 */
	u32 vldel;     /* 244 */
	float z1;      /* 248 */
	float z2;      /* 252 */
	float b0;      /* 256 */
	float b1;      /* 260 */
	float a1;      /* 264 */
	float a2;      /* 268 */
	s32 lastfc;    /* 272 */
	u32 pad[3];
};
AK_STATIC_OFF(struct sy_voice, pos, SV_POS);
AK_STATIC_OFF(struct sy_voice, smp, SV_SMP);
AK_STATIC_OFF(struct sy_voice, cents, SV_CENTS);
AK_STATIC_OFF(struct sy_voice, ve, SV_VE);
AK_STATIC_OFF(struct sy_voice, me, SV_ME);
AK_STATIC_OFF(struct sy_voice, att, SV_ATT);
AK_STATIC_OFF(struct sy_voice, gl, SV_GL);
AK_STATIC_OFF(struct sy_voice, ml2v, SV_ML2V);
AK_STATIC_OFF(struct sy_voice, vldel, SV_VLDEL);
AK_STATIC_OFF(struct sy_voice, a2, SV_A2);
AK_STATIC_OFF(struct sy_voice, lastfc, SV_LASTFC);
_Static_assert(sizeof(struct sy_voice) == SV_SIZE, "sy_voice");

/* ---- synthé (SY_*) */
#define SY_LOCK 0 /* (même place que AKFM_LOCK : fm_lock) */
#define SY_SF 8
#define SY_FM 16
#define SY_AGE 24
#define SY_BPOS 32
#define SY_RVB 40
#define SY_GAIN 48
#define SY_MFC 52
#define SY_MME 56
#define SY_MATT 60
#define SY_MVEL 64
#define SY_CACT 68
#define SY_CHO 72
#define SY_CHAN 80
#define SY_FMV (SY_CHAN + SC_SIZE * SY_NCH)
#define SY_BL (SY_FMV + FV_SIZE * SY_NFM)
#define SY_BR (SY_BL + SY_BLOCK * 4)
#define SY_BV (SY_BR + SY_BLOCK * 4)
#define SY_BC (SY_BV + SY_BLOCK * 4)
#define SY_VOICES (SY_BC + SY_BLOCK * 4)
#define SY_SIZE (SY_VOICES + SV_SIZE * SY_NV)

struct aksf;
struct akfm;
struct aksy {
	u32 lock;	  /* 0 */
	u32 pad0;
	struct aksf *sf;  /* 8 0 : banque FM */
	struct akfm *fm;  /* 16 synthé FM */
	u64 age;	  /* 24 compteur de notes */
	u64 bpos;	  /* 32 trames déjà rendues du bloc courant */
	void *rvb;	  /* 40 réverbération */
	float gain;	  /* 48 volume général */
	s32 mfc;	  /* 52 modulateurs de la note : coupure (cents) */
	s32 mme;	  /* 56 enveloppe -> coupure */
	s32 matt;	  /* 60 atténuation (centibels) */
	u32 mvel;	  /* 64 1 : la SoundFont donne vélocité -> atténuation */
	u32 cact;	  /* 68 1 : un envoi de chorus pendant ce bloc */
	void *cho;	  /* 72 chorus */
	struct sy_chan chan[SY_NCH]; /* 80 */
	struct sy_fmv fmv[SY_NFM];   /* SY_FMV */
	float bl[SY_BLOCK];	     /* SY_BL */
	float br[SY_BLOCK];
	float bv[SY_BLOCK]; /* envoi de réverbération */
	float bc[SY_BLOCK]; /* envoi de chorus */
	struct sy_voice voices[SY_NV]; /* SY_VOICES */
};
AK_STATIC_OFF(struct aksy, sf, SY_SF);
AK_STATIC_OFF(struct aksy, rvb, SY_RVB);
AK_STATIC_OFF(struct aksy, gain, SY_GAIN);
AK_STATIC_OFF(struct aksy, mvel, SY_MVEL);
AK_STATIC_OFF(struct aksy, cact, SY_CACT);
AK_STATIC_OFF(struct aksy, cho, SY_CHO);
AK_STATIC_OFF(struct aksy, chan, SY_CHAN);
AK_STATIC_OFF(struct aksy, fmv, SY_FMV);
AK_STATIC_OFF(struct aksy, bl, SY_BL);
AK_STATIC_OFF(struct aksy, bc, SY_BC);
AK_STATIC_OFF(struct aksy, voices, SY_VOICES);
_Static_assert(sizeof(struct aksy) == SY_SIZE, "aksy");

/* ---- SoundFont (SF_*, 184 octets) : pointeurs dans le fichier en mémoire */
#define SF_MEM 0 /* fichier en mémoire (ak_sf_load : rendu par ak_sf_free) */
#define SF_SIZE_ 8
#define SF_SMPL 16
#define SF_NSMPL 24
#define SF_PHDR 32
#define SF_NPHDR 40 /* (sans le dernier, EOP) */
#define SF_PBAG 48
#define SF_PGEN 56
#define SF_INST 64
#define SF_IBAG 72
#define SF_IGEN 80
#define SF_SHDR 88
#define SF_NSHDR 96
#define SF_NAME 104 /* 64 octets */
#define SF_PMOD 168
#define SF_IMOD 176
#define SF_BYTES 184

struct aksf {
	void *mem;	 /* 0 SF_MEM */
	u64 size;	 /* 8 SF_SIZE_ */
	const u8 *smpl;	 /* 16 */
	u64 nsmpl;	 /* 24 */
	const u8 *phdr;	 /* 32 */
	u64 nphdr;	 /* 40 */
	const u8 *pbag;	 /* 48 */
	const u8 *pgen;	 /* 56 */
	const u8 *inst;	 /* 64 */
	const u8 *ibag;	 /* 72 */
	const u8 *igen;	 /* 80 */
	const u8 *shdr;	 /* 88 */
	u64 nshdr;	 /* 96 */
	char name[64];	 /* 104 */
	const u8 *pmod;	 /* 168 */
	const u8 *imod;	 /* 176 */
};
AK_STATIC_OFF(struct aksf, size, SF_SIZE_);
AK_STATIC_OFF(struct aksf, nshdr, SF_NSHDR);
AK_STATIC_OFF(struct aksf, name, SF_NAME);
AK_STATIC_OFF(struct aksf, pmod, SF_PMOD);
AK_STATIC_OFF(struct aksf, imod, SF_IMOD);
_Static_assert(sizeof(struct aksf) == SF_BYTES, "aksf");

/* ---- tables (ak_gmbank.c) : une seule structure, dans l'ordre de l'asm
 * (ak_sftab.inc puis ak_gmbank.inc) : une lecture au-delà de ak_concave tombe
 * sur les mêmes octets que l'asm (voir sf_voice_start) */
#define GM_NINS 67
struct ak_gmtab {
	s16 concave[128];   /* -400 log10(v / 127) (centibels) */
	u8 prog[128];	    /* programme -> instrument */
	u8 drum[47][2];	    /* touche 35..81 -> instrument, touche jouée */
	u8 ins[GM_NINS][AKFI_SIZE]; /* instruments FM */
	u8 pad[2];	    /* (align 4) */
};
_Static_assert(offsetof(struct ak_gmtab, prog) == 256, "ak_gmtab");
_Static_assert(offsetof(struct ak_gmtab, ins) == 256 + 128 + 94, "ak_gmtab");
extern const struct ak_gmtab ak_gmtab;

/* ---- fonctions partagées (noms des étiquettes de l'asm) */

/* fichier .sf2 en mémoire, octets -> SoundFont, < 0 (le bloc reste à
 * l'appelant ; ak_sf_load range ensuite SF_MEM, SF_SIZE_) */
long sf_parse(const void *buf, u64 n);

/* ---- fonctions publiques (audiokit.abi) */
long ak_sf_open_mem(const void *buf, u64 n);
long ak_sf_name(struct aksf *sf);
long ak_sf_presets(struct aksf *sf);
long ak_sf_preset(struct aksf *sf, u64 i, void *out);
/* audiokit.c : SoundFont du système (0 : aucune) */
long ak_sf_default(void);
long ak_synth_new(struct aksf *sf, long flags);
long ak_synth_free(struct aksy *s);
long ak_synth_reset(struct aksy *s);
long ak_synth_voices(struct aksy *s);
long ak_synth_midi(struct aksy *s, u64 ch, u64 cmd, u64 d1, u64 d2);
long ak_synth_render_f32(struct aksy *s, float *l, float *r, u64 frames);
long ak_synth_render(struct aksy *s, s16 *out, u64 frames);

#endif
