/* ArchiveKit en C : déclarations communes aux fichiers du kit (archivekit.c,
 * ar_deflate.c, ar_inflate.c, ar_read.c, ar_write.c). Réécriture de
 * archivekit.asm et de ses .inc : mêmes résultats, mêmes codes d'erreur,
 * mêmes octets compressés. */
#ifndef AR_H
#define AR_H

/* tout ce qui est partagé entre les fichiers est caché : accès relatifs au
 * compteur ordinal, aucune table d'adresses (GOT) */
#pragma GCC visibility push(hidden)

#include "../common/korin_kit.h"
#include "appkit_import.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* archivekit.inc */
#define AR_RAW 0
#define AR_ZLIB 1
#define AR_GZIP 2
#define AR_AUTO 3
#define AR_DEFAULT 6
#define AR_FMT_ZIP 1
#define AR_FMT_TAR 2
#define AR_FMT_TGZ 3
#define AR_FMT_GZIP 4
#define AR_E_IO (-5)
#define AR_E_NOMEM (-12)
#define AR_E_CRYPT (-13)
#define AR_E_INVAL (-22)
#define AR_E_FBIG (-27)
#define AR_E_SPACE (-28)
#define AR_E_METHOD (-38)
#define AR_E_CORRUPT (-74)
#define AR_E_CRC (-84)
#define AR_E_FORMAT (-95)
#define ARE_NAME 0
#define ARFI_SIZE 128

/* src/inc/armpi.inc, src/inc/fs.inc */
#define O_READ 1
#define O_WRITE 2
#define O_CREATE 4
#define O_TRUNC 8
#define ATTR_DIR 0x10
#define V_STAT 31
#define V_UTIME 42
#define MSG_SIZE 512
#define E_NOENT (-2)
#define PROT_READ 1
#define PROT_WRITE 2

/* poignées : reconnues à leur premier mot */
#define AR_MAGIC_BUF 0x4655424B5241ULL /* "ARKBUF" */
#define AR_MAGIC_RD 0x44524B5241ULL    /* "ARKRD" */
#define AR_MAGIC_WR 0x52574B5241ULL    /* "ARKWR" */

#define AR_STOP 1 /* le puits a ce qu'il voulait */

/* ---- puits : où vont les octets */
#define SKK_NULL 0  /* compte seulement */
#define SKK_FIXED 1 /* tampon de l'appelant (AR_E_SPACE) */
#define SKK_GROW 2  /* bloc du kit, qui grandit */
#define SKK_FD 3    /* fichier */
#define SKK_PROBE 4 /* tampon fixe, AR_STOP quand il est plein */

struct sink {
	u64 kind;
	u64 ptr; /* tampon, ou descripteur */
	u64 pos; /* octets écrits */
	u64 cap;
};

/* tampon du kit (ar_deflate_new, ar_inflate_new, ar_load) */
struct arbuf {
	u64 magic;
	u64 data;
	u64 size;
	u64 pad;
	struct sink sink; /* B_SINK = 32 */
};

/* les 32 bits à l'envers (rbit) */
static inline u32 ar_rev32(u32 v)
{
	v = (v >> 1 & 0x55555555u) | (v & 0x55555555u) << 1;
	v = (v >> 2 & 0x33333333u) | (v & 0x33333333u) << 2;
	v = (v >> 4 & 0x0F0F0F0Fu) | (v & 0x0F0F0F0Fu) << 4;
	return __builtin_bswap32(v);
}

/* ---- mémoire, sommes, puits (archivekit.c) */
void *ar_alloc(u64 n);
void ar_mfree(void *p);
void ar_memcpy(void *d, const void *s, u64 n);
u32 crc32_upd(u32 crc, const u8 *p, u64 n);
u32 adler32_upd(u32 adler, const u8 *p, u64 n);
long sink_write(struct sink *k, const void *data, u64 n);
long sink_zero(struct sink *k);
struct arbuf *buf_new(void);
long buf_done(struct arbuf *b);
long ar_level_of(long level);
long wrap_head(struct sink *k, u64 wrap, u64 level);

/* ---- deflate (ar_deflate.c) */
#define DF_HISTORY 32768
#define DF_CHUNK 131072
#define DF_WINCAP (DF_HISTORY + DF_CHUNK)
#define DF_TOKENS 32767
#define DF_HBITS 15
#define DF_OSIZE 65536
#define DF_OSLACK 64

struct defl {
	struct sink *sink;
	u64 level, good, lazy, nice, chainn;
	u64 pos;   /* prochain octet à traiter (décalage dans la fenêtre) */
	u64 end;   /* fin des octets reçus */
	u64 blk;   /* début du bloc en cours */
	u64 floor; /* on ne remonte pas avant (début du flux) */
	u64 base;  /* position absolue = base + décalage */
	u64 ntok;
	u64 bb, bc; /* bits en attente */
	u64 op;     /* octets dans le tampon de sortie */
	u64 wrap, crc, adler, total;
	u64 err; /* première erreur du puits */
	u64 final;
	u64 bend; /* fin du bloc en cours d'écriture */
	u32 lfreq[288], dfreq[32], bfreq[20];
	u8 llen[288], dlen[32], blen[20];
	u32 ltab[288], dtab[32], btab[20]; /* code inversé | longueur << 16 */
	u32 fltab[288], fdtab[32];         /* les codes fixes */
	u8 seq[320];
	u16 rle[320];
	u64 sort[288];
	u32 work[288];
	u32 cnt[34];
	u32 tok[DF_TOKENS + 1];
	u32 head[1 << DF_HBITS];
	u32 prev[32768];
	u8 obuf[DF_OSIZE + DF_OSLACK];
	u8 win[DF_WINCAP + 32];
};

extern const u8 df_clorder[19];
struct defl *df_new(struct sink *k);
void df_begin(struct defl *s, u64 level, u64 wrap);
long df_feed(struct defl *s, const u8 *data, u64 n);
long df_finish(struct defl *s);
long wrap_tail(struct sink *k, struct defl *s);

/* ---- inflate (ar_inflate.c) */
#define IF_LBITS 10
#define IF_DBITS 9
#define IF_ICHUNK 65536
#define IF_OCHUNK 262144
#define IF_HISTORY 32768
#define IF_OCAP (IF_HISTORY + IF_OCHUNK)
#define IF_OVERMAX 16

struct reader;

struct infl {
	uintptr_t ip, iend;
	u64 over; /* octets inventés après la fin de l'entrée */
	u8 *obase, *oend;
	u8 *flushed; /* ce qui précède a déjà eu sa somme et son puits */
	struct sink *sink;
	u64 total; /* octets déjà passés */
	u64 crc, adler;
	u64 wrap; /* enveloppe (AR_RAW..AR_AUTO) */
	u64 want; /* 1 CRC, 2 Adler du membre en cours */
	struct reader *src; /* archive d'où tirer l'entrée (0 : tout est déjà là) */
	u64 srcoff, srcleft;
	u64 gztime; /* date du premier membre gzip */
	u64 final;
	u64 members;
	u16 lcount[16], dcount[16];
	u16 lsym[288], dsym[32];
	u8 lens[320 + 2];
	u8 gzname[256]; /* nom du premier membre gzip */
	u16 ltab[1 << IF_LBITS];
	u16 dtab[1 << IF_DBITS];
	u8 ibuf[IF_ICHUNK + 16];
	u8 obuf[IF_OCAP + 16];
};

void if_begin(struct infl *s, const u8 *in, u64 n, u8 *out, u64 cap, struct sink *k, u64 wrap);
long if_run(struct infl *s);

/* ---- lire (ar_read.c) */
struct entry {
	u8 *name;
	u64 size;
	u64 pack;
	u64 off; /* ZIP : en-tête local ; tar : données */
	u64 time;
	u32 crc;
	u32 method;
	u32 flags; /* bit 0 : dossier, bit 1 : chiffrée */
	u32 pad;
};

struct reader {
	u64 magic, data, size;
	u64 fmt;
	u8 *mem; /* la source en mémoire, 0 : fichier */
	long fd; /* descripteur, -1 : aucun */
	u64 len; /* octets de la source */
	struct entry *tbl;
	u64 n, cap;
	u8 *pool; /* morceau de noms en cours (u64 : suivant, puis les noms) */
	u64 poolpos;
	struct infl *ifs; /* état d'inflate */
	u8 *inner;        /* le tar décompressé d'un tar.gz (bloc) */
	u8 *lng;          /* nom long du tar en attente (bloc de RD_LONG octets) */
	u64 probe;        /* 1 : on veut seulement le format */
	u64 rawlen;       /* taille de l'archive sur la carte */
	u8 pad2[256 - 136];
	char base[256]; /* nom de base du fichier */
};

long src_read(struct reader *a, u64 pos, void *buf, u64 n);
long ar_close(long a);
long ar_vfs(void *msg);

/* ---- écrire (ar_write.c) */
long wr_free(void *w);

/* fonctions exportées appelées entre fichiers */
long ar_crc32(long crc, long data, long n);
long ar_adler32(long adler, long data, long n);
long ar_free(long h);
long ar_entry_size(long a, long i);
long ar_name(long a, long i);
long ar_format_name(long f);

#pragma GCC visibility pop

#endif
