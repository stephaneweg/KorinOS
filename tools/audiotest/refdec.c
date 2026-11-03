/*
 * tools/audiotest/refdec.c - decodeurs de REFERENCE pour les essais des
 * formats d'AudioKit (docs/SON.md, etape 9) : le PCM attendu, sur le PC.
 *
 * Seulement de la colle autour des decodeurs C d'Onyx (minimp3, stb_vorbis,
 * dr_flac, libopus) : ils ne servent qu'a produire le PCM de comparaison ;
 * aucun code n'en est repris dans AudioKit (ecrit d'apres les normes).
 *
 *   refdec <entree.mp3|.ogg|.flac|.opus> <sortie.raw>
 *        -> s16 entrelace ; affiche "frequence canaux trames"
 *   refdec --opusenc <entree.wav> <sortie.opus> <debit> <mode> [trame_ms]
 *        mode : celt | silk | hybrid | auto (Ogg Opus, RFC 7845)
 *   refdec --opuspkt <entree.opus> : liste des paquets (TOC)
 *   refdec --decraw <entree.opus|.mp3|...> : comme la premiere forme, sans
 *        retirer l'avance d'Opus (pre-skip)
 * Construction : tools/audiotest/build_refdec.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MINIMP3_IMPLEMENTATION
#define get_bits mp3_get_bits
#include "minimp3.h"
#undef get_bits
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#include "opus.h"

#define OPUS_SET_FORCE_MODE_REQUEST 11002
#define MODE_SILK_ONLY 1000
#define MODE_HYBRID 1001
#define MODE_CELT_ONLY 1002

static unsigned char *load(const char *p, long *n) {
  FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
  fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = malloc(*n + 16); fread(b, 1, *n, f); fclose(f); return b;
}

static const char *ext(const char *p) { const char *e = strrchr(p, '.'); return e ? e + 1 : ""; }

/* --- Ogg : pages -> paquets ------------------------------------------- */
typedef struct { unsigned char *d; long n, pos; unsigned char *page; int seg, nseg; long body, segoff;
                 unsigned char pkt[65536 * 4]; } ogg_rd;
/* -> longueur du paquet suivant (-1 en fin) ; granule de la page qui le termine */
static long ogg_next(ogg_rd *o, int64_t *gran) {
  long len = 0;
  for (;;) {
    if (!o->page || o->seg >= o->nseg) {
      if (o->pos + 27 > o->n || memcmp(o->d + o->pos, "OggS", 4)) return -1;
      o->page = o->d + o->pos; o->nseg = o->page[26]; o->seg = 0;
      o->body = o->pos + 27 + o->nseg; o->segoff = 0;
      long tot = 0; for (int i = 0; i < o->nseg; i++) tot += o->page[27 + i];
      o->pos = o->body + tot;
    }
    while (o->seg < o->nseg) {
      int l = o->page[27 + o->seg];
      memcpy(o->pkt + len, o->d + o->body + o->segoff, l);
      len += l; o->segoff += l; o->seg++;
      if (l < 255) { memcpy(gran, o->page + 6, 8); return len; }
    }
  }
}

static int do_opus(const char *in, FILE *out, int keep_pre) {
  long n; ogg_rd *o = calloc(1, sizeof *o); o->d = load(in, &n); o->n = n;
  int64_t g; long l = ogg_next(o, &g);
  if (l < 19 || memcmp(o->pkt, "OpusHead", 8)) { fprintf(stderr, "pas OpusHead\n"); return 1; }
  int ch = o->pkt[9]; int pre = o->pkt[10] | o->pkt[11] << 8;
  if (o->pkt[18] != 0) { fprintf(stderr, "famille %d non geree\n", o->pkt[18]); return 1; }
  ogg_next(o, &g); /* OpusTags */
  int err; OpusDecoder *dec = opus_decoder_create(48000, ch, &err);
  static short pcm[5760 * 2]; long frames = 0, skip = keep_pre ? 0 : pre;
  while ((l = ogg_next(o, &g)) >= 0) {
    int r = opus_decode(dec, o->pkt, l, pcm, 5760, 0);
    if (r < 0) { fprintf(stderr, "erreur %d\n", r); return 1; }
    int s = 0; if (skip) { s = skip < r ? skip : r; skip -= s; }
    fwrite(pcm + s * ch, 2 * ch, r - s, out); frames += r - s;
  }
  printf("48000 %d %ld\n", ch, frames); return 0;
}

/* page Ogg : un paquet (< 255*255 o) par page */
static uint32_t crc_tab[256];
static void crc_init(void) { for (int i = 0; i < 256; i++) { uint32_t r = (uint32_t)i << 24; for (int j = 0; j < 8; j++) r = r & 0x80000000u ? r << 1 ^ 0x04C11DB7u : r << 1; crc_tab[i] = r; } }
static void ogg_page(FILE *f, const unsigned char *p, int len, int64_t gran, int flags, uint32_t seq) {
  unsigned char h[27 + 255]; int ns = len / 255 + 1;
  memcpy(h, "OggS", 4); h[4] = 0; h[5] = flags;
  for (int i = 0; i < 8; i++) h[6 + i] = (unsigned char)(gran >> (8 * i));
  uint32_t serial = 0x41524D50; memcpy(h + 14, &serial, 4); memcpy(h + 18, &seq, 4);
  memset(h + 22, 0, 4); h[26] = ns;
  for (int i = 0; i < ns - 1; i++) h[27 + i] = 255; h[27 + ns - 1] = len % 255;
  uint32_t c = 0;
  for (int i = 0; i < 27 + ns; i++) c = c << 8 ^ crc_tab[(c >> 24) ^ h[i]];
  for (int i = 0; i < len; i++) c = c << 8 ^ crc_tab[(c >> 24) ^ p[i]];
  memcpy(h + 22, &c, 4);
  fwrite(h, 1, 27 + ns, f); fwrite(p, 1, len, f);
}

static int do_opusenc(const char *in, const char *outp, int rate, const char *mode, double fms) {
  long n; unsigned char *w = load(in, &n);
  int ch = w[22], sr = *(int *)(w + 24); long pos = 12; unsigned char *data = 0; long dlen = 0;
  while (pos + 8 <= n) { int l = *(int *)(w + pos + 4); if (!memcmp(w + pos, "data", 4)) { data = w + pos + 8; dlen = l; break; } pos += 8 + l + (l & 1); }
  if (sr != 48000) { fprintf(stderr, "le WAV doit etre a 48 kHz\n"); return 1; }
  int app = OPUS_APPLICATION_AUDIO, force = 0;
  if (!strcmp(mode, "celt")) force = MODE_CELT_ONLY;
  else if (!strcmp(mode, "silk")) { force = MODE_SILK_ONLY; app = OPUS_APPLICATION_VOIP; }
  else if (!strcmp(mode, "hybrid")) { force = MODE_HYBRID; app = OPUS_APPLICATION_VOIP; }
  int err; OpusEncoder *e = opus_encoder_create(48000, ch, app, &err);
  opus_encoder_ctl(e, OPUS_SET_BITRATE(rate));
  if (force) opus_encoder_ctl(e, OPUS_SET_FORCE_MODE_REQUEST, force);
  if (!strcmp(mode, "silk")) opus_encoder_ctl(e, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
  int la; opus_encoder_ctl(e, OPUS_GET_LOOKAHEAD(&la));
  crc_init(); FILE *f = fopen(outp, "wb"); uint32_t seq = 0;
  unsigned char head[19]; memcpy(head, "OpusHead", 8); head[8] = 1; head[9] = ch; head[10] = la & 255; head[11] = la >> 8;
  uint32_t r48 = 48000; memcpy(head + 12, &r48, 4); head[16] = head[17] = 0; head[18] = 0;
  ogg_page(f, head, 19, 0, 2, seq++);
  unsigned char tags[64]; memcpy(tags, "OpusTags", 8); uint32_t vl = 6; memcpy(tags + 8, &vl, 4); memcpy(tags + 12, "refdec", 6);
  uint32_t nc = 1; memcpy(tags + 18, &nc, 4); const char *t = "TITLE=Essai Opus"; uint32_t tl = strlen(t); memcpy(tags + 22, &tl, 4); memcpy(tags + 26, t, tl);
  ogg_page(f, tags, 26 + tl, 0, 0, seq++);
  int fs = (int)(48 * fms); long total = dlen / (2 * ch); int64_t gran = 0; unsigned char pkt[4000];
  short *buf = calloc(fs * ch, 2);
  for (long i = 0; i < total + la; i += fs) {
    memset(buf, 0, fs * ch * 2);
    long k = total - i; if (k > fs) k = fs; if (k > 0) memcpy(buf, data + i * 2 * ch, k * 2 * ch);
    int len = opus_encode(e, buf, fs, pkt, sizeof pkt);
    if (len < 0) { fprintf(stderr, "encode %d\n", len); return 1; }
    gran += fs; int last = i + fs >= total + la;
    ogg_page(f, pkt, len, last ? total + la : gran, last ? 4 : 0, seq++);
  }
  fclose(f); printf("%ld trames, avance %d\n", total, la); return 0;
}

static int do_opuspkt(const char *in) {
  long n; ogg_rd *o = calloc(1, sizeof *o); o->d = load(in, &n); o->n = n; int64_t g; long l; int k = 0;
  while ((l = ogg_next(o, &g)) >= 0) { if (k++ < 2) continue; int toc = o->pkt[0]; printf("%d config %d s %d c %d  %ld o\n", k - 3, toc >> 3, toc >> 2 & 1, toc & 3, l); }
  return 0;
}

int main(int argc, char **argv) {
  int keep = 0;
  if (argc >= 6 && !strcmp(argv[1], "--opusenc")) return do_opusenc(argv[2], argv[3], atoi(argv[4]), argv[5], argc > 6 ? atof(argv[6]) : 20);
  if (argc >= 3 && !strcmp(argv[1], "--opuspkt")) return do_opuspkt(argv[2]);
  if (argc >= 2 && !strcmp(argv[1], "--decraw")) { keep = 1; argv++; argc--; }
  if (argc < 3) { fprintf(stderr, "refdec entree sortie.raw\n"); return 1; }
  const char *e = ext(argv[1]); FILE *out = fopen(argv[2], "wb");
  if (!strcmp(e, "opus")) return do_opus(argv[1], out, keep);
  if (!strcmp(e, "flac")) {
    unsigned int ch, sr; drflac_uint64 fr;
    short *p = drflac_open_file_and_read_pcm_frames_s16(argv[1], &ch, &sr, &fr, NULL);
    if (!p) { fprintf(stderr, "dr_flac : echec\n"); return 1; }
    fwrite(p, 2 * ch, fr, out); printf("%u %u %llu\n", sr, ch, (unsigned long long)fr); return 0;
  }
  if (!strcmp(e, "ogg")) {
    int ch, sr; short *p; int fr = stb_vorbis_decode_filename(argv[1], &ch, &sr, &p);
    if (fr < 0) { fprintf(stderr, "stb_vorbis : echec\n"); return 1; }
    fwrite(p, 2 * ch, fr, out); printf("%d %d %d\n", sr, ch, fr); return 0;
  }
  if (!strcmp(e, "mp3")) {
    long n; unsigned char *b = load(argv[1], &n); static mp3dec_t d; mp3dec_init(&d);
    mp3dec_frame_info_t info; static short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]; long pos = 0, fr = 0; int ch = 0, sr = 0;
    while (pos < n) {
      int s = mp3dec_decode_frame(&d, b + pos, n - pos, pcm, &info);
      if (!info.frame_bytes) break;
      pos += info.frame_bytes;
      if (!s && getenv("REFDEC_VERBOSE")) fprintf(stderr, "trame a %ld (%d o) : rien\n", pos - info.frame_bytes, info.frame_bytes);
      if (s) { ch = info.channels; sr = info.hz; fwrite(pcm, 2 * ch, s, out); fr += s; }
    }
    printf("%d %d %ld\n", sr, ch, fr); return 0;
  }
  fprintf(stderr, "format inconnu : %s\n", e); return 1;
}
